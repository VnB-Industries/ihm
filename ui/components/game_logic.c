#include "game_logic.h"
#include "game_db.h"
#include "lvgl/lvgl.h"

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
        segs[i].color = k_seg_colors[i];
        segs[i].value = (uint32_t)val;
    }
    *count = 4;
}

/* ── bonus wheel ────────────────────────────────────────────────────────── */

void game_compute_bonus_segments(wf_segment_t *segs, uint8_t *count)
{
    int wb = db_get_config("bonus_wheel_bonus_weight",   1);
    int wn = db_get_config("bonus_wheel_nothing_weight", 2);
    int wm = db_get_config("bonus_wheel_malus_weight",   1);

    if (wb < 0) wb = 0;
    if (wn < 0) wn = 0;
    if (wm < 0) wm = 0;

    uint8_t n = 0;
    for (int i = 0; i < wb && n < WHEEL_FORTUNE_MAX_SEGMENTS; i++, n++) {
        segs[n].label = "BONUS";
        segs[n].color = lv_color_hex(0x00C853);
        segs[n].value = BONUS_WHEEL_VAL_BONUS;
    }
    for (int i = 0; i < wn && n < WHEEL_FORTUNE_MAX_SEGMENTS; i++, n++) {
        segs[n].label = "RIEN";
        segs[n].color = lv_color_hex(0x616161);
        segs[n].value = BONUS_WHEEL_VAL_NOTHING;
    }
    for (int i = 0; i < wm && n < WHEEL_FORTUNE_MAX_SEGMENTS; i++, n++) {
        segs[n].label = "MALUS";
        segs[n].color = lv_color_hex(0xD50000);
        segs[n].value = BONUS_WHEEL_VAL_MALUS;
    }

    /* Fallback: one of each if all weights are 0 */
    if (n == 0) {
        segs[0].label = "BONUS"; segs[0].color = lv_color_hex(0x00C853); segs[0].value = BONUS_WHEEL_VAL_BONUS;
        segs[1].label = "RIEN";  segs[1].color = lv_color_hex(0x616161); segs[1].value = BONUS_WHEEL_VAL_NOTHING;
        segs[2].label = "MALUS"; segs[2].color = lv_color_hex(0xD50000); segs[2].value = BONUS_WHEEL_VAL_MALUS;
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

    int chance = db_get_config("wheel_trigger_chance", 20);
    bool triggered = false;
    if ((int)lv_rand(0, 99) < chance) {
        u.wheel_trigger++;
        triggered = true;
    }

    db_update_user(&u);
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
    if (val == BONUS_WHEEL_VAL_BONUS) return MODIFIER_BONUS;
    if (val == BONUS_WHEEL_VAL_MALUS) return MODIFIER_MALUS;
    return MODIFIER_NONE;
}

void game_apply_modifier(int giver_id, int target_id, modifier_type_t type)
{
    if (type == MODIFIER_NONE) return;

    user_record_t giver, target;
    if (db_get_user(giver_id, &giver) != 0) return;
    if (db_get_user(target_id, &target) != 0) return;

    giver.given_modifier = (int)type;  /* +1 = gave bonus, -1 = gave malus */
    giver.given_to_id    = target_id;

    if (type == MODIFIER_BONUS) target.bonus++;
    else                        target.malus++;

    db_update_user(&giver);
    db_update_user(&target);
}
