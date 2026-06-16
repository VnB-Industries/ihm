#include "scr_bonus_wheel.h"
#include "screen_manager.h"
#include "wheel_fortune.h"
#include "game_logic.h"
#include "game_db.h"
#include <stdio.h>
#include <string.h>
#if LV_USE_LOTTIE
#include <libgen.h>
#include <unistd.h>
#endif

static lv_obj_t    *s_screen;
static lv_obj_t    *s_wheel;
static lv_obj_t    *s_user_label;
static lv_obj_t    *s_result_label;
static lv_obj_t    *s_spin_btn;
static lv_obj_t    *s_continue_btn;
#if LV_USE_LOTTIE
static lv_obj_t    *s_confetti = NULL;
static uint32_t     s_confetti_buf[700 * 700];
static lv_timer_t  *s_confetti_timer = NULL;
#endif

static wf_segment_t s_segments[WHEEL_FORTUNE_MAX_SEGMENTS];
static uint8_t      s_seg_count = 0;

/* ── helpers ────────────────────────────────────────────────────────────── */

static void set_spin_enabled(bool en)
{
    if (en) lv_obj_clear_state(s_spin_btn, LV_STATE_DISABLED);
    else    lv_obj_add_state(s_spin_btn,   LV_STATE_DISABLED);
}

#if LV_USE_LOTTIE
static const char *resolve_confetti_path(void)
{
    static char resolved[512];
    char exe[512];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        exe[len] = '\0';
        char *dir = dirname(exe);
        snprintf(resolved, sizeof(resolved), "%s/images/Confetti.json", dir);
        FILE *f = fopen(resolved, "rb");
        if (f) { fclose(f); return resolved; }
    }
    static const char *candidates[] = { "images/Confetti.json", "../images/Confetti.json" };
    for (uint32_t i = 0; i < (uint32_t)(sizeof(candidates)/sizeof(candidates[0])); i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) { fclose(f); return candidates[i]; }
    }
    return NULL;
}

static void confetti_preload(lv_obj_t *screen)
{
    const char *src = resolve_confetti_path();
    if (src == NULL || s_confetti != NULL) return;
    s_confetti = lv_lottie_create(screen);
    lv_lottie_set_buffer(s_confetti, 700, 700, s_confetti_buf);
    lv_lottie_set_src_file(s_confetti, src);
    lv_obj_align(s_confetti, LV_ALIGN_CENTER, -170, 0);
    lv_obj_add_flag(s_confetti, LV_OBJ_FLAG_HIDDEN);
    /* Leave repeat_count as LV_ANIM_REPEAT_INFINITE (LVGL's default). */
}

static void confetti_hide_cb(lv_timer_t *t)
{
    (void)t;
    if (s_confetti) lv_obj_add_flag(s_confetti, LV_OBJ_FLAG_HIDDEN);
    s_confetti_timer = NULL;
}

static void play_confetti_once(void)
{
    if (s_confetti == NULL) return;

    lv_anim_t *anim = lv_lottie_get_anim(s_confetti);
    if (anim == NULL) return;

    if (s_confetti_timer) {
        lv_timer_delete(s_confetti_timer);
        s_confetti_timer = NULL;
    }

    anim->act_time = 0;
    lv_obj_move_foreground(s_confetti);
    lv_obj_clear_flag(s_confetti, LV_OBJ_FLAG_HIDDEN);
    s_confetti_timer = lv_timer_create(confetti_hide_cb, (uint32_t)anim->duration, NULL);
    lv_timer_set_repeat_count(s_confetti_timer, 1);
}
#endif

/* ── wheel callback ─────────────────────────────────────────────────────── */

