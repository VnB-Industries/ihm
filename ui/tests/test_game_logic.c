/* Host tests for the rules layer.
 *
 * game_logic.c no longer includes lvgl.h, so these run on a plain host with
 * nothing but sqlite3 - which is the point of decoupling it. */

#include "game_logic.h"
#include "game_db.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_fail = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        printf("  FAIL %s:%d: ", __func__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); \
        g_fail++; \
    } \
} while (0)

static const char *k_db = "/tmp/ihm_test_game.db";

static void fresh_db(void)
{
    db_close();
    unlink(k_db);
    unlink("/tmp/ihm_test_game.db-wal");
    unlink("/tmp/ihm_test_game.db-shm");
    if (db_init(k_db) != 0) { printf("db_init failed\n"); exit(1); }
}

/* ── RNG ────────────────────────────────────────────────────────────────── */

static void test_rng_range_and_seed(void)
{
    game_rng_seed(12345);
    for (int i = 0; i < 20000; i++) {
        uint32_t v = game_rand(0, 3);
        CHECK(v <= 3, "game_rand(0,3) returned %u", v);
    }

    /* A single value must be returned, not an infinite loop. */
    CHECK(game_rand(7, 7) == 7, "degenerate range");

    /* Different seeds must diverge, otherwise every boot replays one game. */
    game_rng_seed(1);
    uint32_t a = game_rand(0, 1000000);
    game_rng_seed(2);
    uint32_t b = game_rand(0, 1000000);
    CHECK(a != b, "seeds 1 and 2 produced the same first value %u", a);

    /* Same seed must reproduce, so a failure can be replayed. */
    game_rng_seed(99);
    uint32_t c1 = game_rand(0, 1000000);
    game_rng_seed(99);
    uint32_t c2 = game_rand(0, 1000000);
    CHECK(c1 == c2, "same seed diverged: %u vs %u", c1, c2);

    /* A zero seed must not wedge the generator at zero. */
    game_rng_seed(0);
    uint32_t z1 = game_rand(0, 1000000);
    uint32_t z2 = game_rand(0, 1000000);
    CHECK(!(z1 == 0 && z2 == 0), "zero seed produced a stuck sequence");
}

static void test_rng_uniform_enough(void)
{
    /* 12 buckets does not divide 2^32, so a plain modulo would skew low.
     * Allow 3% from the mean. */
    const int N = 12, ITER = 600000;
    int hist[12] = {0};

    game_rng_seed(0xC0FFEE);
    for (int i = 0; i < ITER; i++) hist[game_rand(0, N - 1)]++;

    double mean = (double)ITER / N;
    for (int i = 0; i < N; i++) {
        double dev = (hist[i] - mean) / mean;
        if (dev < 0) dev = -dev;
        CHECK(dev < 0.03, "bucket %d off by %.2f%% (%d vs %.0f)",
              i, dev * 100.0, hist[i], mean);
    }
}

/* ── DB ─────────────────────────────────────────────────────────────────── */

static void test_update_user_persists_name_and_tag(void)
{
    fresh_db();
    int id = db_add_user("Alice");
    CHECK(id > 0, "db_add_user returned %d", id);

    user_record_t u;
    CHECK(db_get_user(id, &u) == 0, "db_get_user failed");

    snprintf(u.name, sizeof(u.name), "%s", "Alicia");
    snprintf(u.rfid_tag, sizeof(u.rfid_tag), "%s", "DEADBEEF");
    u.is_admin = true;
    u.total_cl = 42;
    CHECK(db_update_user(&u) == 0, "db_update_user failed");

    user_record_t back;
    CHECK(db_get_user(id, &back) == 0, "re-read failed");
    CHECK(strcmp(back.name, "Alicia") == 0, "name not persisted: '%s'", back.name);
    CHECK(strcmp(back.rfid_tag, "DEADBEEF") == 0, "tag not persisted: '%s'", back.rfid_tag);
    CHECK(back.is_admin, "is_admin not persisted");
    CHECK(back.total_cl == 42, "total_cl not persisted: %d", back.total_cl);

    /* The tag must now resolve back to this user. */
    user_record_t byrfid;
    CHECK(db_get_user_by_rfid("DEADBEEF", &byrfid) == 0, "lookup by rfid failed");
    CHECK(byrfid.id == id, "rfid lookup hit the wrong user");
}

