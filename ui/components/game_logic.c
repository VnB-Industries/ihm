#include "game_logic.h"
#include "game_db.h"
#include <stdio.h>
#include <time.h>

/* No lvgl.h here on purpose - see the note in game_logic.h. */

/* ── session state ──────────────────────────────────────────────────────── */

static int             s_active_user_id  = -1;
static modifier_type_t s_pending_modifier = MODIFIER_NONE;
static int             s_selected_glass_cl = 33;
static int             s_selected_cocktail_id = -1;
static bool            s_modifier_announce = false;

void game_set_modifier_announce(void) { s_modifier_announce = true; }

bool game_take_modifier_announce(void)
{
    bool v = s_modifier_announce;
    s_modifier_announce = false;
    return v;
}

int             game_get_active_user(void)              { return s_active_user_id; }
void            game_set_active_user(int id)            { s_active_user_id = id; }
modifier_type_t game_get_pending_modifier(void)         { return s_pending_modifier; }
void            game_set_pending_modifier(modifier_type_t m) { s_pending_modifier = m; }
int             game_get_selected_glass_cl(void)        { return s_selected_glass_cl; }
int             game_get_selected_cocktail_id(void)     { return s_selected_cocktail_id; }
void            game_set_selected_cocktail_id(int id)   { s_selected_cocktail_id = id; }
void            game_set_selected_glass_cl(int glass_cl)
{
    if (glass_cl < 0) {
        glass_cl = 0;
    }
    s_selected_glass_cl = glass_cl;
}

/* ── random numbers ─────────────────────────────────────────────────────── */

/* xorshift32.  Seeded explicitly from main() so spins differ between boots;
 * a zero state would stick at zero forever, hence the guard in the seeder. */
static uint32_t s_rng_state = 0x1234ABCDu;

void game_rng_seed(uint32_t seed)
{
    s_rng_state = (seed != 0u) ? seed : 0x1234ABCDu;
}

static uint32_t rng_next(void)
{
    uint32_t x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng_state = x;
    return x;
}

uint32_t game_rand(uint32_t min, uint32_t max)
{
    if (max <= min) return min;

    uint64_t span = (uint64_t)max - (uint64_t)min + 1u;

    /* Reject the tail that does not divide evenly into 2^32, so each value in
     * the range is equally likely.  When span is a power of two the remainder
     * is 0, limit becomes 2^32 and nothing is ever rejected. */
    uint64_t limit = (UINT64_C(1) << 32) - ((UINT64_C(1) << 32) % span);

    uint32_t r;
    do {
        r = rng_next();
    } while ((uint64_t)r >= limit);

    return (uint32_t)(min + (r % span));
}

/* ── basic wheel ────────────────────────────────────────────────────────── */

static const uint32_t k_seg_colors[4] = {
    0xE74C3Cu, /* red    */
    0x2ECC71u, /* green  */
    0xF39C12u, /* orange */
    0x9B59B6u, /* purple */
};

static const int k_base_cl[4] = { 0, 2, 4, 6 };

/* Persistent label buffers – pointers remain valid for the app lifetime */
static char s_seg_labels[4][16];

void game_compute_wheel_segments(const user_record_t *u,
                                 game_segment_t *segs, uint8_t *count)
{
    int shift = (u->malus - u->bonus) * 2; /* net cL shift */

    for (int i = 0; i < 4; i++) {
        int val = k_base_cl[i] + shift;
        if (val < 0) val = 0;
        snprintf(s_seg_labels[i], sizeof(s_seg_labels[i]), "%dcL", val);
        segs[i].label = s_seg_labels[i];
        segs[i].image_src = NULL;
        segs[i].color_rgb = k_seg_colors[i];
        segs[i].value = (uint32_t)val;
    }
    *count = 4;
}

/* ── bonus wheel ────────────────────────────────────────────────────────── */

/* Labels are the fallback the wheel draws when a slice has no image, or when
 * its image fails to decode, so they are plain words rather than icon glyphs. */