static void on_wheel_result(lv_obj_t *wf, uint16_t seg_index,
                             const wf_segment_t *seg)
{
    (void)wf; (void)seg;

#if LV_USE_LOTTIE
    play_confetti_once();
#endif

    int uid = game_get_active_user();
    modifier_type_t mod = game_on_bonus_spin(uid, seg_index,
                                              s_segments, s_seg_count);
    game_set_pending_modifier(mod);

    switch (mod) {
        case MODIFIER_BONUS:
            lv_label_set_text(s_result_label,
                LV_SYMBOL_OK "  BONUS obtenu !\n-2cL sur toute la roue\nChoisissez qui en profite.");
            lv_obj_set_style_text_color(s_result_label,
                lv_color_hex(0x00C853), LV_PART_MAIN);
            break;
        case MODIFIER_MALUS:
            lv_label_set_text(s_result_label,
                LV_SYMBOL_WARNING "  MALUS obtenu !\n+2cL sur toute la roue.\nChoisissez qui l'ecope.");
            lv_obj_set_style_text_color(s_result_label,
                lv_color_hex(0xD50000), LV_PART_MAIN);
            break;
        case MODIFIER_TIMEOUT_ADD: {
            int mins = db_get_config("timeout_modifier_minutes", 5);
            char tbuf[64];
            lv_snprintf(tbuf, sizeof(tbuf),
                LV_SYMBOL_WARNING "  +%d min !\nChoisissez qui attend.", mins);
            lv_label_set_text(s_result_label, tbuf);
            lv_obj_set_style_text_color(s_result_label,
                lv_color_hex(0xFF6D00), LV_PART_MAIN);
            break;
        }
        case MODIFIER_TIMEOUT_REMOVE: {
            int mins = db_get_config("timeout_modifier_minutes", 5);
            char tbuf[64];
            lv_snprintf(tbuf, sizeof(tbuf),
                LV_SYMBOL_OK "  -%d min !\nChoisissez qui profite.", mins);
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
    if (lv_obj_has_state(s_spin_btn, LV_STATE_DISABLED)) return;
    if (wheel_fortune_is_spinning(s_wheel) || s_seg_count == 0) return;

    lv_label_set_text(s_result_label, "...");
    lv_obj_set_style_text_color(s_result_label,
        lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_add_flag(s_continue_btn, LV_OBJ_FLAG_HIDDEN);
    set_spin_enabled(false);

    uint16_t result = (uint16_t)(lv_rand(0, 0xFFFF) % s_seg_count);
    wheel_fortune_spin(s_wheel, 3000, result);
}

static void on_wheel_swipe_spin(lv_obj_t *wf, uint16_t swipe_strength)
{
    if (lv_obj_has_state(s_spin_btn, LV_STATE_DISABLED)) return;
    if (wheel_fortune_is_spinning(s_wheel) || s_seg_count == 0) return;

    uint16_t result = (uint16_t)(lv_rand(0, 0xFFFF) % s_seg_count);
    uint32_t clamped_strength = LV_MIN((uint32_t)swipe_strength, 144U);
    uint16_t full_turns = (uint16_t)(2U + clamped_strength / 16U);
    uint32_t duration_ms = 3800U + clamped_strength * 12U;

    lv_label_set_text(s_result_label, "...");
    lv_obj_set_style_text_color(s_result_label,
        lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_add_flag(s_continue_btn, LV_OBJ_FLAG_HIDDEN);
    set_spin_enabled(false);
    wheel_fortune_spin_ex(wf, duration_ms, result, full_turns);
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
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* Title */
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, LV_SYMBOL_SHUFFLE "  Roue Bonus !");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    /* Wheel — left side, same layout as the main wheel screen */
    s_wheel = wheel_fortune_create(s_screen, 160);
    lv_obj_align(s_wheel, LV_ALIGN_LEFT_MID, 200, 10);
    wheel_fortune_set_result_cb(s_wheel, on_wheel_result);
    wheel_fortune_set_spin_request_cb(s_wheel, on_wheel_swipe_spin);

    /* User label — top right */
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

    /* Spin button */
    s_spin_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_spin_btn, 220, 64);
    lv_obj_align(s_spin_btn, LV_ALIGN_RIGHT_MID, -70, 20);
    lv_obj_set_style_bg_color(s_spin_btn, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_set_style_radius(s_spin_btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(s_spin_btn, on_spin_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_spin = lv_label_create(s_spin_btn);
    lv_label_set_text(lbl_spin, LV_SYMBOL_REFRESH "  Tourner !");
    lv_obj_set_style_text_color(lbl_spin, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_center(lbl_spin);

    /* Continue button — hidden until spin done */
    s_continue_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_continue_btn, 220, 64);
    lv_obj_align(s_continue_btn, LV_ALIGN_RIGHT_MID, -70, 105);
    lv_obj_set_style_bg_color(s_continue_btn, lv_color_hex(0xE94560), LV_PART_MAIN);
    lv_obj_set_style_radius(s_continue_btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(s_continue_btn, on_continue_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cont = lv_label_create(s_continue_btn);
    lv_label_set_text(lbl_cont, "Continuer " LV_SYMBOL_RIGHT);
    lv_obj_center(lbl_cont);
    lv_obj_add_flag(s_continue_btn, LV_OBJ_FLAG_HIDDEN);

#if LV_USE_LOTTIE
    confetti_preload(s_screen);
#endif
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

#if LV_USE_LOTTIE
    if (s_confetti) {
        lv_obj_add_flag(s_confetti, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}