static void test_empty_tag_is_null_not_shared(void)
{
    fresh_db();
    int a = db_add_user("A");
    int b = db_add_user("B");

    user_record_t ua, ub;
    db_get_user(a, &ua);
    db_get_user(b, &ub);
    ua.rfid_tag[0] = '\0';
    ub.rfid_tag[0] = '\0';
    db_update_user(&ua);
    db_update_user(&ub);

    /* Two tagless users must not both answer to "". */
    user_record_t out;
    CHECK(db_get_user_by_rfid("", &out) != 0, "empty tag matched a user");
}

static void test_foreign_keys_cascade_history(void)
{
    fresh_db();
    int id = db_add_user("Bob");

    for (int i = 0; i < 5; i++)
        CHECK(db_add_cl_history(id, 1000 + i, i * 2) == 0, "history insert failed");

    cl_history_point_t pts[16];
    CHECK(db_get_cl_history(id, pts, 16) == 5, "expected 5 history rows");

    CHECK(db_delete_user(id) == 0, "delete user failed");

    /* Without PRAGMA foreign_keys=ON these rows would survive forever. */
    int left = db_get_cl_history(id, pts, 16);
    CHECK(left == 0, "history not cascaded: %d rows left", left);
}

static int count_rows(const char *file, const char *table)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    char sql[96];
    int n = -1;

    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s;", table);
    if (sqlite3_open_v2(file, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK &&
        sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK &&
        sqlite3_step(st) == SQLITE_ROW)
        n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    sqlite3_close(db);
    return n;
}

static void test_archive_and_reset_stats(void)
{
    fresh_db();
    db_set_config("max_bonus_stack", 9);
    db_set_text_config("home_banner_text_2", "kept");
    int cid = db_add_cocktail("Mauresque");

    int a = db_add_user("Alice");
    int b = db_add_user("Bob");
    user_record_t u;
    db_get_user(a, &u);
    u.total_cl = 42;
    db_update_user(&u);
    db_add_cl_history(a, 1000, 42);
    db_add_cl_history(b, 1001, 7);

    char arc[320] = "";
    CHECK(db_archive_and_reset_stats(arc, sizeof(arc)) == 0, "archive+reset failed");
    CHECK(arc[0] != '\0', "archive path not reported");

    /* Archive holds what was wiped. */
    CHECK(count_rows(arc, "users") == 2, "archive users");
    CHECK(count_rows(arc, "cl_history") == 2, "archive history");

    /* Live database is blank... */
    user_record_t all[GAME_DB_MAX_USERS];
    CHECK(db_get_all_users(all, GAME_DB_MAX_USERS) == 0, "users not wiped");
    cl_history_point_t pts[8];
    CHECK(db_get_cl_history(a, pts, 8) == 0, "history not wiped");

    /* ...ids restart... */
    CHECK(db_add_user("Carol") == 1, "ids did not restart");

    /* ...and every other setting survives. */
    CHECK(db_get_config("max_bonus_stack", 0) == 9, "config lost");
    char txt[32];
    db_get_text_config("home_banner_text_2", txt, sizeof(txt), "");
    CHECK(strcmp(txt, "kept") == 0, "text config lost");
    cocktail_record_t ck[4];
    CHECK(db_get_all_cocktails(ck, 4) == 1 && ck[0].id == cid, "cocktail lost");

    unlink(arc);
}

