#include "scr_cocktail_select.h"
#include "screen_manager.h"
#include "game_logic.h"
#include "game_db.h"
#include <stdint.h>

static lv_obj_t *s_screen;
static lv_obj_t *s_user_label;
static lv_obj_t *s_list;

static void on_cocktail_clicked(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    game_set_selected_cocktail_id(id);
    screen_manager_load(SCREEN_WHEEL);
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    screen_manager_load(SCREEN_GLASS_SELECT);
}

void scr_cocktail_select_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, LV_SYMBOL_LIST "  Choisissez un cocktail");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    s_user_label = lv_label_create(s_screen);
    lv_label_set_text(s_user_label, "");
    lv_obj_set_style_text_color(s_user_label, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_align(s_user_label, LV_ALIGN_TOP_MID, 0, 56);

    s_list = lv_obj_create(s_screen);
    lv_obj_set_size(s_list, SCREEN_INNER_W, SCREEN_H - 160);
    lv_obj_align(s_list, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_list, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

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

lv_obj_t *scr_cocktail_select_get(void)
{
    return s_screen;
}

void scr_cocktail_select_refresh(void)
{
    int uid = game_get_active_user();
    user_record_t u;
    if (db_get_user(uid, &u) == 0) {
        char text[96];
        lv_snprintf(text, sizeof(text), "%s: choisissez un cocktail", u.name);
        lv_label_set_text(s_user_label, text);
    } else {
        lv_label_set_text(s_user_label, "Choisissez un cocktail");
    }

    lv_obj_clean(s_list);

    cocktail_record_t cks[GAME_DB_MAX_COCKTAILS];
    int count = db_get_all_cocktails(cks, GAME_DB_MAX_COCKTAILS);

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty,
            "Aucun cocktail configure.\nAjoutez-en dans Parametres > Cocktails.");
        lv_obj_set_style_text_color(empty, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }

    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_btn_create(s_list);
        lv_obj_set_size(btn, 220, 110);
        lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0F3460), LV_PART_MAIN);
        lv_obj_add_event_cb(btn, on_cocktail_clicked, LV_EVENT_CLICKED,
                            (void *)(intptr_t)cks[i].id);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, cks[i].name);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, 196);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_center(lbl);
    }
}
