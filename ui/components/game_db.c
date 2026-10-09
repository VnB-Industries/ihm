#include "game_db.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static sqlite3 *s_db = NULL;
static char     s_db_path[256];

/* ── prepared-statement cache ───────────────────────────────────────────────
 * Every query owns a file-static slot that holds its compiled statement.
 * stmt_prep() compiles on first use and only resets afterwards, so the hot
 * paths (db_get_user, db_update_user, db_get_config) stop re-compiling SQL on
 * each call.  Slots are registered here so db_close() can finalize them all.  */

#define DB_STMT_SLOTS 32
static sqlite3_stmt **s_stmt_slots[DB_STMT_SLOTS];
static int            s_stmt_slot_count = 0;

/* Return a statement for @p sql, ready to be bound.
 * @p slot must be a file-static pointer, one per distinct @p sql. */
static sqlite3_stmt *stmt_prep(sqlite3_stmt **slot, const char *sql)
{
    if (!s_db || !slot) return NULL;

    if (*slot) {
        sqlite3_reset(*slot);
        sqlite3_clear_bindings(*slot);
        return *slot;
    }

    if (sqlite3_prepare_v2(s_db, sql, -1, slot, NULL) != SQLITE_OK) {
        fprintf(stderr, "[db] prepare failed (%s): %s\n", sql, sqlite3_errmsg(s_db));
        *slot = NULL;
        return NULL;
    }

    if (s_stmt_slot_count < DB_STMT_SLOTS) {
        s_stmt_slots[s_stmt_slot_count++] = slot;
    } else {
        /* Statement still works; it just leaks until the process exits. */
        fprintf(stderr, "[db] statement slot registry full, raise DB_STMT_SLOTS\n");
    }

    return *slot;
}

static void stmt_finalize_all(void)
{
    for (int i = 0; i < s_stmt_slot_count; i++) {
        if (s_stmt_slots[i] && *s_stmt_slots[i]) {
            sqlite3_finalize(*s_stmt_slots[i]);
            *s_stmt_slots[i] = NULL;
        }
        s_stmt_slots[i] = NULL;
    }
    s_stmt_slot_count = 0;
}

/* ── integer-config cache ───────────────────────────────────────────────────
 * game_config is read far more often than written (the wheel rebuilds read
 * five weights every spin).  Values are cached on first read and refreshed by
 * db_set_config, which is the only writer in this process.                   */

#define DB_CFG_CACHE_MAX 48
#define DB_CFG_KEY_LEN   48

typedef struct {
    char key[DB_CFG_KEY_LEN];
    int  value;
} cfg_entry_t;

static cfg_entry_t s_cfg_cache[DB_CFG_CACHE_MAX];
static int         s_cfg_cache_count = 0;

static cfg_entry_t *cfg_cache_find(const char *key)
{
    for (int i = 0; i < s_cfg_cache_count; i++) {
        if (strncmp(s_cfg_cache[i].key, key, DB_CFG_KEY_LEN) == 0)
            return &s_cfg_cache[i];
    }
    return NULL;
}

static void cfg_cache_put(const char *key, int value)
{
    cfg_entry_t *e = cfg_cache_find(key);
    if (e) { e->value = value; return; }

    if (s_cfg_cache_count >= DB_CFG_CACHE_MAX) {
        fprintf(stderr, "[db] config cache full, raise DB_CFG_CACHE_MAX\n");
        return;
    }

    e = &s_cfg_cache[s_cfg_cache_count++];
    snprintf(e->key, sizeof(e->key), "%s", key);
    e->value = value;
}

static void cfg_cache_clear(void)
{
    s_cfg_cache_count = 0;
}

/* ── internal helpers ───────────────────────────────────────────────────── */

static int exec_sql(const char *sql)
{
    char *err = NULL;
    int rc = sqlite3_exec(s_db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] SQL error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
    }
    return rc;
}

