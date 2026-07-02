#include "scr_profile_select.h"
#include "screen_manager.h"
#include "game_db.h"
#include "game_logic.h"

static lv_obj_t *s_screen;
static lv_obj_t *s_list_cont;
static lv_obj_t *s_add_user_modal;
static lv_obj_t *s_new_user_ta;
static lv_obj_t *s_kb;

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
    if(!lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
    if(!lv_obj_has_flag(s_add_user_modal, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_add_user_modal, LV_OBJ_FLAG_HIDDEN);
    }
    screen_manager_load(SCREEN_HOME);
}

static void on_add_box_clicked(lv_event_t *e)
{
    (void)e;
    lv_textarea_set_text(s_new_user_ta, "");
    lv_obj_clear_flag(s_add_user_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_add_user_modal);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, s_new_user_ta);
    lv_obj_move_foreground(s_kb);
    lv_obj_add_state(s_new_user_ta, LV_STATE_FOCUSED);
}

static void on_add_user_cancelled(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_add_user_modal, LV_OBJ_FLAG_HIDDEN);
}

static void on_add_user_clicked(lv_event_t *e)
{
    (void)e;

    const char *name = lv_textarea_get_text(s_new_user_ta);
    if(name == NULL || name[0] == '\0') {
        return;
    }

    db_add_user(name);
    lv_textarea_set_text(s_new_user_ta, "");
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_add_user_modal, LV_OBJ_FLAG_HIDDEN);
    scr_profile_select_refresh();
}

static void on_ta_focused(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_kb, ta);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_kb);
}

static void on_kb_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        if(code == LV_EVENT_CANCEL) {
            lv_obj_add_flag(s_add_user_modal, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_profile_select_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* Title */
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, LV_SYMBOL_PLAY "  Choisissez un joueur");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    /* Scrollable tile container */
    s_list_cont = lv_obj_create(s_screen);
    lv_obj_set_size(s_list_cont, SCREEN_INNER_W, SCREEN_H - 110);
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
    lv_obj_set_size(btn_back, 130, 40);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn_back, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn_back, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT "  Retour");
    lv_obj_center(lbl_back);

    s_kb = lv_keyboard_create(s_screen);
    lv_obj_set_size(s_kb, SCREEN_W, 220);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, on_kb_event, LV_EVENT_ALL, NULL);

    s_add_user_modal = lv_obj_create(s_screen);
    lv_obj_set_size(s_add_user_modal, 420, 170);
    lv_obj_align(s_add_user_modal, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_bg_color(s_add_user_modal, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_add_user_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_add_user_modal, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_add_user_modal, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_add_user_modal, 12, LV_PART_MAIN);
    lv_obj_clear_flag(s_add_user_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_add_user_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *modal_title = lv_label_create(s_add_user_modal);
    lv_label_set_text(modal_title, LV_SYMBOL_PLUS "  Ajouter un joueur");
    lv_obj_set_style_text_color(modal_title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(modal_title, LV_ALIGN_TOP_MID, 0, 0);

    s_new_user_ta = lv_textarea_create(s_add_user_modal);
    lv_obj_set_size(s_new_user_ta, 388, 46);
    lv_obj_align(s_new_user_ta, LV_ALIGN_TOP_MID, 0, 38);
    lv_textarea_set_one_line(s_new_user_ta, true);
    lv_textarea_set_placeholder_text(s_new_user_ta, "Nom du joueur...");
    lv_obj_add_event_cb(s_new_user_ta, on_ta_focused, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *btn_cancel = lv_btn_create(s_add_user_modal);
    lv_obj_set_size(btn_cancel, 150, 42);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_cancel, on_add_user_cancelled, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, LV_SYMBOL_CLOSE "  Annuler");
    lv_obj_center(lbl_cancel);

    lv_obj_t *btn_add = lv_btn_create(s_add_user_modal);
    lv_obj_set_size(btn_add, 150, 42);
    lv_obj_align(btn_add, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_add, lv_color_hex(0x00C853), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_add, on_add_user_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS "  Ajouter");
    lv_obj_center(lbl_add);
}

lv_obj_t *scr_profile_select_get(void) { return s_screen; }

void scr_profile_select_refresh(void)
{
    lv_obj_clean(s_list_cont);

    user_record_t users[GAME_DB_MAX_USERS];
    int count = db_get_all_users(users, GAME_DB_MAX_USERS);

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

    lv_obj_t *add_box = lv_btn_create(s_list_cont);
    lv_obj_set_size(add_box, 210, 80);
    lv_obj_set_style_bg_color(add_box, lv_color_hex(0x245C43), LV_PART_MAIN);
    lv_obj_set_style_radius(add_box, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(add_box, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(add_box, on_add_box_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_add = lv_label_create(add_box);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS "\nAjouter");
    lv_obj_set_style_text_align(lbl_add, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_add, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_center(lbl_add);

    if(count == 0) {
        lv_obj_t *hint = lv_label_create(s_list_cont);
        lv_label_set_text(hint, "Touchez + Ajouter pour creer votre premier joueur");
        lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(hint, 320);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    }
}