static void test_history_pruning(void)
{
    fresh_db();
    db_set_config("cl_history_max_per_user", 10);
    int id = db_add_user("Carol");

    for (int i = 0; i < 40; i++)
        db_add_cl_history(id, 2000 + i, i);

    cl_history_point_t pts[64];
    int n = db_get_cl_history(id, pts, 64);
    CHECK(n == 10, "expected 10 retained rows, got %d", n);
    /* The newest must be the ones kept. */
    CHECK(pts[n - 1].epoch == 2039, "newest row lost, got epoch %lld",
          (long long)pts[n - 1].epoch);
    CHECK(pts[0].epoch == 2030, "pruned the wrong end, oldest kept is %lld",
          (long long)pts[0].epoch);
}

static void test_config_cache_coherent(void)
{
    fresh_db();
    CHECK(db_get_config("spin_cooldown_seconds", -1) == 0, "default not seeded");
    db_set_config("spin_cooldown_seconds", 77);
    CHECK(db_get_config("spin_cooldown_seconds", -1) == 77,
          "cache did not see the write");
}

/* ── cooldown / modifiers ───────────────────────────────────────────────── */

static void test_spin_cooldown_arms_blocked_until(void)
{
    fresh_db();
    db_set_config("spin_cooldown_seconds", 60);
    int id = db_add_user("Dave");

    user_record_t u;
    db_get_user(id, &u);
    CHECK(game_spin_remaining_seconds(&u) <= 0, "fresh user already blocked");

    game_on_spin_started(id);
    db_get_user(id, &u);

    int64_t rem = game_spin_remaining_seconds(&u);
    CHECK(rem > 55 && rem <= 60, "cooldown not armed, remaining=%lld", (long long)rem);
    /* last_spin_epoch must record the spin, not the block. */
    CHECK(u.last_spin_epoch <= (int64_t)time(NULL),
          "last_spin_epoch is in the future: it is not a 'blocked until' field");
}

static void test_timeout_add_and_remove(void)
{
    fresh_db();
    db_set_config("spin_cooldown_seconds", 0);
    db_set_config("timeout_modifier_minutes", 5);
    int giver = db_add_user("Giver");
    int target = db_add_user("Target");

    /* +Temps must block a user who has never spun. */
    game_apply_modifier(giver, target, MODIFIER_TIMEOUT_ADD);
    user_record_t t;
    db_get_user(target, &t);
    int64_t rem = game_spin_remaining_seconds(&t);
    CHECK(rem > 290 && rem <= 300, "TIMEOUT_ADD gave remaining=%lld", (long long)rem);

    /* Stacking must add, not replace. */
    game_apply_modifier(giver, target, MODIFIER_TIMEOUT_ADD);
    db_get_user(target, &t);
    rem = game_spin_remaining_seconds(&t);
    CHECK(rem > 590 && rem <= 600, "stacked TIMEOUT_ADD gave %lld", (long long)rem);

    /* -Temps must claw time back. */
    game_apply_modifier(giver, target, MODIFIER_TIMEOUT_REMOVE);
    db_get_user(target, &t);
    rem = game_spin_remaining_seconds(&t);
    CHECK(rem > 290 && rem <= 300, "TIMEOUT_REMOVE gave %lld", (long long)rem);

    /* The giver's record must remember what it handed out. */
    user_record_t g;
    db_get_user(giver, &g);
    CHECK(g.given_modifier == (int)MODIFIER_TIMEOUT_REMOVE, "giver modifier not recorded");
    CHECK(g.given_to_id == target, "giver target not recorded");
}

static void test_timeout_remove_floors_at_zero(void)
{
    fresh_db();
    db_set_config("timeout_modifier_minutes", 5);
    int a = db_add_user("A");
    game_apply_modifier(a, a, MODIFIER_TIMEOUT_REMOVE);

    user_record_t u;
    db_get_user(a, &u);
    CHECK(u.spin_blocked_until >= 0, "spin_blocked_until went negative: %lld",
          (long long)u.spin_blocked_until);
    CHECK(game_spin_remaining_seconds(&u) <= 0, "user blocked after -Temps");
}

