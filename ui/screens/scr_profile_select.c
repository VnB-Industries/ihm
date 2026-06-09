#include "scr_profile_select.h"
#include "screen_manager.h"
#include "game_db.h"
#include "game_logic.h"

static lv_obj_t *s_screen;
static lv_obj_t *s_list_cont;

/* ── event callbacks ────────────────────────────────────────────────────── */

static void on_user_clicked(lv_event_t *e)
{
    int uid = (int)(intptr_t)lv_event_get_user_data(e);
    game_set_active_user(uid);
    screen_manager_load(SCREEN_WHEEL);
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    screen_manager_load(SCREEN_HOME);
}

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_profile_select_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* Title */
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, LV_SYMBOL_PLAY "  Choisissez un joueur");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    /* Scrollable tile container */
    s_list_cont = lv_obj_create(s_screen);
    lv_obj_set_size(s_list_cont, 760, 370);
    lv_obj_align(s_list_cont, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_bg_color(s_list_cont, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_list_cont, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list_cont, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_list_cont, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_list_cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_list_cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_list_cont, LV_DIR_VER);

    /* Back button */
    lv_obj_t *btn_back = lv_btn_create(s_screen);
    lv_obj_set_size(btn_back, 160, 46);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -20, -8);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT "  Retour");
    lv_obj_center(lbl_back);
}

lv_obj_t *scr_profile_select_get(void) { return s_screen; }

void scr_profile_select_refresh(void)
{
    lv_obj_clean(s_list_cont);

    user_record_t users[GAME_DB_MAX_USERS];
    int count = db_get_all_users(users, GAME_DB_MAX_USERS);

    if (count == 0) {
        lv_obj_t *hint = lv_label_create(s_list_cont);
        lv_label_set_text(hint,
            "Aucun joueur.\nAjoutez-en dans Paramètres " LV_SYMBOL_SETTINGS ".");
        lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(hint, 500);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        return;
    }

    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_btn_create(s_list_cont);
        lv_obj_set_size(btn, 210, 80);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0F3460), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 8, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, on_user_clicked, LV_EVENT_CLICKED,
                            (void *)(intptr_t)users[i].id);

        /* Name */
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, users[i].name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 4);

        /* Stats sub-label */
        char info[40];
        lv_snprintf(info, sizeof(info), "%d cL | B+%d M+%d",
                    users[i].total_cl, users[i].bonus, users[i].malus);
        lv_obj_t *sub = lv_label_create(btn);
        lv_label_set_text(sub, info);
        lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
}
