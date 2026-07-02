#include "game_logic.h"
#include "game_db.h"
#include "lvgl/lvgl.h"
#include <time.h>

/* ── session state ──────────────────────────────────────────────────────── */

static int             s_active_user_id  = -1;
static modifier_type_t s_pending_modifier = MODIFIER_NONE;

int             game_get_active_user(void)              { return s_active_user_id; }
void            game_set_active_user(int id)            { s_active_user_id = id; }
modifier_type_t game_get_pending_modifier(void)         { return s_pending_modifier; }
void            game_set_pending_modifier(modifier_type_t m) { s_pending_modifier = m; }

/* ── basic wheel ────────────────────────────────────────────────────────── */

static const lv_color_t k_seg_colors[4] = {
    { .blue = 0x3C, .green = 0x4C, .red = 0xE7 }, /* blue-violet */
    { .blue = 0x71, .green = 0xCC, .red = 0x2E }, /* green       */
    { .blue = 0x12, .green = 0x9C, .red = 0xF3 }, /* orange      */
    { .blue = 0xB6, .green = 0x59, .red = 0x9B }, /* purple      */
};

static const int k_base_cl[4] = { 0, 2, 4, 6 };

/* Persistent label buffers – pointers remain valid for the app lifetime */
static char s_seg_labels[4][8];

void game_compute_wheel_segments(const user_record_t *u,
                                 wf_segment_t *segs, uint8_t *count)
{
    int shift = (u->malus - u->bonus) * 2; /* net cL shift */

    for (int i = 0; i < 4; i++) {
        int val = k_base_cl[i] + shift;
        if (val < 0) val = 0;
        lv_snprintf(s_seg_labels[i], sizeof(s_seg_labels[i]),
                    "%dcL", val);
        segs[i].label = s_seg_labels[i];
        segs[i].image_src = NULL;
        segs[i].color = k_seg_colors[i];
        segs[i].value = (uint32_t)val;
    }
    *count = 4;
}

/* ── bonus wheel ────────────────────────────────────────────────────────── */

static const char *k_bonus_icon_bonus          = LV_SYMBOL_OK;
static const char *k_bonus_icon_nothing        = LV_SYMBOL_CLOSE;
static const char *k_bonus_icon_malus          = LV_SYMBOL_WARNING;
static const char *k_bonus_icon_timeout_add    = LV_SYMBOL_PAUSE;
static const char *k_bonus_icon_timeout_remove = LV_SYMBOL_PLAY;
static const char *k_bonus_img_bonus           = "A:images/verreBonus.png";
static const char *k_bonus_img_malus           = "A:images/verreMalus.png";
static const char *k_bonus_img_timeout_add     = "A:images/timeMalus.png";
static const char *k_bonus_img_timeout_remove  = "A:images/timeBonus.png";

