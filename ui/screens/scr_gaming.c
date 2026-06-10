#include "scr_gaming.h"
#include "screen_manager.h"
#include "wheel_fortune.h"
#include "game_logic.h"
#include "game_db.h"
#include <time.h>

static lv_obj_t    *s_screen;
static lv_obj_t    *s_wheel;
static lv_obj_t    *s_user_label;
static lv_obj_t    *s_result_label;
static lv_obj_t    *s_cooldown_label;
static lv_obj_t    *s_spin_btn;
static lv_obj_t    *s_bonus_btn;
static lv_timer_t  *s_cooldown_timer = NULL;

static wf_segment_t s_segments[WHEEL_FORTUNE_MAX_SEGMENTS];
static uint8_t      s_seg_count = 0;

/* ── helpers ────────────────────────────────────────────────────────────── */

static void set_spin_btn_enabled(bool en)
{
    if (en) lv_obj_clear_state(s_spin_btn, LV_STATE_DISABLED);
    else    lv_obj_add_state(s_spin_btn,   LV_STATE_DISABLED);
}

static void cooldown_timer_cb(lv_timer_t *t)
{
    (void)t;
    user_record_t u;
    if (db_get_user(game_get_active_user(), &u) != 0) return;

    int cooldown = db_get_config("spin_cooldown_seconds", 0);
    int64_t elapsed  = (int64_t)time(NULL) - u.last_spin_epoch;
    int64_t remaining = (int64_t)cooldown - elapsed;

    if (remaining <= 0) {
        lv_label_set_text(s_cooldown_label, "");
        lv_obj_add_flag(s_cooldown_label, LV_OBJ_FLAG_HIDDEN);
        set_spin_btn_enabled(true);
        lv_timer_delete(s_cooldown_timer);
        s_cooldown_timer = NULL;
    } else {
        char buf[32];
        lv_snprintf(buf, sizeof(buf), LV_SYMBOL_PAUSE "  Attendre %ds",
                    (int)remaining);
        lv_label_set_text(s_cooldown_label, buf);
    }
}

/* Check cooldown state on screen entry or after a spin.
 * Disables the spin button and starts the 1-s ticker if still cooling down. */
static void update_cooldown_state(void)
{
    /* NOTE: no early-exit for spin_cooldown==0 — a TIMEOUT_ADD modifier sets
     * last_spin_epoch to a future value, so remaining = spin_cd - elapsed will
     * be positive even when spin_cd = 0 (elapsed becomes negative). */
    user_record_t u;
    if (db_get_user(game_get_active_user(), &u) != 0) return;

    int cooldown = db_get_config("spin_cooldown_seconds", 0);
    int64_t elapsed   = (int64_t)time(NULL) - u.last_spin_epoch;
    int64_t remaining = (int64_t)cooldown - elapsed;

    if (remaining <= 0) {
        lv_obj_add_flag(s_cooldown_label, LV_OBJ_FLAG_HIDDEN);
        set_spin_btn_enabled(true);
        if (s_cooldown_timer) {
            lv_timer_delete(s_cooldown_timer);
            s_cooldown_timer = NULL;
        }
    } else {
        char buf[32];
        lv_snprintf(buf, sizeof(buf), LV_SYMBOL_PAUSE "  Attendre %ds",
                    (int)remaining);
        lv_label_set_text(s_cooldown_label, buf);
        lv_obj_clear_flag(s_cooldown_label, LV_OBJ_FLAG_HIDDEN);
        set_spin_btn_enabled(false);

        if (!s_cooldown_timer)
            s_cooldown_timer = lv_timer_create(cooldown_timer_cb, 1000, NULL);
    }
}

/* ── wheel callback ─────────────────────────────────────────────────────── */

static void on_wheel_result(lv_obj_t *wf, uint16_t seg_index,
                             const wf_segment_t *seg)
{
    (void)wf; (void)seg_index;

    int cl = (int)seg->value;
    char buf[48];
    lv_snprintf(buf, sizeof(buf),
                cl == 0 ? "Chance ! 0 cL"
                        : "Tu bois %d cL !", cl);
    lv_label_set_text(s_result_label, buf);

    bool bonus_triggered = game_on_basic_spin(game_get_active_user(), cl);
    if (bonus_triggered) {
        lv_obj_clear_flag(s_bonus_btn, LV_OBJ_FLAG_HIDDEN);
        /* Cooldown still applies after bonus wheel; will be checked on SCREEN_WHEEL re-entry */
    } else {
        update_cooldown_state();
    }
}

/* ── event callbacks ────────────────────────────────────────────────────── */

static void on_spin_clicked(lv_event_t *e)
{
    (void)e;
    if (wheel_fortune_is_spinning(s_wheel) || s_seg_count == 0) return;

    lv_label_set_text(s_result_label, "...");
    lv_obj_add_flag(s_bonus_btn, LV_OBJ_FLAG_HIDDEN);
    set_spin_btn_enabled(false);

    uint16_t result = (uint16_t)(lv_rand(0, 0xFFFF) % s_seg_count);
    wheel_fortune_spin(s_wheel, 3500, result);
}