static const char *k_bonus_text_bonus          = "Bonus";
static const char *k_bonus_text_nothing        = "Rien";
static const char *k_bonus_text_malus          = "Malus";
static const char *k_bonus_text_timeout_add    = "+Temps";
static const char *k_bonus_text_timeout_remove = "-Temps";
static const char *k_bonus_img_bonus           = "A:images/verreBonus.png";
static const char *k_bonus_img_malus           = "A:images/verreMalus.png";
static const char *k_bonus_img_timeout_add     = "A:images/timeMalus.png";
static const char *k_bonus_img_timeout_remove  = "A:images/timeBonus.png";

/* Append @p weight identical slices, stopping at the segment ceiling. */
static void push_bonus_slices(game_segment_t *segs, uint8_t *n, int weight,
                              const char *label, const char *image,
                              uint32_t color_rgb, uint32_t value)
{
    for (int i = 0; i < weight && *n < GAME_MAX_SEGMENTS; i++) {
        segs[*n].label     = label;
        segs[*n].image_src = image;
        segs[*n].color_rgb = color_rgb;
        segs[*n].value     = value;
        (*n)++;
    }
}

void game_compute_bonus_segments(game_segment_t *segs, uint8_t *count)
{
    int wb  = db_get_config("bonus_wheel_bonus_weight",          1);
    int wn  = db_get_config("bonus_wheel_nothing_weight",        2);
    int wm  = db_get_config("bonus_wheel_malus_weight",          1);
    int wta = db_get_config("bonus_wheel_timeout_add_weight",    1);
    int wtr = db_get_config("bonus_wheel_timeout_remove_weight", 1);

    if (wb  < 0) wb  = 0;
    if (wn  < 0) wn  = 0;
    if (wm  < 0) wm  = 0;
    if (wta < 0) wta = 0;
    if (wtr < 0) wtr = 0;

    uint8_t n = 0;
    push_bonus_slices(segs, &n, wb,  k_bonus_text_bonus,  k_bonus_img_bonus,
                      0x00C853u, BONUS_WHEEL_VAL_BONUS);
    push_bonus_slices(segs, &n, wn,  k_bonus_text_nothing, NULL,
                      0x616161u, BONUS_WHEEL_VAL_NOTHING);
    push_bonus_slices(segs, &n, wm,  k_bonus_text_malus,  k_bonus_img_malus,
                      0xD50000u, BONUS_WHEEL_VAL_MALUS);
    /* deep orange - add time = bad */
    push_bonus_slices(segs, &n, wta, k_bonus_text_timeout_add, k_bonus_img_timeout_add,
                      0xFF6D00u, BONUS_WHEEL_VAL_TIMEOUT_ADD);
    /* light blue - remove time = good */
    push_bonus_slices(segs, &n, wtr, k_bonus_text_timeout_remove, k_bonus_img_timeout_remove,
                      0x0091EAu, BONUS_WHEEL_VAL_TIMEOUT_REMOVE);

    /* Fallback: one of each if all weights are 0 */
    if (n == 0) {
        push_bonus_slices(segs, &n, 1, k_bonus_text_bonus,   k_bonus_img_bonus,
                          0x00C853u, BONUS_WHEEL_VAL_BONUS);
        push_bonus_slices(segs, &n, 1, k_bonus_text_nothing, NULL,
                          0x616161u, BONUS_WHEEL_VAL_NOTHING);
        push_bonus_slices(segs, &n, 1, k_bonus_text_malus,   k_bonus_img_malus,
                          0xD50000u, BONUS_WHEEL_VAL_MALUS);
    }
    *count = n;
}

/* ── spin cooldown ──────────────────────────────────────────────────────── */

int64_t game_spin_remaining_seconds(const user_record_t *u)
{
    if (!u) return 0;
    return u->spin_blocked_until - (int64_t)time(NULL);
}

/* ── spin result handlers ───────────────────────────────────────────────── */

void game_on_spin_started(int user_id)
{
    user_record_t u;
    if (db_get_user(user_id, &u) != 0) return;

    int64_t now = (int64_t)time(NULL);
    u.last_spin_epoch = now;

    int cooldown = db_get_config("spin_cooldown_seconds", 0);
    if (cooldown > 0) {
        int64_t until = now + cooldown;
        /* Never shorten a longer block already set by a +Temps modifier. */
        if (u.spin_blocked_until < until) u.spin_blocked_until = until;
    }

    db_update_user(&u);
}