/* Silently execute – used for idempotent migrations that may fail (e.g. ADD COLUMN) */
static void exec_sql_quiet(const char *sql)
{
    sqlite3_exec(s_db, sql, NULL, NULL, NULL);
}

/* Like exec_sql_quiet but reports the result, so a migration can tell the
 * difference between "already applied" and "applied just now". */
static int exec_sql_quiet_rc(const char *sql)
{
    return sqlite3_exec(s_db, sql, NULL, NULL, NULL);
}

static void fill_user(sqlite3_stmt *stmt, user_record_t *u)
{
    u->id             = sqlite3_column_int(stmt, 0);
    const char *n     = (const char *)sqlite3_column_text(stmt, 1);
    strncpy(u->name, n ? n : "", GAME_DB_NAME_LEN - 1);
    u->name[GAME_DB_NAME_LEN - 1] = '\0';
    u->is_admin       = sqlite3_column_int(stmt, 2) != 0;
    u->bonus          = sqlite3_column_int(stmt, 3);
    u->malus          = sqlite3_column_int(stmt, 4);
    u->wheel_trigger  = sqlite3_column_int(stmt, 5);
    u->total_cl       = sqlite3_column_int(stmt, 6);
    u->given_modifier = sqlite3_column_int(stmt, 7);
    u->given_to_id    = sqlite3_column_int(stmt, 8);
    u->last_spin_epoch = sqlite3_column_int64(stmt, 9);
    const char *tag = (const char *)sqlite3_column_text(stmt, 10);
    strncpy(u->rfid_tag, tag ? tag : "", sizeof(u->rfid_tag) - 1);
    u->rfid_tag[sizeof(u->rfid_tag) - 1] = '\0';
    u->spin_blocked_until = sqlite3_column_int64(stmt, 11);
}


/* Full statement text per query.  Spelling each one out instead of
 * concatenating a shared SELECT prefix at runtime keeps the statement text
 * constant, which is what the statement cache keys on. */
static const char k_sql_users_all[] =
    "SELECT id, name, is_admin, bonus, malus, wheel_trigger, total_cl,"
    "       given_modifier, given_to_id, last_spin_epoch, rfid_tag,"
    "       spin_blocked_until FROM users ORDER BY name;";
static const char k_sql_user_by_id[] =
    "SELECT id, name, is_admin, bonus, malus, wheel_trigger, total_cl,"
    "       given_modifier, given_to_id, last_spin_epoch, rfid_tag,"
    "       spin_blocked_until FROM users WHERE id = ?;";
static const char k_sql_user_by_rfid[] =
    "SELECT id, name, is_admin, bonus, malus, wheel_trigger, total_cl,"
    "       given_modifier, given_to_id, last_spin_epoch, rfid_tag,"
    "       spin_blocked_until FROM users WHERE rfid_tag = ?;";

/* ── public API ─────────────────────────────────────────────────────────── */

