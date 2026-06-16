#include "scr_home.h"
#include "screen_manager.h"
#include "game_db.h"
#include <stdio.h>
#include <string.h>
#if LV_USE_GIF
#include <libgen.h>
#include <unistd.h>
#endif

static lv_obj_t *s_screen;
static lv_obj_t *s_title_label;
static lv_obj_t *s_btn_col;
static lv_obj_t *s_title_img_left;
static lv_obj_t *s_title_img_right;
static lv_obj_t *s_side_img_left;
static lv_obj_t *s_side_img_right;
static lv_obj_t *s_ricassou_gif;
static lv_obj_t *s_ricassou_right_gif;
static lv_obj_t *s_banner_bg;
static lv_obj_t *s_banner_label;
static lv_timer_t *s_banner_timer;
static lv_timer_t *s_banner_start_timer;
static lv_timer_t *s_ricassou_timer;
static bool s_home_images_ready = false;

static char s_banner_msgs[3][192];
static uint8_t s_banner_msg_count = 0;
static uint8_t s_banner_msg_index = 0;

static const char *k_banner_default =
    "L'abus d'eau est dangereux pour la sante.";

/* ── helpers ────────────────────────────────────────────────────────────── */

#if LV_USE_GIF
static const char *resolve_ricassou_path(void)
{
    static char resolved[512];
    char exe[512];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        exe[len] = '\0';
        char *dir = dirname(exe);
        char local_path[512];
        lv_snprintf(local_path, sizeof(local_path), "%s/images/ricassouLeft.gif", dir);
        FILE *f = fopen(local_path, "rb");
        if (f) {
            fclose(f);
            lv_snprintf(resolved, sizeof(resolved), "A:%s", local_path);
            return resolved;
        }
    }

    static const char *candidates[] = {
        "images/ricassouLeft.gif",
        "../images/ricassouLeft.gif",
    };

    for (uint32_t i = 0; i < (uint32_t)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) {
            fclose(f);
            lv_snprintf(resolved, sizeof(resolved), "A:%s", candidates[i]);
            return resolved;
        }
    }

    return NULL;
}

static const char *resolve_ricassou_right_path(void)
{
    static char resolved[512];
    char exe[512];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        exe[len] = '\0';
        char *dir = dirname(exe);
        char local_path[512];
        lv_snprintf(local_path, sizeof(local_path), "%s/images/ricassouRight.gif", dir);
        FILE *f = fopen(local_path, "rb");
        if (f) {
            fclose(f);
            lv_snprintf(resolved, sizeof(resolved), "A:%s", local_path);
            return resolved;
        }
    }

    static const char *candidates[] = {
        "images/ricassouRight.gif",
        "../images/ricassouRight.gif",
    };

    for (uint32_t i = 0; i < (uint32_t)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) {
            fclose(f);
            lv_snprintf(resolved, sizeof(resolved), "A:%s", candidates[i]);
            return resolved;
        }
    }

    return NULL;
}
#endif

static lv_obj_t *make_main_btn(lv_obj_t *parent, const char *text,
                                lv_color_t color, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 260, 80);
    lv_obj_set_style_bg_color(btn, color, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(lbl);
    return btn;
}

/* ── event callbacks ────────────────────────────────────────────────────── */

static void on_start_clicked(lv_event_t *e)
{
    (void)e;
    screen_manager_load(SCREEN_PROFILE_SELECT);
}

static void on_leaderboard_clicked(lv_event_t *e)
{
    (void)e;
    screen_manager_load(SCREEN_LEADERBOARD);
}

static void on_parameters_clicked(lv_event_t *e)
{
    (void)e;
    screen_manager_load(SCREEN_PARAMETERS);
}

static void ricassou_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_ricassou_gif && lv_gif_is_loaded(s_ricassou_gif)) {
        lv_gif_restart(s_ricassou_gif);
        lv_gif_set_loop_count(s_ricassou_gif, 1);
    }

    if (s_ricassou_right_gif && lv_gif_is_loaded(s_ricassou_right_gif)) {
        lv_gif_restart(s_ricassou_right_gif);
        lv_gif_set_loop_count(s_ricassou_right_gif, 1);
    }
}

/* ── banner helpers ────────────────────────────────────────────────────── */