bool game_on_dispense_complete(int user_id, int measured_cl)
{
    user_record_t u;
    if (db_get_user(user_id, &u) != 0) return false;

    if (measured_cl < 0) measured_cl = 0;
    u.total_cl += measured_cl;

    int chance = db_get_config("wheel_trigger_chance", 20);
    bool triggered = false;
    if ((int)game_rand(0, 99) < chance) {
        u.wheel_trigger++;
        triggered = true;
    }

    db_update_user(&u);
    /* Stamp the history with the real time of the pour.  Reusing
     * last_spin_epoch here used to be fine only because the two always
     * matched; they no longer have to. */
    db_add_cl_history(u.id, (int64_t)time(NULL), u.total_cl);
    return triggered;
}

modifier_type_t game_on_bonus_spin(int user_id, uint16_t seg_index,
                                    const game_segment_t *segs, uint8_t seg_count)
{
    if (seg_index >= seg_count) return MODIFIER_NONE;

    user_record_t u;
    if (db_get_user(user_id, &u) != 0) return MODIFIER_NONE;

    if (u.wheel_trigger > 0) {
        u.wheel_trigger--;
        db_update_user(&u);
    }

    uint32_t val = segs[seg_index].value;
    if (val == BONUS_WHEEL_VAL_BONUS)          return MODIFIER_BONUS;
    if (val == BONUS_WHEEL_VAL_MALUS)          return MODIFIER_MALUS;
    if (val == BONUS_WHEEL_VAL_TIMEOUT_ADD)    return MODIFIER_TIMEOUT_ADD;
    if (val == BONUS_WHEEL_VAL_TIMEOUT_REMOVE) return MODIFIER_TIMEOUT_REMOVE;
    return MODIFIER_NONE;
}

/* Apply @p type to @p rec in place.  Caller persists. */
static void apply_modifier_to(user_record_t *rec, modifier_type_t type,
                              int max_bonus, int max_malus, int timeout_s)
{
    int64_t now = (int64_t)time(NULL);

    switch (type) {
        case MODIFIER_BONUS:
            if (rec->bonus < max_bonus) rec->bonus++;
            break;

        case MODIFIER_MALUS:
            if (rec->malus < max_malus) rec->malus++;
            break;

        case MODIFIER_TIMEOUT_ADD: {
            /* Anchor to NOW so the block is always in the future regardless of
             * when the user last spun, and stack onto any existing block. */
            int64_t base = (rec->spin_blocked_until > now) ? rec->spin_blocked_until : now;
            rec->spin_blocked_until = base + timeout_s;
            break;
        }

        case MODIFIER_TIMEOUT_REMOVE:
            rec->spin_blocked_until -= timeout_s;
            if (rec->spin_blocked_until < 0) rec->spin_blocked_until = 0;
            break;

        case MODIFIER_NONE:
        default:
            break;
    }
}

void game_apply_modifier(int giver_id, int target_id, modifier_type_t type)
{
    if (type == MODIFIER_NONE) return;

    user_record_t giver;
    if (db_get_user(giver_id, &giver) != 0) return;

    giver.given_modifier = (int)type;
    giver.given_to_id    = target_id;

    int max_bonus = db_get_config("max_bonus_stack", 5);
    int max_malus = db_get_config("max_malus_stack", 5);
    int timeout_s = db_get_config("timeout_modifier_minutes", 5) * 60;

    /* Fake target (e.g. "Personne"): keep history on giver, apply to nobody. */
    if (target_id <= 0) {
        db_update_user(&giver);
        return;
    }

    if (giver_id == target_id) {
        apply_modifier_to(&giver, type, max_bonus, max_malus, timeout_s);
        db_update_user(&giver);
        return;
    }

    user_record_t target;
    if (db_get_user(target_id, &target) != 0) {
        db_update_user(&giver);
        return;
    }

    apply_modifier_to(&target, type, max_bonus, max_malus, timeout_s);
    db_update_user(&giver);
    db_update_user(&target);
}