static void on_bonus_clicked(lv_event_t *e)
{
    (void)e;
    screen_manager_load(SCREEN_BONUS_WHEEL);
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    screen_manager_load(SCREEN_PROFILE_SELECT);
}

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_gaming_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* Title */
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "Roue de la Fortune");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    /* Wheel — left side, radius 180 (fits 480px height comfortably) */
    s_wheel = wheel_fortune_create(s_screen, 180);
    lv_obj_align(s_wheel, LV_ALIGN_LEFT_MID, 16, 10);
    wheel_fortune_set_result_cb(s_wheel, on_wheel_result);

    /* User name + modifier info — top right */
    s_user_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_user_label, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_user_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_width(s_user_label, 340);
    lv_label_set_long_mode(s_user_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_user_label, LV_ALIGN_TOP_RIGHT, -20, 16);

    /* Result label — right center */
    s_result_label = lv_label_create(s_screen);
    lv_label_set_text(s_result_label, "Appuyez sur Tourner !");
    lv_obj_set_style_text_color(s_result_label, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_result_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(s_result_label, 320);
    lv_label_set_long_mode(s_result_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_result_label, LV_ALIGN_RIGHT_MID, -60, -70);

    /* Cooldown label — hidden until a cooldown is active */
    s_cooldown_label = lv_label_create(s_screen);
    lv_label_set_text(s_cooldown_label, "");
    lv_obj_set_style_text_color(s_cooldown_label, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_cooldown_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_cooldown_label, LV_ALIGN_RIGHT_MID, -70, -130);
    lv_obj_add_flag(s_cooldown_label, LV_OBJ_FLAG_HIDDEN);

    /* Spin button */
    s_spin_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_spin_btn, 220, 64);
    lv_obj_align(s_spin_btn, LV_ALIGN_RIGHT_MID, -70, 20);
    lv_obj_set_style_bg_color(s_spin_btn, lv_color_hex(0xE94560), LV_PART_MAIN);
    lv_obj_set_style_radius(s_spin_btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(s_spin_btn, on_spin_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_spin = lv_label_create(s_spin_btn);
    lv_label_set_text(lbl_spin, LV_SYMBOL_REFRESH "  Tourner !");
    lv_obj_center(lbl_spin);

    /* Bonus wheel button — hidden until triggered */
    s_bonus_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_bonus_btn, 220, 64);
    lv_obj_align(s_bonus_btn, LV_ALIGN_RIGHT_MID, -70, 105);
    lv_obj_set_style_bg_color(s_bonus_btn, lv_color_hex(0x00C853), LV_PART_MAIN);
    lv_obj_set_style_radius(s_bonus_btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(s_bonus_btn, on_bonus_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_bonus = lv_label_create(s_bonus_btn);
    lv_label_set_text(lbl_bonus, LV_SYMBOL_SHUFFLE "  Roue Bonus !");
    lv_obj_center(lbl_bonus);
    lv_obj_add_flag(s_bonus_btn, LV_OBJ_FLAG_HIDDEN);

    /* Back button */
    lv_obj_t *btn_back = lv_btn_create(s_screen);
    lv_obj_set_size(btn_back, 160, 46);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT "  Retour");
    lv_obj_center(lbl_back);
}

lv_obj_t *scr_gaming_get(void) { return s_screen; }

void scr_gaming_refresh(void)
{
    int uid = game_get_active_user();
    user_record_t u;
    if (db_get_user(uid, &u) != 0) return;

    /* Update user info label */
    char buf[80];
    lv_snprintf(buf, sizeof(buf), "%s\nBonus +%d  Malus +%d",
                u.name, u.bonus, u.malus);
    lv_label_set_text(s_user_label, buf);

    /* Recompute wheel segments */
    game_compute_wheel_segments(&u, s_segments, &s_seg_count);
    wheel_fortune_set_segments(s_wheel, s_segments, s_seg_count);

    /* Show confirmation if a modifier was just applied by this user */
    if (u.given_modifier != (int)MODIFIER_NONE && u.given_to_id >= 0) {
        user_record_t tgt;
        const char *tname = "?";
        if (db_get_user(u.given_to_id, &tgt) == 0) tname = tgt.name;
        const char *mstr;
        switch ((modifier_type_t)u.given_modifier) {
            case MODIFIER_BONUS:          mstr = "Bonus";   break;
            case MODIFIER_MALUS:          mstr = "Malus";   break;
            case MODIFIER_TIMEOUT_ADD:    mstr = "+Temps";  break;
            case MODIFIER_TIMEOUT_REMOVE: mstr = "-Temps";  break;
            default:                      mstr = "?";        break;
        }
        char mbuf[80];
        lv_snprintf(mbuf, sizeof(mbuf), "%s donne a %s !", mstr, tname);
        lv_label_set_text(s_result_label, mbuf);
    } else {
        lv_label_set_text(s_result_label, "Appuyez sur Tourner !");
    }
    lv_obj_add_flag(s_bonus_btn, LV_OBJ_FLAG_HIDDEN);

    /* Kill any leftover cooldown timer from a previous session */
    if (s_cooldown_timer) {
        lv_timer_delete(s_cooldown_timer);
        s_cooldown_timer = NULL;
    }
    update_cooldown_state();
}