static void test_bonus_malus_respect_caps(void)
{
    fresh_db();
    db_set_config("max_bonus_stack", 2);
    db_set_config("max_malus_stack", 1);
    int a = db_add_user("A");

    for (int i = 0; i < 5; i++) game_apply_modifier(a, a, MODIFIER_BONUS);
    for (int i = 0; i < 5; i++) game_apply_modifier(a, a, MODIFIER_MALUS);

    user_record_t u;
    db_get_user(a, &u);
    CHECK(u.bonus == 2, "bonus exceeded cap: %d", u.bonus);
    CHECK(u.malus == 1, "malus exceeded cap: %d", u.malus);
}

/* ── pour accounting ────────────────────────────────────────────────────── */

static void test_dispense_credits_measured_volume(void)
{
    fresh_db();
    db_set_config("wheel_trigger_chance", 0);   /* keep the bonus roll out of it */
    int id = db_add_user("Eve");

    game_on_dispense_complete(id, 4);
    game_on_dispense_complete(id, 1);   /* a pour cut short */

    user_record_t u;
    db_get_user(id, &u);
    CHECK(u.total_cl == 5, "expected 5 cL credited, got %d", u.total_cl);

    /* Each completed pour must leave a history point. */
    cl_history_point_t pts[8];
    CHECK(db_get_cl_history(id, pts, 8) == 2, "expected 2 history points");
    CHECK(pts[1].total_cl == 5, "history total wrong: %d", pts[1].total_cl);
}

static void test_dispense_clamps_negative(void)
{
    fresh_db();
    db_set_config("wheel_trigger_chance", 0);
    int id = db_add_user("Frank");
    game_on_dispense_complete(id, -7);

    user_record_t u;
    db_get_user(id, &u);
    CHECK(u.total_cl == 0, "negative volume credited: %d", u.total_cl);
}

static void test_bonus_trigger_fires_at_full_chance(void)
{
    fresh_db();
    db_set_config("wheel_trigger_chance", 100);
    int id = db_add_user("Gina");

    game_rng_seed(4242);
    CHECK(game_on_dispense_complete(id, 2), "100%% chance did not trigger");

    user_record_t u;
    db_get_user(id, &u);
    CHECK(u.wheel_trigger == 1, "wheel_trigger not incremented: %d", u.wheel_trigger);
}

/* ── wheel segments ─────────────────────────────────────────────────────── */

static void test_wheel_segments_shift_and_floor(void)
{
    fresh_db();
    int id = db_add_user("Hugo");
    user_record_t u;
    db_get_user(id, &u);

    game_segment_t segs[GAME_MAX_SEGMENTS];
    uint8_t n = 0;

    /* Neutral: the base ladder. */
    game_compute_wheel_segments(&u, segs, &n);
    CHECK(n == 4, "expected 4 segments, got %u", n);
    CHECK(segs[0].value == 0 && segs[1].value == 2
          && segs[2].value == 4 && segs[3].value == 6, "base ladder wrong");

    /* Malus shifts up by 2 cL each. */
    u.malus = 1; u.bonus = 0;
    game_compute_wheel_segments(&u, segs, &n);
    CHECK(segs[0].value == 2 && segs[3].value == 8, "malus shift wrong");

    /* Bonus shifts down and must floor at 0, never wrap. */
    u.malus = 0; u.bonus = 3;
    game_compute_wheel_segments(&u, segs, &n);
    CHECK(segs[0].value == 0, "bonus shift did not floor: %u", segs[0].value);
    CHECK(segs[3].value == 0, "expected floor at 0, got %u", segs[3].value);
    for (int i = 0; i < 4; i++)
        CHECK(segs[i].value < 1000, "segment %d wrapped: %u", i, segs[i].value);
}