static void banner_load_messages(void)
{
    char b1[192];
    char b2[192];
    char b3[192];

    db_get_text_config("home_banner_text_1", b1, sizeof(b1), k_banner_default);
    db_get_text_config("home_banner_text_2", b2, sizeof(b2), "");
    db_get_text_config("home_banner_text_3", b3, sizeof(b3), "");

    s_banner_msg_count = 0;

    if (b1[0] != '\0') {
        lv_snprintf(s_banner_msgs[s_banner_msg_count], sizeof(s_banner_msgs[s_banner_msg_count]), "%s", b1);
        s_banner_msg_count++;
    }
    if (b2[0] != '\0' && s_banner_msg_count < 3) {
        lv_snprintf(s_banner_msgs[s_banner_msg_count], sizeof(s_banner_msgs[s_banner_msg_count]), "%s", b2);
        s_banner_msg_count++;
    }
    if (b3[0] != '\0' && s_banner_msg_count < 3) {
        lv_snprintf(s_banner_msgs[s_banner_msg_count], sizeof(s_banner_msgs[s_banner_msg_count]), "%s", b3);
        s_banner_msg_count++;
    }

    if (s_banner_msg_count == 0) {
        lv_snprintf(s_banner_msgs[0], sizeof(s_banner_msgs[0]), "%s", k_banner_default);
        s_banner_msg_count = 1;
    }

    s_banner_msg_index = 0;
}

static void banner_show_next(void)
{
    if (!s_banner_label || !s_banner_bg || s_banner_msg_count == 0) return;

    const char *msg = s_banner_msgs[s_banner_msg_index];
    s_banner_msg_index = (uint8_t)((s_banner_msg_index + 1) % s_banner_msg_count);

    lv_label_set_text(s_banner_label, msg);
    lv_obj_update_layout(s_banner_label);

    lv_coord_t text_w = lv_obj_get_width(s_banner_label);
    lv_coord_t text_h = lv_obj_get_height(s_banner_label);
    lv_coord_t banner_h = lv_obj_get_height(s_banner_bg);
    lv_coord_t y = (lv_coord_t)((banner_h - text_h) / 2);

    lv_obj_set_y(s_banner_label, y);

    lv_coord_t start_x = SCREEN_W;
    lv_coord_t end_x = (lv_coord_t)(-text_w);
    lv_obj_set_x(s_banner_label, start_x);

    lv_anim_delete(s_banner_label, NULL);

    int32_t travel = (int32_t)start_x - (int32_t)end_x;
    uint32_t duration = (uint32_t)(travel * 14);
    if (duration < 5000U) duration = 5000U;
    if (duration > 22000U) duration = 22000U;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_banner_label);
    lv_anim_set_values(&a, start_x, end_x);
    lv_anim_set_time(&a, duration);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);

    if (s_banner_timer) {
        lv_timer_set_period(s_banner_timer, duration + 900U);
    }
}

static void banner_timer_cb(lv_timer_t *t)
{
    (void)t;
    banner_show_next();
}