int db_init(const char *path)
{
    if (sqlite3_open(path, &s_db) != SQLITE_OK) {
        fprintf(stderr, "[db] Cannot open %s: %s\n", path, sqlite3_errmsg(s_db));
        return -1;
    }
    snprintf(s_db_path, sizeof(s_db_path), "%s", path);

    /* ON DELETE CASCADE on cl_history and cocktail_pumps is inert unless this
     * is enabled, and it is per-connection, not stored in the file. */
    exec_sql("PRAGMA foreign_keys = ON;");

    /* The machine is unplugged rather than shut down, so prefer the journal
     * mode that survives that without an fsync on every single write. */
    exec_sql_quiet("PRAGMA journal_mode = WAL;");
    exec_sql_quiet("PRAGMA synchronous = NORMAL;");

    exec_sql(
        "CREATE TABLE IF NOT EXISTS users ("
        "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name           TEXT    NOT NULL UNIQUE COLLATE NOCASE,"
        "  is_admin       INTEGER NOT NULL DEFAULT 0,"
        "  bonus          INTEGER NOT NULL DEFAULT 0,"
        "  malus          INTEGER NOT NULL DEFAULT 0,"
        "  wheel_trigger  INTEGER NOT NULL DEFAULT 0,"
        "  total_cl       INTEGER NOT NULL DEFAULT 0,"
        "  given_modifier INTEGER NOT NULL DEFAULT 0,"
        "  given_to_id    INTEGER NOT NULL DEFAULT -1"
        ");"
    );

    exec_sql(
        "CREATE TABLE IF NOT EXISTS cl_history ("
        "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id  INTEGER NOT NULL,"
        "  epoch    INTEGER NOT NULL,"
        "  total_cl INTEGER NOT NULL,"
        "  FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE"
        ");"
    );
    exec_sql(
        "CREATE INDEX IF NOT EXISTS idx_cl_history_user_epoch"
        " ON cl_history(user_id, epoch);"
    );

    exec_sql(
        "CREATE TABLE IF NOT EXISTS game_config ("
        "  key   TEXT    PRIMARY KEY,"
        "  value INTEGER NOT NULL DEFAULT 0"
        ");"
    );

    exec_sql(
        "CREATE TABLE IF NOT EXISTS game_text_config ("
        "  key   TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL DEFAULT ''"
        ");"
    );

    exec_sql(
        "CREATE TABLE IF NOT EXISTS cocktails ("
        "  id   INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT    NOT NULL UNIQUE COLLATE NOCASE"
        ");"
    );

    exec_sql(
        "CREATE TABLE IF NOT EXISTS cocktail_pumps ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  cocktail_id INTEGER NOT NULL,"
        "  pump_index  INTEGER NOT NULL,"
        "  mode        INTEGER NOT NULL DEFAULT 0,"
        "  value       INTEGER NOT NULL DEFAULT 0,"
        "  FOREIGN KEY(cocktail_id) REFERENCES cocktails(id) ON DELETE CASCADE"
        ");"
    );
    exec_sql(
        "CREATE INDEX IF NOT EXISTS idx_cocktail_pumps_cocktail"
        " ON cocktail_pumps(cocktail_id);"
    );

    /* Insert default config values (ignored if already present) */
    exec_sql(
        "INSERT OR IGNORE INTO game_config (key, value) VALUES"
        "  ('wheel_trigger_chance',            20),"
        "  ('bonus_wheel_bonus_weight',          1),"
        "  ('bonus_wheel_nothing_weight',        2),"
        "  ('bonus_wheel_malus_weight',          1),"
        "  ('bonus_wheel_timeout_add_weight',    1),"
        "  ('bonus_wheel_timeout_remove_weight', 1),"
        "  ('max_bonus_stack',                   5),"
        "  ('max_malus_stack',                   5),"
        "  ('spin_cooldown_seconds',             0),"
        "  ('timeout_modifier_minutes',          5),"
        "  ('cl_history_max_per_user',          500),"
        "  ('pump_count',                        2),"
        "  ('pump1_pulses_per_cl_x1000',    540420),"
        "  ('pump2_flow_clps_x1000',          2800);"
    );

    exec_sql(
        "INSERT OR IGNORE INTO game_text_config (key, value) VALUES"
        "  ('home_banner_text_1', 'L''abus d''eau est dangereux pour la sante.'),"
        "  ('home_banner_text_2', ''),"
        "  ('home_banner_text_3', '');"
    );

    /* Schema migration: add last_spin_epoch column if not present yet.
     * SQLite returns an error if the column already exists – we ignore it. */
    exec_sql_quiet(
        "ALTER TABLE users ADD COLUMN"
        " last_spin_epoch INTEGER NOT NULL DEFAULT 0;"
    );

    /* Schema migration: add rfid_tag column if not present yet. */
    exec_sql_quiet(
        "ALTER TABLE users ADD COLUMN rfid_tag TEXT;"
    );

    /* Schema migration: add is_admin column if not present yet. */
    exec_sql_quiet(
        "ALTER TABLE users ADD COLUMN is_admin INTEGER NOT NULL DEFAULT 0;"
    );

    /* Schema migration: split the two meanings that used to share
     * last_spin_epoch.  Older builds stored a *future* timestamp there to
     * express "blocked until", which made the column lie about when the user
     * actually spun.  When the column is created here for the first time,
     * carry any such pending block over so active timeouts survive the
     * upgrade, then clamp last_spin_epoch back to a real past value. */
    if (exec_sql_quiet_rc(
            "ALTER TABLE users ADD COLUMN"
            " spin_blocked_until INTEGER NOT NULL DEFAULT 0;") == SQLITE_OK) {
        exec_sql_quiet(
            "UPDATE users SET spin_blocked_until = last_spin_epoch"
            " WHERE last_spin_epoch > strftime('%s','now');"
        );
        exec_sql_quiet(
            "UPDATE users SET last_spin_epoch = strftime('%s','now')"
            " WHERE last_spin_epoch > strftime('%s','now');"
        );
    }

    return 0;
}

