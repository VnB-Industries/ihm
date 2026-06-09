#include "scr_home.h"
#include "screen_manager.h"

static lv_obj_t *s_screen;

/* ── helpers ────────────────────────────────────────────────────────────── */

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

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_home_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* Title */
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "Ricardo");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    /* Subtitle */
    lv_obj_t *sub = lv_label_create(s_screen);
    lv_label_set_text(sub, "Roue de la Fortune");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 106);

    /* Button container – centered column */
    lv_obj_t *col = lv_obj_create(s_screen);
    lv_obj_set_size(col, 300, 310);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(col, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    make_main_btn(col, LV_SYMBOL_PLAY "  Jouer",
                  lv_color_hex(0xE94560), on_start_clicked);
    make_main_btn(col, LV_SYMBOL_LIST "  Classement",
                  lv_color_hex(0x0F3460), on_leaderboard_clicked);
    make_main_btn(col, LV_SYMBOL_SETTINGS "  Paramètres",
                  lv_color_hex(0x444444), on_parameters_clicked);
}

lv_obj_t *scr_home_get(void)
{
    return s_screen;
}