static void test_bonus_segments_weights(void)
{
    fresh_db();
    db_set_config("bonus_wheel_bonus_weight", 2);
    db_set_config("bonus_wheel_nothing_weight", 1);
    db_set_config("bonus_wheel_malus_weight", 0);
    db_set_config("bonus_wheel_timeout_add_weight", 0);
    db_set_config("bonus_wheel_timeout_remove_weight", 0);

    game_segment_t segs[GAME_MAX_SEGMENTS];
    uint8_t n = 0;
    game_compute_bonus_segments(segs, &n);
    CHECK(n == 3, "expected 3 slices, got %u", n);

    int bonus = 0, nothing = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (segs[i].value == BONUS_WHEEL_VAL_BONUS) bonus++;
        if (segs[i].value == BONUS_WHEEL_VAL_NOTHING) nothing++;
    }
    CHECK(bonus == 2, "bonus weight not honoured: %d", bonus);
    CHECK(nothing == 1, "nothing weight not honoured: %d", nothing);

    /* Every slice needs a label: the wheel skips slices without one. */
    for (uint8_t i = 0; i < n; i++)
        CHECK(segs[i].label != NULL, "slice %u has no label", i);
}

static void test_bonus_segments_all_zero_fallback(void)
{
    fresh_db();
    db_set_config("bonus_wheel_bonus_weight", 0);
    db_set_config("bonus_wheel_nothing_weight", 0);
    db_set_config("bonus_wheel_malus_weight", 0);
    db_set_config("bonus_wheel_timeout_add_weight", 0);
    db_set_config("bonus_wheel_timeout_remove_weight", 0);

    game_segment_t segs[GAME_MAX_SEGMENTS];
    uint8_t n = 0;
    game_compute_bonus_segments(segs, &n);
    CHECK(n == 3, "all-zero weights should fall back to 3 slices, got %u", n);
}

static void test_bonus_segments_cap(void)
{
    fresh_db();
    /* Weights far past the ceiling must be clamped, not overflow the array. */
    db_set_config("bonus_wheel_bonus_weight", 50);
    db_set_config("bonus_wheel_nothing_weight", 50);
    db_set_config("bonus_wheel_malus_weight", 50);
    db_set_config("bonus_wheel_timeout_add_weight", 50);
    db_set_config("bonus_wheel_timeout_remove_weight", 50);

    game_segment_t segs[GAME_MAX_SEGMENTS];
    uint8_t n = 0;
    game_compute_bonus_segments(segs, &n);
    CHECK(n == GAME_MAX_SEGMENTS, "expected clamp to %d, got %u", GAME_MAX_SEGMENTS, n);
}

/* ── migration ──────────────────────────────────────────────────────────── */

static void test_legacy_timeout_migrates_to_blocked_until(void)
{
    /* Build a pre-split schema where a pending timeout was stored as a future
     * last_spin_epoch, then let db_init migrate it. */
    db_close();
    unlink(k_db);
    unlink("/tmp/ihm_test_game.db-wal");
    unlink("/tmp/ihm_test_game.db-shm");

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "sqlite3 %s \"CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT NOT NULL UNIQUE COLLATE NOCASE, is_admin INTEGER NOT NULL DEFAULT 0,"
        " bonus INTEGER NOT NULL DEFAULT 0, malus INTEGER NOT NULL DEFAULT 0,"
        " wheel_trigger INTEGER NOT NULL DEFAULT 0, total_cl INTEGER NOT NULL DEFAULT 0,"
        " given_modifier INTEGER NOT NULL DEFAULT 0, given_to_id INTEGER NOT NULL DEFAULT -1,"
        " last_spin_epoch INTEGER NOT NULL DEFAULT 0, rfid_tag TEXT);"
        " INSERT INTO users (name, last_spin_epoch) VALUES"
        " ('Blocked', strftime('%%s','now') + 600),"
        " ('Idle', strftime('%%s','now') - 600);\" ", k_db);
    if (system(cmd) != 0) { printf("  SKIP migration test (sqlite3 CLI missing)\n"); return; }

    CHECK(db_init(k_db) == 0, "db_init on legacy schema failed");

    user_record_t blocked, idle;
    CHECK(db_get_user(1, &blocked) == 0, "legacy user 1 unreadable");
    CHECK(db_get_user(2, &idle) == 0, "legacy user 2 unreadable");

    int64_t rem = game_spin_remaining_seconds(&blocked);
    CHECK(rem > 590 && rem <= 600, "pending timeout lost in migration: %lld",
          (long long)rem);
    CHECK(blocked.last_spin_epoch <= (int64_t)time(NULL),
          "last_spin_epoch left in the future after migration");

    CHECK(game_spin_remaining_seconds(&idle) <= 0, "idle user became blocked");
}