void db_close(void)
{
    stmt_finalize_all();
    cfg_cache_clear();
    if (s_db) { sqlite3_close(s_db); s_db = NULL; }
}

int db_get_all_users(user_record_t *buf, int max_count)
{
    if (!s_db || !buf || max_count <= 0) return 0;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st, k_sql_users_all);
    if (!stmt) return 0;

    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && n < max_count)
        fill_user(stmt, &buf[n++]);

    sqlite3_reset(stmt);
    return n;
}

int db_get_user(int id, user_record_t *out)
{
    if (!s_db || !out) return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st, k_sql_user_by_id);
    if (!stmt) return -1;
    sqlite3_bind_int(stmt, 1, id);

    int rc = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) { fill_user(stmt, out); rc = 0; }
    sqlite3_reset(stmt);
    return rc;
}

int db_add_user(const char *name)
{
    if (!s_db || !name || name[0] == '\0') return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st, "INSERT INTO users (name) VALUES (?);");
    if (!stmt) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    int new_id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE)
        new_id = (int)sqlite3_last_insert_rowid(s_db);
    sqlite3_reset(stmt);
    return new_id;
}

int db_update_user(const user_record_t *u)
{
    if (!s_db || !u) return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st,
        "UPDATE users SET name=?, is_admin=?, bonus=?, malus=?, wheel_trigger=?,"
        "                 total_cl=?, given_modifier=?, given_to_id=?,"
        "                 last_spin_epoch=?, spin_blocked_until=?, rfid_tag=?"
        " WHERE id=?;");
    if (!stmt) return -1;

    sqlite3_bind_text (stmt,  1, u->name, -1, SQLITE_STATIC);
    sqlite3_bind_int  (stmt,  2, u->is_admin ? 1 : 0);
    sqlite3_bind_int  (stmt,  3, u->bonus);
    sqlite3_bind_int  (stmt,  4, u->malus);
    sqlite3_bind_int  (stmt,  5, u->wheel_trigger);
    sqlite3_bind_int  (stmt,  6, u->total_cl);
    sqlite3_bind_int  (stmt,  7, u->given_modifier);
    sqlite3_bind_int  (stmt,  8, u->given_to_id);
    sqlite3_bind_int64(stmt,  9, u->last_spin_epoch);
    sqlite3_bind_int64(stmt, 10, u->spin_blocked_until);
    /* Store an unlinked tag as NULL: "" in a UNIQUE-ish lookup column would
     * make every tagless user match a db_get_user_by_rfid("") probe. */
    if (u->rfid_tag[0] != '\0')
        sqlite3_bind_text(stmt, 11, u->rfid_tag, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(stmt, 11);
    sqlite3_bind_int  (stmt, 12, u->id);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    if (rc != 0)
        fprintf(stderr, "[db] update user %d failed: %s\n", u->id, sqlite3_errmsg(s_db));
    sqlite3_reset(stmt);
    return rc;
}