static void banner_start_after_render_cb(lv_timer_t *t)
{
    (void)t;

    if (!s_home_images_ready) {
        if (s_title_img_left) lv_obj_add_flag(s_title_img_left, LV_OBJ_FLAG_HIDDEN);
        if (s_side_img_left) lv_obj_add_flag(s_side_img_left, LV_OBJ_FLAG_HIDDEN);
        if (s_title_img_right) lv_obj_add_flag(s_title_img_right, LV_OBJ_FLAG_HIDDEN);
        if (s_side_img_right) lv_obj_add_flag(s_side_img_right, LV_OBJ_FLAG_HIDDEN);

        if (s_ricassou_gif) {
            const char *gif_src = resolve_ricassou_path();
            if (gif_src) {
                lv_gif_set_src(s_ricassou_gif, gif_src);
                lv_obj_align(s_ricassou_gif, LV_ALIGN_LEFT_MID, -40, 20);
                if (lv_gif_is_loaded(s_ricassou_gif)) {
                    lv_gif_set_loop_count(s_ricassou_gif, 1);
                    lv_gif_pause(s_ricassou_gif);
                }
            }
        }

        if (s_ricassou_right_gif) {
            const char *gif_src = resolve_ricassou_right_path();
            if (gif_src) {
                lv_gif_set_src(s_ricassou_right_gif, gif_src);
                lv_obj_align(s_ricassou_right_gif, LV_ALIGN_RIGHT_MID, 40, 20);
                if (lv_gif_is_loaded(s_ricassou_right_gif)) {
                    lv_gif_set_loop_count(s_ricassou_right_gif, 1);
                    lv_gif_pause(s_ricassou_right_gif);
                }
            }
        }

        s_home_images_ready = true;
    }

    banner_load_messages();

    banner_show_next();
    if (s_banner_timer) {
        lv_timer_resume(s_banner_timer);
    }
    if (s_ricassou_timer) {
        lv_timer_resume(s_ricassou_timer);
        lv_timer_reset(s_ricassou_timer);
    }

    /* Play GIFs immediately when entering home, then continue with periodic timer. */
    ricassou_timer_cb(NULL);

    if (s_banner_start_timer) {
        lv_timer_delete(s_banner_start_timer);
        s_banner_start_timer = NULL;
    }
}

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_home_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);

    /* Title */
    s_title_label = lv_label_create(s_screen);
    lv_label_set_text(s_title_label, "Ricardo");
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_48, LV_PART_MAIN);

    s_title_img_left = lv_image_create(s_screen);
    lv_obj_align_to(s_title_img_left, s_title_label, LV_ALIGN_OUT_LEFT_MID, -20, 0);

    s_title_img_right = lv_image_create(s_screen);
    lv_obj_align_to(s_title_img_right, s_title_label, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    s_ricassou_gif = lv_gif_create(s_screen);
    lv_obj_align(s_ricassou_gif, LV_ALIGN_LEFT_MID, -40, 20);
    lv_obj_move_background(s_ricassou_gif);

    s_ricassou_right_gif = lv_gif_create(s_screen);
    lv_obj_align(s_ricassou_right_gif, LV_ALIGN_RIGHT_MID, 40, 20);
    lv_obj_move_background(s_ricassou_right_gif);

    /* Subtitle */
    lv_obj_t *sub = lv_label_create(s_screen);
    lv_label_set_text(sub, "Une roue, du Ricard.");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 106);

    /* Button container – centered column */
    s_btn_col = lv_obj_create(s_screen);
    lv_obj_set_size(s_btn_col, 300, 390);
    lv_obj_align(s_btn_col, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_opa(s_btn_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_btn_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_btn_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_btn_col, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_btn_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_btn_col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_btn_col, LV_OBJ_FLAG_SCROLLABLE);

    /* Decorative side images around the button column */
    s_side_img_left = lv_image_create(s_screen);
    lv_obj_align_to(s_side_img_left, s_btn_col, LV_ALIGN_OUT_LEFT_MID, -80, 0);

    s_side_img_right = lv_image_create(s_screen);
    lv_obj_align_to(s_side_img_right, s_btn_col, LV_ALIGN_OUT_RIGHT_MID, 80, 0);

    make_main_btn(s_btn_col, LV_SYMBOL_PLAY "  Jouer",
                  lv_color_hex(0xE94560), on_start_clicked);
    make_main_btn(s_btn_col, LV_SYMBOL_LIST "  Classement",
                  lv_color_hex(0x0F3460), on_leaderboard_clicked);
    make_main_btn(s_btn_col, LV_SYMBOL_SETTINGS "  Parametres",
                  lv_color_hex(0x444444), on_parameters_clicked);

    /* Bottom scrolling banner */
    s_banner_bg = lv_obj_create(s_screen);
    lv_obj_set_size(s_banner_bg, SCREEN_W, 42);
    lv_obj_align(s_banner_bg, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_banner_bg, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_banner_bg, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_banner_bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_banner_bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_banner_bg, LV_OBJ_FLAG_SCROLLABLE);

    s_banner_label = lv_label_create(s_banner_bg);
    lv_obj_set_style_text_color(s_banner_label, lv_color_hex(0xE5E7EB), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_banner_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(s_banner_label, SCREEN_W, 10);

    s_banner_timer = lv_timer_create(banner_timer_cb, 7000, NULL);
    lv_timer_pause(s_banner_timer);

    s_ricassou_timer = lv_timer_create(ricassou_timer_cb, 11000, NULL);
    lv_timer_pause(s_ricassou_timer);
}

void scr_home_refresh(void)
{
    if (s_banner_timer) {
        lv_timer_pause(s_banner_timer);
    }
    if (s_ricassou_timer) {
        lv_timer_pause(s_ricassou_timer);
    }

    if (s_ricassou_gif) {
        lv_gif_pause(s_ricassou_gif);
    }
    if (s_ricassou_right_gif) {
        lv_gif_pause(s_ricassou_right_gif);
    }

    lv_anim_delete(s_banner_label, NULL);

    if (s_banner_label) {
        lv_label_set_text(s_banner_label, "");
        lv_obj_set_x(s_banner_label, SCREEN_W);
    }

    if (s_banner_start_timer) {
        lv_timer_delete(s_banner_start_timer);
        s_banner_start_timer = NULL;
    }

    /* Delay all banner work until screen transition has rendered. */
    s_banner_start_timer = lv_timer_create(banner_start_after_render_cb, 320, NULL);
}

lv_obj_t *scr_home_get(void)
{
    return s_screen;
}