/* The wheel screen announces a handed-out modifier once, not on every visit. */
static void test_modifier_announce_is_one_shot(void)
{
    CHECK(!game_take_modifier_announce(), "announce set before anything happened");

    game_set_modifier_announce();
    CHECK(game_take_modifier_announce(), "announce not reported after being set");
    CHECK(!game_take_modifier_announce(), "announce reported twice");
}

/* Profile tiles show a medal for the top 3 by cL; ties share a place and
 * nobody gets one for 0 cL. */
static void test_cl_rank(void)
{
    user_record_t u[5];
    memset(u, 0, sizeof(u));
    u[0].total_cl = 10;
    u[1].total_cl = 30;
    u[2].total_cl = 20;
    u[3].total_cl = 20;
    u[4].total_cl = 0;

    CHECK(game_cl_rank(u, 5, 1) == 1, "top player not 1st");
    CHECK(game_cl_rank(u, 5, 2) == 2, "tied player not 2nd");
    CHECK(game_cl_rank(u, 5, 3) == 2, "tied player not 2nd");
    CHECK(game_cl_rank(u, 5, 0) == 4, "player behind a tie not 4th");
    CHECK(game_cl_rank(u, 5, 4) == 0, "0 cL player got a place");
    CHECK(game_cl_rank(u, 5, 5) == 0, "out-of-range index got a place");
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"rng_range_and_seed",                test_rng_range_and_seed},
        {"rng_uniform_enough",                test_rng_uniform_enough},
        {"update_user_persists_name_and_tag", test_update_user_persists_name_and_tag},
        {"empty_tag_is_null_not_shared",      test_empty_tag_is_null_not_shared},
        {"foreign_keys_cascade_history",      test_foreign_keys_cascade_history},
        {"archive_and_reset_stats",           test_archive_and_reset_stats},
        {"history_pruning",                   test_history_pruning},
        {"config_cache_coherent",             test_config_cache_coherent},
        {"spin_cooldown_arms_blocked_until",  test_spin_cooldown_arms_blocked_until},
        {"timeout_add_and_remove",            test_timeout_add_and_remove},
        {"timeout_remove_floors_at_zero",     test_timeout_remove_floors_at_zero},
        {"bonus_malus_respect_caps",          test_bonus_malus_respect_caps},
        {"dispense_credits_measured_volume",  test_dispense_credits_measured_volume},
        {"dispense_clamps_negative",          test_dispense_clamps_negative},
        {"bonus_trigger_fires_at_full_chance",test_bonus_trigger_fires_at_full_chance},
        {"wheel_segments_shift_and_floor",    test_wheel_segments_shift_and_floor},
        {"bonus_segments_weights",            test_bonus_segments_weights},
        {"bonus_segments_all_zero_fallback",  test_bonus_segments_all_zero_fallback},
        {"bonus_segments_cap",                test_bonus_segments_cap},
        {"legacy_timeout_migrates",           test_legacy_timeout_migrates_to_blocked_until},
        {"modifier_announce_is_one_shot",     test_modifier_announce_is_one_shot},
        {"cl_rank",                           test_cl_rank},
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    for (int i = 0; i < n; i++) {
        int before = g_fail;
        printf("%-38s", tests[i].name);
        fflush(stdout);
        tests[i].fn();
        printf("%s\n", (g_fail == before) ? "ok" : "FAILED");
    }

    db_close();
    unlink(k_db);
    unlink("/tmp/ihm_test_game.db-wal");
    unlink("/tmp/ihm_test_game.db-shm");

    printf("\n%d check(s) failed\n", g_fail);
    return g_fail ? 1 : 0;
}