int db_delete_user(int id)
{
    if (!s_db) return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st, "DELETE FROM users WHERE id=?;");
    if (!stmt) return -1;
    sqlite3_bind_int(stmt, 1, id);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_reset(stmt);
    return rc;
}

/* Build "<db stem>_archive_<timestamp>.db" from the open database's path. */
static int archive_path_make(char *out, size_t n)
{
    char stem[sizeof(s_db_path)];
    snprintf(stem, sizeof(stem), "%s", s_db_path);
    size_t len = strlen(stem);
    if (len > 3 && strcmp(stem + len - 3, ".db") == 0)
        stem[len - 3] = '\0';

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm_now);

    int w = snprintf(out, n, "%s_archive_%s.db", stem, ts);
    return (w > 0 && (size_t)w < n) ? 0 : -1;
}

int db_archive_and_reset_stats(char *archive_out, size_t archive_out_size)
{
    if (!s_db) return -1;

    char path[sizeof(s_db_path) + 32];
    if (archive_path_make(path, sizeof(path)) != 0) return -1;

    /* Never overwrite an earlier archive (two resets within one second). */
    FILE *probe = fopen(path, "r");
    if (probe) {
        fclose(probe);
        fprintf(stderr, "[db] archive %s already exists\n", path);
        return -1;
    }

    sqlite3_stmt *att = NULL;
    if (sqlite3_prepare_v2(s_db, "ATTACH DATABASE ? AS arc;", -1, &att, NULL) != SQLITE_OK) {
        fprintf(stderr, "[db] attach prepare failed: %s\n", sqlite3_errmsg(s_db));
        return -1;
    }
    sqlite3_bind_text(att, 1, path, -1, SQLITE_STATIC);
    int rc = sqlite3_step(att);
    sqlite3_finalize(att);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[db] cannot create archive %s: %s\n", path, sqlite3_errmsg(s_db));
        return -1;
    }

    /* Archive first and commit it on its own: the wipe below only runs once
     * the archive is safely on disk. */
    int ok = exec_sql(
        "BEGIN;"
        "CREATE TABLE arc.users       AS SELECT * FROM main.users;"
        "CREATE TABLE arc.cl_history  AS SELECT * FROM main.cl_history;"
        "COMMIT;") == SQLITE_OK;

    if (ok) {
        ok = exec_sql(
            "BEGIN;"
            "DELETE FROM main.cl_history;"
            "DELETE FROM main.users;"
            "DELETE FROM main.sqlite_sequence"
            " WHERE name IN ('users','cl_history');"
            "COMMIT;") == SQLITE_OK;
    }

    if (!ok) exec_sql_quiet("ROLLBACK;");
    exec_sql_quiet("DETACH DATABASE arc;");

    if (!ok) {
        /* An archive of a reset that never happened is only clutter. */
        remove(path);
        return -1;
    }

    if (archive_out && archive_out_size > 0)
        snprintf(archive_out, archive_out_size, "%s", path);
    return 0;
}

int db_prune_cl_history(int user_id, int keep)
{
    if (!s_db || keep <= 0) return 0;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st,
        "DELETE FROM cl_history WHERE user_id = ? AND id NOT IN ("
        "  SELECT id FROM cl_history WHERE user_id = ?"
        "   ORDER BY epoch DESC, id DESC LIMIT ?);");
    if (!stmt) return -1;

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_int(stmt, 3, keep);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_reset(stmt);
    return rc;
}

int db_add_cl_history(int user_id, int64_t epoch, int total_cl)
{
    if (!s_db) return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st,
            "INSERT INTO cl_history (user_id, epoch, total_cl) VALUES (?,?,?);");
    if (!stmt) return -1;
    sqlite3_bind_int  (stmt, 1, user_id);
    sqlite3_bind_int64(stmt, 2, epoch);
    sqlite3_bind_int  (stmt, 3, total_cl);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_reset(stmt);

    /* One row per spin would otherwise grow without bound for the lifetime of
     * the machine, and the leaderboard reads every user's history on open. */
    if (rc == 0)
        db_prune_cl_history(user_id, db_get_config("cl_history_max_per_user", 500));

    return rc;
}