void game_compute_bonus_segments(wf_segment_t *segs, uint8_t *count)
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
    for (int i = 0; i < wb && n < WHEEL_FORTUNE_MAX_SEGMENTS; i++, n++) {
        segs[n].label = k_bonus_icon_bonus;
        segs[n].image_src = k_bonus_img_bonus;
        segs[n].color = lv_color_hex(0x00C853);
        segs[n].value = BONUS_WHEEL_VAL_BONUS;
    }
    for (int i = 0; i < wn && n < WHEEL_FORTUNE_MAX_SEGMENTS; i++, n++) {
        segs[n].label = k_bonus_icon_nothing;
        segs[n].image_src = NULL;
        segs[n].color = lv_color_hex(0x616161);
        segs[n].value = BONUS_WHEEL_VAL_NOTHING;
    }
    for (int i = 0; i < wm && n < WHEEL_FORTUNE_MAX_SEGMENTS; i++, n++) {
        segs[n].label = k_bonus_icon_malus;
        segs[n].image_src = k_bonus_img_malus;
        segs[n].color = lv_color_hex(0xD50000);
        segs[n].value = BONUS_WHEEL_VAL_MALUS;
    }
    for (int i = 0; i < wta && n < WHEEL_FORTUNE_MAX_SEGMENTS; i++, n++) {
        segs[n].label = k_bonus_icon_timeout_add;
        segs[n].image_src = k_bonus_img_timeout_add;
        segs[n].color = lv_color_hex(0xFF6D00); /* deep orange - add time = bad */
        segs[n].value = BONUS_WHEEL_VAL_TIMEOUT_ADD;
    }
    for (int i = 0; i < wtr && n < WHEEL_FORTUNE_MAX_SEGMENTS; i++, n++) {
        segs[n].label = k_bonus_icon_timeout_remove;
        segs[n].image_src = k_bonus_img_timeout_remove;
        segs[n].color = lv_color_hex(0x0091EA); /* light blue - remove time = good */
        segs[n].value = BONUS_WHEEL_VAL_TIMEOUT_REMOVE;
    }

    /* Fallback: one of each if all weights are 0 */
    if (n == 0) {
        segs[0].label = k_bonus_icon_bonus;   segs[0].image_src = k_bonus_img_bonus; segs[0].color = lv_color_hex(0x00C853); segs[0].value = BONUS_WHEEL_VAL_BONUS;
        segs[1].label = k_bonus_icon_nothing; segs[1].image_src = NULL;              segs[1].color = lv_color_hex(0x616161); segs[1].value = BONUS_WHEEL_VAL_NOTHING;
        segs[2].label = k_bonus_icon_malus;   segs[2].image_src = k_bonus_img_malus; segs[2].color = lv_color_hex(0xD50000); segs[2].value = BONUS_WHEEL_VAL_MALUS;
        n = 3;
    }
    *count = n;
}

/* ── spin result handlers ───────────────────────────────────────────────── */

bool game_on_basic_spin(int user_id, int cl_value)
{
    user_record_t u;
    if (db_get_user(user_id, &u) != 0) return false;

    u.total_cl += cl_value;
    u.last_spin_epoch = (int64_t)time(NULL);

    int chance = db_get_config("wheel_trigger_chance", 20);
    bool triggered = false;
    if ((int)lv_rand(0, 99) < chance) {
        u.wheel_trigger++;
        triggered = true;
    }

    db_update_user(&u);
    db_add_cl_history(u.id, u.last_spin_epoch, u.total_cl);
    return triggered;
}

modifier_type_t game_on_bonus_spin(int user_id, uint16_t seg_index,
                                    const wf_segment_t *segs, uint8_t seg_count)
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

    /* Helper lambda-equivalent: apply effect to a user_record_t */
#define APPLY_MOD(rec) \
    do { \
        if      (type == MODIFIER_BONUS          && (rec).bonus < max_bonus) (rec).bonus++; \
        else if (type == MODIFIER_MALUS          && (rec).malus < max_malus) (rec).malus++; \
        else if (type == MODIFIER_TIMEOUT_ADD) { \
            /* Anchor to NOW so the timeout is always in the future, regardless \
             * of when the user last spun.  Stack with existing timeout. */ \
            int64_t _now = (int64_t)time(NULL); \
            int64_t _base = (rec).last_spin_epoch > _now ? (rec).last_spin_epoch : _now; \
            (rec).last_spin_epoch = _base + timeout_s; \
        } \
        else if (type == MODIFIER_TIMEOUT_REMOVE) { \
            (rec).last_spin_epoch -= timeout_s; \
            if ((rec).last_spin_epoch < 0) (rec).last_spin_epoch = 0; \
        } \
    } while (0)

    if (giver_id == target_id) {
        APPLY_MOD(giver);
        db_update_user(&giver);
    } else {
        user_record_t target;
        if (db_get_user(target_id, &target) != 0) { db_update_user(&giver); return; }
        APPLY_MOD(target);
        db_update_user(&giver);
        db_update_user(&target);
    }
#undef APPLY_MOD
}