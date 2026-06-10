#include "scr_bonus_wheel.h"
#include "screen_manager.h"
#include "wheel_fortune.h"
#include "game_logic.h"
#include "game_db.h"

static lv_obj_t    *s_screen;
static lv_obj_t    *s_wheel;
static lv_obj_t    *s_user_label;
static lv_obj_t    *s_result_label;
static lv_obj_t    *s_spin_btn;
static lv_obj_t    *s_continue_btn;

static wf_segment_t s_segments[WHEEL_FORTUNE_MAX_SEGMENTS];
static uint8_t      s_seg_count = 0;

/* ── helpers ────────────────────────────────────────────────────────────── */

static void set_spin_enabled(bool en)
{
    if (en) lv_obj_clear_state(s_spin_btn, LV_STATE_DISABLED);
    else    lv_obj_add_state(s_spin_btn,   LV_STATE_DISABLED);
}

/* ── wheel callback ─────────────────────────────────────────────────────── */

static void on_wheel_result(lv_obj_t *wf, uint16_t seg_index,
                             const wf_segment_t *seg)
{
    (void)wf; (void)seg;

    int uid = game_get_active_user();
    modifier_type_t mod = game_on_bonus_spin(uid, seg_index,
                                              s_segments, s_seg_count);
    game_set_pending_modifier(mod);

    switch (mod) {
        case MODIFIER_BONUS:
            lv_label_set_text(s_result_label,
                LV_SYMBOL_OK "  BONUS obtenu !\nChoisissez qui en profite.");
            lv_obj_set_style_text_color(s_result_label,
                lv_color_hex(0x00C853), LV_PART_MAIN);
            break;
        case MODIFIER_MALUS:
            lv_label_set_text(s_result_label,
                LV_SYMBOL_WARNING "  MALUS obtenu !\nChoisissez qui l'ecope.");
            lv_obj_set_style_text_color(s_result_label,
                lv_color_hex(0xD50000), LV_PART_MAIN);
            break;
        case MODIFIER_TIMEOUT_ADD: {
            int mins = db_get_config("timeout_modifier_minutes", 5);
            char tbuf[64];
            lv_snprintf(tbuf, sizeof(tbuf),
                LV_SYMBOL_PAUSE "  +%d min !\nChoisissez qui attend.", mins);
            lv_label_set_text(s_result_label, tbuf);
            lv_obj_set_style_text_color(s_result_label,
                lv_color_hex(0xFF6D00), LV_PART_MAIN);
            break;
        }
        case MODIFIER_TIMEOUT_REMOVE: {
            int mins = db_get_config("timeout_modifier_minutes", 5);
            char tbuf[64];
            lv_snprintf(tbuf, sizeof(tbuf),
                LV_SYMBOL_PLAY "  -%d min !\nChoisissez qui profite.", mins);
            lv_label_set_text(s_result_label, tbuf);
            lv_obj_set_style_text_color(s_result_label,
                lv_color_hex(0x0091EA), LV_PART_MAIN);
            break;
        }
        default:
            lv_label_set_text(s_result_label, "Rien cette fois...");
            lv_obj_set_style_text_color(s_result_label,
                lv_color_hex(0x888888), LV_PART_MAIN);
            break;
    }

    lv_obj_clear_flag(s_continue_btn, LV_OBJ_FLAG_HIDDEN);
}

/* ── event callbacks ────────────────────────────────────────────────────── */

static void on_spin_clicked(lv_event_t *e)
{
    (void)e;
    if (wheel_fortune_is_spinning(s_wheel) || s_seg_count == 0) return;

    lv_label_set_text(s_result_label, "...");
    lv_obj_set_style_text_color(s_result_label,
        lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_add_flag(s_continue_btn, LV_OBJ_FLAG_HIDDEN);
    set_spin_enabled(false);

    uint16_t result = (uint16_t)(lv_rand(0, 0xFFFF) % s_seg_count);
    wheel_fortune_spin(s_wheel, 3000, result);
}

static void on_continue_clicked(lv_event_t *e)
{
    (void)e;
    modifier_type_t mod = game_get_pending_modifier();
    if (mod != MODIFIER_NONE)
        screen_manager_load(SCREEN_GIVE_MODIFIER);
    else
        screen_manager_load(SCREEN_WHEEL);
}

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_bonus_wheel_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* Title */
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, LV_SYMBOL_SHUFFLE "  Roue Bonus !");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    /* Wheel — left side, radius 160 */
    s_wheel = wheel_fortune_create(s_screen, 160);
    lv_obj_align(s_wheel, LV_ALIGN_LEFT_MID, 20, 10);
    wheel_fortune_set_result_cb(s_wheel, on_wheel_result);

    /* User label */
    s_user_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_user_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(s_user_label, LV_ALIGN_TOP_RIGHT, -30, 16);

    /* Result label — right side */
    s_result_label = lv_label_create(s_screen);
    lv_label_set_text(s_result_label, "Appuyez sur Tourner !");
    lv_obj_set_style_text_color(s_result_label, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_result_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(s_result_label, 300);
    lv_label_set_long_mode(s_result_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_result_label, LV_ALIGN_RIGHT_MID, -80, -80);

    /* Spin button */
    s_spin_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_spin_btn, 200, 60);
    lv_obj_align(s_spin_btn, LV_ALIGN_RIGHT_MID, -80, 10);
    lv_obj_set_style_bg_color(s_spin_btn, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_add_event_cb(s_spin_btn, on_spin_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_spin = lv_label_create(s_spin_btn);
    lv_label_set_text(lbl_spin, LV_SYMBOL_REFRESH "  Tourner !");
    lv_obj_set_style_text_color(lbl_spin, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_center(lbl_spin);

    /* Continue button — hidden until spin done */
    s_continue_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_continue_btn, 200, 60);
    lv_obj_align(s_continue_btn, LV_ALIGN_RIGHT_MID, -80, 90);
    lv_obj_set_style_bg_color(s_continue_btn, lv_color_hex(0xE94560), LV_PART_MAIN);
    lv_obj_add_event_cb(s_continue_btn, on_continue_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cont = lv_label_create(s_continue_btn);
    lv_label_set_text(lbl_cont, "Continuer " LV_SYMBOL_RIGHT);
    lv_obj_center(lbl_cont);
    lv_obj_add_flag(s_continue_btn, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *scr_bonus_wheel_get(void) { return s_screen; }

void scr_bonus_wheel_refresh(void)
{
    /* Update user label */
    int uid = game_get_active_user();
    user_record_t u;
    if (db_get_user(uid, &u) == 0)
        lv_label_set_text(s_user_label, u.name);

    /* Rebuild segments from current config */
    game_compute_bonus_segments(s_segments, &s_seg_count);
    wheel_fortune_set_segments(s_wheel, s_segments, s_seg_count);

    lv_label_set_text(s_result_label, "Appuyez sur Tourner !");
    lv_obj_set_style_text_color(s_result_label, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_add_flag(s_continue_btn, LV_OBJ_FLAG_HIDDEN);
    set_spin_enabled(true);
}