int db_get_cl_history(int user_id, cl_history_point_t *buf, int max_count)
{
    if (!s_db || !buf || max_count <= 0) return 0;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st,
            "SELECT epoch, total_cl FROM cl_history"
            " WHERE user_id = ? ORDER BY epoch ASC;");
    if (!stmt) return 0;
    sqlite3_bind_int(stmt, 1, user_id);

    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && n < max_count) {
        buf[n].epoch    = sqlite3_column_int64(stmt, 0);
        buf[n].total_cl = sqlite3_column_int(stmt, 1);
        n++;
    }
    sqlite3_reset(stmt);
    return n;
}

int db_get_config(const char *key, int default_val)
{
    if (!s_db || !key) return default_val;

    cfg_entry_t *cached = cfg_cache_find(key);
    if (cached) return cached->value;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st, "SELECT value FROM game_config WHERE key=?;");
    if (!stmt) return default_val;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);

    int val = default_val;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        val = sqlite3_column_int(stmt, 0);
    sqlite3_reset(stmt);

    /* Cache the resolved value, default included: a key absent from the table
     * stays absent, so re-querying it on every call would buy nothing. */
    cfg_cache_put(key, val);
    return val;
}

int db_set_config(const char *key, int value)
{
    if (!s_db || !key) return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st,
        "INSERT OR REPLACE INTO game_config (key, value) VALUES (?,?);");
    if (!stmt) return -1;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, value);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_reset(stmt);

    if (rc == 0) cfg_cache_put(key, value);
    return rc;
}

int db_get_text_config(const char *key, char *out, int out_size,
                        const char *default_val)
{
    if (!out || out_size <= 0) return -1;

    out[0] = '\0';
    if (default_val) {
        strncpy(out, default_val, (size_t)(out_size - 1));
        out[out_size - 1] = '\0';
    }

    if (!s_db || !key) return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st,
            "SELECT value FROM game_text_config WHERE key=?;");
    if (!stmt) return -1;

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);

    int rc = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *val = (const char *)sqlite3_column_text(stmt, 0);
        strncpy(out, val ? val : "", (size_t)(out_size - 1));
        out[out_size - 1] = '\0';
        rc = 0;
    }

    sqlite3_reset(stmt);
    return rc;
}

int db_set_text_config(const char *key, const char *value)
{
    if (!s_db || !key || !value) return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st,
        "INSERT OR REPLACE INTO game_text_config (key, value) VALUES (?,?);");
    if (!stmt) return -1;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_reset(stmt);
    return rc;
}

int db_get_user_by_rfid(const char *tag, user_record_t *out)
{
    if (!s_db || !tag || tag[0] == '\0' || !out) return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st, k_sql_user_by_rfid);
    if (!stmt) return -1;
    sqlite3_bind_text(stmt, 1, tag, -1, SQLITE_STATIC);

    int rc = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) { fill_user(stmt, out); rc = 0; }
    sqlite3_reset(stmt);
    return rc;
}

int db_set_user_rfid(int user_id, const char *tag)
{
    if (!s_db) return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st,
            "UPDATE users SET rfid_tag = ? WHERE id = ?;");
    if (!stmt) return -1;

    if (tag && tag[0] != '\0')
        sqlite3_bind_text(stmt, 1, tag, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(stmt, 1);
    sqlite3_bind_int(stmt, 2, user_id);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_reset(stmt);
    return rc;
}

/* ── cocktails ─────────────────────────────────────────────────────────── */

