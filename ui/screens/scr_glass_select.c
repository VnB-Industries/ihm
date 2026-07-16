#include "scr_glass_select.h"
#include "screen_manager.h"
#include "game_logic.h"
#include "game_db.h"
#include <stdint.h>

static lv_obj_t *s_screen;
static lv_obj_t *s_user_label;

static void on_glass_clicked(lv_event_t *e)
{
    int glass_cl = (int)(intptr_t)lv_event_get_user_data(e);
    game_set_selected_glass_cl(glass_cl);
    screen_manager_load(SCREEN_WHEEL);
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    screen_manager_load(SCREEN_PROFILE_SELECT);
}

static lv_obj_t *create_glass_button(lv_obj_t *parent, const char *text, int glass_cl)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 220, 110);
    lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_add_event_cb(btn, on_glass_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)glass_cl);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(lbl);

    return btn;
}

void scr_glass_select_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, LV_SYMBOL_DRIVE "  Choisissez un verre");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    s_user_label = lv_label_create(s_screen);
    lv_label_set_text(s_user_label, "");
    lv_obj_set_style_text_color(s_user_label, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_align(s_user_label, LV_ALIGN_TOP_MID, 0, 56);

    lv_obj_t *hint = lv_label_create(s_screen);
    lv_label_set_text(hint, "Le volume roue va dans Pump1.\nPump2 complete le verre.");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 90);

    lv_obj_t *cont = lv_obj_create(s_screen);
    lv_obj_set_size(cont, SCREEN_INNER_W, 260);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(cont, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_glass_button(cont, "22 cL", 22);
    create_glass_button(cont, "33 cL", 33);
    create_glass_button(cont, "50 cL", 50);

    lv_obj_t *btn_back = lv_btn_create(s_screen);
    lv_obj_set_size(btn_back, 130, 40);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT "  Retour");
    lv_obj_center(lbl_back);
}

lv_obj_t *scr_glass_select_get(void)
{
    return s_screen;
}

void scr_glass_select_refresh(void)
{
    int uid = game_get_active_user();
    user_record_t u;
    if (db_get_user(uid, &u) == 0) {
        char text[96];
        lv_snprintf(text, sizeof(text), "%s: choisissez la taille du verre", u.name);
        lv_label_set_text(s_user_label, text);
    } else {
        lv_label_set_text(s_user_label, "Choisissez la taille du verre");
    }
}