int db_get_all_cocktails(cocktail_record_t *buf, int max_count)
{
    if (!s_db || !buf || max_count <= 0) return 0;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st,
            "SELECT id, name FROM cocktails ORDER BY name;");
    if (!stmt) return 0;

    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && n < max_count) {
        buf[n].id = sqlite3_column_int(stmt, 0);
        const char *nm = (const char *)sqlite3_column_text(stmt, 1);
        strncpy(buf[n].name, nm ? nm : "", GAME_DB_COCKTAIL_NAME_LEN - 1);
        buf[n].name[GAME_DB_COCKTAIL_NAME_LEN - 1] = '\0';
        n++;
    }

    sqlite3_reset(stmt);
    return n;
}

int db_add_cocktail(const char *name)
{
    if (!s_db || !name || name[0] == '\0') return -1;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st, "INSERT INTO cocktails (name) VALUES (?);");
    if (!stmt) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    int new_id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE)
        new_id = (int)sqlite3_last_insert_rowid(s_db);
    sqlite3_reset(stmt);
    return new_id;
}

int db_delete_cocktail(int id)
{
    if (!s_db) return -1;

    /* foreign_keys is ON, so the child rows cascade.  The explicit delete is
     * kept so an older database opened by this build still ends up clean. */
    static sqlite3_stmt *st_pumps = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st_pumps,
            "DELETE FROM cocktail_pumps WHERE cocktail_id=?;");
    if (!stmt) return -1;
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);

    static sqlite3_stmt *st_ck = NULL;
    stmt = stmt_prep(&st_ck, "DELETE FROM cocktails WHERE id=?;");
    if (!stmt) return -1;
    sqlite3_bind_int(stmt, 1, id);
    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_reset(stmt);
    return rc;
}

int db_get_cocktail_pumps(int cocktail_id, cocktail_pump_t *buf, int max_count)
{
    if (!s_db || !buf || max_count <= 0) return 0;

    static sqlite3_stmt *st = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st,
            "SELECT pump_index, mode, value FROM cocktail_pumps"
            " WHERE cocktail_id=? ORDER BY pump_index;");
    if (!stmt) return 0;
    sqlite3_bind_int(stmt, 1, cocktail_id);

    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && n < max_count) {
        buf[n].pump_index = sqlite3_column_int(stmt, 0);
        buf[n].mode       = sqlite3_column_int(stmt, 1);
        buf[n].value      = sqlite3_column_int(stmt, 2);
        n++;
    }

    sqlite3_reset(stmt);
    return n;
}

int db_set_cocktail_pumps(int cocktail_id, const cocktail_pump_t *pumps, int count)
{
    if (!s_db || (count > 0 && !pumps)) return -1;

    /* Replacing a recipe must not leave it half-written if one row fails. */
    exec_sql("BEGIN IMMEDIATE;");

    static sqlite3_stmt *st_del = NULL;
    sqlite3_stmt *stmt = stmt_prep(&st_del,
            "DELETE FROM cocktail_pumps WHERE cocktail_id=?;");
    if (!stmt) { exec_sql("ROLLBACK;"); return -1; }
    sqlite3_bind_int(stmt, 1, cocktail_id);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);

    static sqlite3_stmt *st_ins = NULL;
    for (int i = 0; i < count; i++) {
        stmt = stmt_prep(&st_ins,
                "INSERT INTO cocktail_pumps (cocktail_id, pump_index, mode, value)"
                " VALUES (?,?,?,?);");
        if (!stmt) { exec_sql("ROLLBACK;"); return -1; }
        sqlite3_bind_int(stmt, 1, cocktail_id);
        sqlite3_bind_int(stmt, 2, pumps[i].pump_index);
        sqlite3_bind_int(stmt, 3, pumps[i].mode);
        sqlite3_bind_int(stmt, 4, pumps[i].value);
        int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
        sqlite3_reset(stmt);
        if (rc != 0) { exec_sql("ROLLBACK;"); return -1; }
    }

    exec_sql("COMMIT;");
    return 0;
}
