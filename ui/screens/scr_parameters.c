#include "scr_parameters.h"
#include "screen_manager.h"
#include "game_db.h"
#include <string.h>

/* ── constants ──────────────────────────────────────────────────────────── */

static const char k_correct_pin[] = "2106";

/* ── static objects ─────────────────────────────────────────────────────── */

static lv_obj_t *s_screen;

/* PIN phase */
static lv_obj_t *s_pin_panel;
static lv_obj_t *s_pin_display;   /* shows ---- / **** / FAUX */

/* Config phase */
static lv_obj_t *s_cfg_panel;
static lv_obj_t *s_sld_trigger;
static lv_obj_t *s_sld_bonus;
static lv_obj_t *s_sld_nothing;
static lv_obj_t *s_sld_malus;
static lv_obj_t *s_sld_timeout_add_w;
static lv_obj_t *s_sld_timeout_rem_w;
static lv_obj_t *s_sld_timeout_mins;
static lv_obj_t *s_sld_max_bonus;
static lv_obj_t *s_sld_max_malus;
static lv_obj_t *s_sld_cooldown;
static lv_obj_t *s_lbl_trigger;
static lv_obj_t *s_lbl_bonus_w;
static lv_obj_t *s_lbl_nothing_w;
static lv_obj_t *s_lbl_malus_w;
static lv_obj_t *s_lbl_timeout_add_w;
static lv_obj_t *s_lbl_timeout_rem_w;
static lv_obj_t *s_lbl_timeout_mins;
static lv_obj_t *s_lbl_max_bonus;
static lv_obj_t *s_lbl_max_malus;
static lv_obj_t *s_lbl_cooldown;
static lv_obj_t *s_user_list;
static lv_obj_t *s_new_user_ta;
static lv_obj_t *s_banner_ta_1;
static lv_obj_t *s_banner_ta_2;
static lv_obj_t *s_banner_ta_3;
static lv_obj_t *s_kb;
static lv_obj_t *s_delete_popup;
static lv_obj_t *s_delete_popup_label;
static int s_pending_delete_uid = -1;
static char s_pending_delete_name[GAME_DB_NAME_LEN];

/* PIN state */
static char s_pin_buf[5] = {'\0'};
static int  s_pin_len    = 0;

/* ── PIN helpers ────────────────────────────────────────────────────────── */

static void update_pin_display(void)
{
    char buf[5];
    for (int i = 0; i < 4; i++)
        buf[i] = (i < s_pin_len) ? '*' : '-';
    buf[4] = '\0';
    lv_label_set_text(s_pin_display, buf);
    lv_obj_set_style_text_color(s_pin_display, lv_color_hex(0xF5C518), LV_PART_MAIN);
}

/* ── user list (inside config panel) ───────────────────────────────────── */

static void on_delete_user(lv_event_t *e);  /* forward decl */

static void refresh_user_list(void)
{
    lv_obj_clean(s_user_list);

    user_record_t users[GAME_DB_MAX_USERS];
    int count = db_get_all_users(users, GAME_DB_MAX_USERS);

    for (int i = 0; i < count; i++) {
        /* Row container */
        lv_obj_t *row = lv_obj_create(s_user_list);
        lv_obj_set_size(row, SCREEN_INNER_W, 48);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x0F3460), LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(row, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Name label */
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, users[i].name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
        lv_obj_set_width(lbl, SCREEN_INNER_W - 80);

        /* Delete button */
        lv_obj_t *del = lv_btn_create(row);
        lv_obj_set_size(del, 60, 36);
        lv_obj_set_style_bg_color(del, lv_color_hex(0xD50000), LV_PART_MAIN);
        lv_obj_add_event_cb(del, on_delete_user, LV_EVENT_CLICKED,
                            (void *)(intptr_t)users[i].id);
        lv_obj_t *del_lbl = lv_label_create(del);
        lv_label_set_text(del_lbl, LV_SYMBOL_CLOSE);
        lv_obj_center(del_lbl);
    }
}

/* ── config-phase helpers ───────────────────────────────────────────────── */

static void update_slider_labels(void)
{
    char buf[12];
    lv_snprintf(buf, sizeof(buf), "%d %%",
                lv_slider_get_value(s_sld_trigger));
    lv_label_set_text(s_lbl_trigger, buf);

    lv_snprintf(buf, sizeof(buf), "%d",
                lv_slider_get_value(s_sld_bonus));
    lv_label_set_text(s_lbl_bonus_w, buf);

    lv_snprintf(buf, sizeof(buf), "%d",
                lv_slider_get_value(s_sld_nothing));
    lv_label_set_text(s_lbl_nothing_w, buf);

    lv_snprintf(buf, sizeof(buf), "%d",
                lv_slider_get_value(s_sld_malus));
    lv_label_set_text(s_lbl_malus_w, buf);

    lv_snprintf(buf, sizeof(buf), "%d",
                lv_slider_get_value(s_sld_max_bonus));
    lv_label_set_text(s_lbl_max_bonus, buf);

    lv_snprintf(buf, sizeof(buf), "%d",
                lv_slider_get_value(s_sld_max_malus));
    lv_label_set_text(s_lbl_max_malus, buf);

    lv_snprintf(buf, sizeof(buf), "%d s",
                lv_slider_get_value(s_sld_cooldown));
    lv_label_set_text(s_lbl_cooldown, buf);

    lv_snprintf(buf, sizeof(buf), "%d",
                lv_slider_get_value(s_sld_timeout_add_w));
    lv_label_set_text(s_lbl_timeout_add_w, buf);

    lv_snprintf(buf, sizeof(buf), "%d",
                lv_slider_get_value(s_sld_timeout_rem_w));
    lv_label_set_text(s_lbl_timeout_rem_w, buf);

    lv_snprintf(buf, sizeof(buf), "%d min",
                lv_slider_get_value(s_sld_timeout_mins));
    lv_label_set_text(s_lbl_timeout_mins, buf);
}

static void show_config_phase(void)
{
    /* Load current values */
    lv_slider_set_value(s_sld_trigger,
        db_get_config("wheel_trigger_chance", 20), LV_ANIM_OFF);
    lv_slider_set_value(s_sld_bonus,
        db_get_config("bonus_wheel_bonus_weight", 1), LV_ANIM_OFF);
    lv_slider_set_value(s_sld_nothing,
        db_get_config("bonus_wheel_nothing_weight", 2), LV_ANIM_OFF);
    lv_slider_set_value(s_sld_malus,
        db_get_config("bonus_wheel_malus_weight", 1), LV_ANIM_OFF);
    lv_slider_set_value(s_sld_max_bonus,
        db_get_config("max_bonus_stack", 5), LV_ANIM_OFF);
    lv_slider_set_value(s_sld_max_malus,
        db_get_config("max_malus_stack", 5), LV_ANIM_OFF);
    lv_slider_set_value(s_sld_cooldown,
        db_get_config("spin_cooldown_seconds", 0), LV_ANIM_OFF);
    lv_slider_set_value(s_sld_timeout_add_w,
        db_get_config("bonus_wheel_timeout_add_weight", 1), LV_ANIM_OFF);
    lv_slider_set_value(s_sld_timeout_rem_w,
        db_get_config("bonus_wheel_timeout_remove_weight", 1), LV_ANIM_OFF);
    lv_slider_set_value(s_sld_timeout_mins,
        db_get_config("timeout_modifier_minutes", 5), LV_ANIM_OFF);

    {
        char b1[192];
        char b2[192];
        char b3[192];
        db_get_text_config("home_banner_text_1", b1, sizeof(b1),
            "L'abus d'eau est dangereux pour la sante.");
        db_get_text_config("home_banner_text_2", b2, sizeof(b2), "");
        db_get_text_config("home_banner_text_3", b3, sizeof(b3), "");
        lv_textarea_set_text(s_banner_ta_1, b1);
        lv_textarea_set_text(s_banner_ta_2, b2);
        lv_textarea_set_text(s_banner_ta_3, b3);
    }

    update_slider_labels();

    refresh_user_list();

    lv_obj_add_flag(s_pin_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_cfg_panel, LV_OBJ_FLAG_HIDDEN);
}

/* ── event callbacks ────────────────────────────────────────────────────── */

/* numpad map for PIN entry */
static const char * const k_numpad_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "DEL", "0", LV_SYMBOL_OK, ""
};

static void on_numpad_clicked(lv_event_t *e)
{
    lv_obj_t *btnm = lv_event_get_target(e);
    uint32_t  id   = lv_btnmatrix_get_selected_btn(btnm);
    const char *txt = lv_btnmatrix_get_btn_text(btnm, id);
    if (!txt) return;

    if (strcmp(txt, "DEL") == 0) {
        if (s_pin_len > 0) {
            s_pin_len--;
            s_pin_buf[s_pin_len] = '\0';
        }
        update_pin_display();
    } else if (strcmp(txt, LV_SYMBOL_OK) == 0) {
        if (strcmp(s_pin_buf, k_correct_pin) == 0) {
            show_config_phase();
        } else {
            lv_label_set_text(s_pin_display, "FAUX");
            lv_obj_set_style_text_color(s_pin_display,
                lv_color_hex(0xE94560), LV_PART_MAIN);
            s_pin_len = 0;
            memset(s_pin_buf, 0, sizeof(s_pin_buf));
        }
    } else if (txt[0] >= '0' && txt[0] <= '9' && s_pin_len < 4) {
        s_pin_buf[s_pin_len++] = txt[0];
        s_pin_buf[s_pin_len]   = '\0';
        update_pin_display();
    }
}

static void on_slider_changed(lv_event_t *e)
{
    (void)e;
    update_slider_labels();
}

static void on_save_clicked(lv_event_t *e)
{
    (void)e;
    db_set_config("wheel_trigger_chance",
                  lv_slider_get_value(s_sld_trigger));
    db_set_config("bonus_wheel_bonus_weight",
                  lv_slider_get_value(s_sld_bonus));
    db_set_config("bonus_wheel_nothing_weight",
                  lv_slider_get_value(s_sld_nothing));
    db_set_config("bonus_wheel_malus_weight",
                  lv_slider_get_value(s_sld_malus));
    db_set_config("max_bonus_stack",
                  lv_slider_get_value(s_sld_max_bonus));
    db_set_config("max_malus_stack",
                  lv_slider_get_value(s_sld_max_malus));
    db_set_config("spin_cooldown_seconds",
                  lv_slider_get_value(s_sld_cooldown));
    db_set_config("bonus_wheel_timeout_add_weight",
                  lv_slider_get_value(s_sld_timeout_add_w));
    db_set_config("bonus_wheel_timeout_remove_weight",
                  lv_slider_get_value(s_sld_timeout_rem_w));
    db_set_config("timeout_modifier_minutes",
                  lv_slider_get_value(s_sld_timeout_mins));

    db_set_text_config("home_banner_text_1", lv_textarea_get_text(s_banner_ta_1));
    db_set_text_config("home_banner_text_2", lv_textarea_get_text(s_banner_ta_2));
    db_set_text_config("home_banner_text_3", lv_textarea_get_text(s_banner_ta_3));
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    /* Hide keyboard if visible */
    if (!lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN))
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    if (!lv_obj_has_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN))
        lv_obj_add_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
    screen_manager_load(SCREEN_HOME);
}

static void on_add_user_clicked(lv_event_t *e)
{
    (void)e;
    const char *name = lv_textarea_get_text(s_new_user_ta);
    if (name && name[0] != '\0') {
        db_add_user(name);
        lv_textarea_set_text(s_new_user_ta, "");
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        refresh_user_list();
    }
}

static void on_delete_user(lv_event_t *e)
{
    int uid = (int)(intptr_t)lv_event_get_user_data(e);
    user_record_t user;
    if(db_get_user(uid, &user) != 0) {
        return;
    }

    s_pending_delete_uid = uid;
    lv_snprintf(s_pending_delete_name, sizeof(s_pending_delete_name), "%s", user.name);

    char msg[160];
    lv_snprintf(msg, sizeof(msg), "Supprimer le joueur\n\"%s\" ?", s_pending_delete_name);
    lv_label_set_text(s_delete_popup_label, msg);
    lv_obj_clear_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_delete_popup);
}

static void on_delete_confirmed(lv_event_t *e)
{
    (void)e;

    if(s_pending_delete_uid >= 0) {
        db_delete_user(s_pending_delete_uid);
        s_pending_delete_uid = -1;
        s_pending_delete_name[0] = '\0';
        lv_obj_add_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
        refresh_user_list();
    }
}

static void on_delete_cancelled(lv_event_t *e)
{
    (void)e;
    s_pending_delete_uid = -1;
    s_pending_delete_name[0] = '\0';
    lv_obj_add_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
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
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

/* ── slider row builder ─────────────────────────────────────────────────── */

static lv_obj_t *create_slider_row(lv_obj_t *parent, const char *caption,
                                    int min_v, int max_v,
                                    lv_obj_t **sld_out, lv_obj_t **val_lbl_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_INNER_W, 50);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_label_create(row);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_set_width(cap, 320);
    lv_obj_align(cap, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *sld = lv_slider_create(row);
    lv_obj_set_size(sld, 450, 14);
    lv_obj_align(sld, LV_ALIGN_LEFT_MID, 330, 0);
    lv_slider_set_range(sld, min_v, max_v);
    lv_obj_add_event_cb(sld, on_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_style_text_color(val, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_set_width(val, 80);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);

    *sld_out     = sld;
    *val_lbl_out = val;
    return row;
}

static lv_obj_t *create_textarea_row(lv_obj_t *parent, const char *caption,
                                     const char *placeholder,
                                     lv_obj_t **ta_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_INNER_W, 84);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_label_create(row);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *ta = lv_textarea_create(row);
    lv_obj_set_size(ta, SCREEN_INNER_W, 50);
    lv_obj_align(ta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_add_event_cb(ta, on_ta_focused, LV_EVENT_FOCUSED, NULL);

    *ta_out = ta;
    return row;
}

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_parameters_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* ━━━━━━━━━━ PIN PANEL ━━━━━━━━━━ */
    s_pin_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_pin_panel, SCREEN_W, SCREEN_H);
    lv_obj_align(s_pin_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_pin_panel, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pin_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_pin_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_pin_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pin_title = lv_label_create(s_pin_panel);
    lv_label_set_text(pin_title, LV_SYMBOL_SETTINGS "  Code PIN");
    lv_obj_set_style_text_color(pin_title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(pin_title, LV_ALIGN_TOP_MID, 0, 40);

    s_pin_display = lv_label_create(s_pin_panel);
    lv_label_set_text(s_pin_display, "----");
    lv_obj_set_style_text_color(s_pin_display, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(s_pin_display, LV_ALIGN_TOP_MID, 0, 100);

    lv_obj_t *btnm = lv_btnmatrix_create(s_pin_panel);
    lv_btnmatrix_set_map(btnm, k_numpad_map);
    lv_obj_set_size(btnm, 300, 260);
    lv_obj_align(btnm, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(btnm, on_numpad_clicked, LV_EVENT_CLICKED, NULL);

    /* Back button on PIN screen */
    lv_obj_t *btn_pin_back = lv_btn_create(s_pin_panel);
    lv_obj_set_size(btn_pin_back, 130, 40);
    lv_obj_align(btn_pin_back, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_pin_back, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_pin_back, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn_pin_back, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn_pin_back, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_pin_back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_pin_back = lv_label_create(btn_pin_back);
    lv_label_set_text(lbl_pin_back, LV_SYMBOL_LEFT "  Retour");
    lv_obj_center(lbl_pin_back);

    /* ━━━━━━━━━━ CONFIG PANEL ━━━━━━━━━━ */
    s_cfg_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_cfg_panel, SCREEN_W, SCREEN_H);
    lv_obj_align(s_cfg_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_cfg_panel, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_cfg_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_cfg_panel, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_cfg_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *cfg_title = lv_label_create(s_cfg_panel);
    lv_label_set_text(cfg_title, LV_SYMBOL_SETTINGS "  Parametres");
    lv_obj_set_style_text_color(cfg_title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(cfg_title, LV_ALIGN_TOP_MID, 0, 14);

    /* Scrollable content area */
    lv_obj_t *scroll = lv_obj_create(s_cfg_panel);
    lv_obj_set_size(scroll, SCREEN_INNER_W, SCREEN_INNER_H);
    lv_obj_align(scroll, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(scroll, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scroll, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(scroll, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);

    /* Section: Chances */
    lv_obj_t *sec1 = lv_label_create(scroll);
    lv_label_set_text(sec1, "Chances");
    lv_obj_set_style_text_color(sec1, lv_color_hex(0x888888), LV_PART_MAIN);

    create_slider_row(scroll, "Chance roue bonus", 0, 100,
                      &s_sld_trigger, &s_lbl_trigger);

    /* Section: Poids roue bonus */
    lv_obj_t *sec2 = lv_label_create(scroll);
    lv_label_set_text(sec2, "Poids roue bonus");
    lv_obj_set_style_text_color(sec2, lv_color_hex(0x888888), LV_PART_MAIN);

    create_slider_row(scroll, "Poids BONUS",  0, 5, &s_sld_bonus,   &s_lbl_bonus_w);
    create_slider_row(scroll, "Poids RIEN",   0, 5, &s_sld_nothing, &s_lbl_nothing_w);
    create_slider_row(scroll, "Poids MALUS",  0, 5, &s_sld_malus,   &s_lbl_malus_w);
    create_slider_row(scroll, "Poids +TEMPS", 0, 5, &s_sld_timeout_add_w, &s_lbl_timeout_add_w);
    create_slider_row(scroll, "Poids -TEMPS", 0, 5, &s_sld_timeout_rem_w, &s_lbl_timeout_rem_w);

    /* Section: Timeout */
    lv_obj_t *sec_to = lv_label_create(scroll);
    lv_label_set_text(sec_to, "Timeout");
    lv_obj_set_style_text_color(sec_to, lv_color_hex(0x888888), LV_PART_MAIN);

    create_slider_row(scroll, "Duree timeout (min)", 1, 60,
                      &s_sld_timeout_mins, &s_lbl_timeout_mins);

    /* Section: Limites */
    lv_obj_t *sec_lim = lv_label_create(scroll);
    lv_label_set_text(sec_lim, "Limites");
    lv_obj_set_style_text_color(sec_lim, lv_color_hex(0x888888), LV_PART_MAIN);

    create_slider_row(scroll, "Max bonus par joueur", 1, 20,
                      &s_sld_max_bonus, &s_lbl_max_bonus);
    create_slider_row(scroll, "Max malus par joueur", 1, 20,
                      &s_sld_max_malus, &s_lbl_max_malus);
    create_slider_row(scroll, "Cooldown entre tours (s)", 0, 300,
                      &s_sld_cooldown, &s_lbl_cooldown);

    /* Section: Banniere defilante */
    lv_obj_t *sec_banner = lv_label_create(scroll);
    lv_label_set_text(sec_banner, "Banniere defilante (home)");
    lv_obj_set_style_text_color(sec_banner, lv_color_hex(0x888888), LV_PART_MAIN);

    create_textarea_row(scroll, "Texte 1", "Message principal...", &s_banner_ta_1);
    create_textarea_row(scroll, "Texte 2", "Message alternatif...", &s_banner_ta_2);
    create_textarea_row(scroll, "Texte 3", "Message alternatif...", &s_banner_ta_3);

    /* Section: Joueurs */
    lv_obj_t *sec3 = lv_label_create(scroll);
    lv_label_set_text(sec3, "Joueurs");
    lv_obj_set_style_text_color(sec3, lv_color_hex(0x888888), LV_PART_MAIN);

    /* Add-user row */
    lv_obj_t *add_row = lv_obj_create(scroll);
    lv_obj_set_size(add_row, SCREEN_INNER_W, 54);
    lv_obj_set_style_bg_opa(add_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(add_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(add_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(add_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(add_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(add_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(add_row, 10, LV_PART_MAIN);

    s_new_user_ta = lv_textarea_create(add_row);
    lv_obj_set_size(s_new_user_ta, 620, 46);
    lv_textarea_set_one_line(s_new_user_ta, true);
    lv_textarea_set_placeholder_text(s_new_user_ta, "Nom du joueur...");
    lv_obj_add_event_cb(s_new_user_ta, on_ta_focused,
                        LV_EVENT_FOCUSED, NULL);

    lv_obj_t *btn_add = lv_btn_create(add_row);
    lv_obj_set_size(btn_add, 200, 46);
    lv_obj_set_style_bg_color(btn_add, lv_color_hex(0x00C853), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_add, on_add_user_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS "  Ajouter");
    lv_obj_center(lbl_add);

    /* User list container */
    s_user_list = lv_obj_create(scroll);
    lv_obj_set_size(s_user_list, SCREEN_INNER_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_user_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_user_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_user_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_user_list, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_user_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_user_list, LV_OBJ_FLAG_SCROLLABLE);

    /* Bottom buttons row */
    lv_obj_t *btn_row = lv_obj_create(s_cfg_panel);
    lv_obj_set_size(btn_row, SCREEN_INNER_W, 44);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_save = lv_btn_create(btn_row);
    lv_obj_set_size(btn_save, 160, 42);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_save, on_save_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, LV_SYMBOL_SAVE "  Sauvegarder");
    lv_obj_center(lbl_save);

    lv_obj_t *btn_back = lv_btn_create(btn_row);
    lv_obj_set_size(btn_back, 160, 42);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT "  Retour");
    lv_obj_center(lbl_back);

    /* Keyboard — overlay at bottom, hidden by default */
    s_kb = lv_keyboard_create(s_screen);
    lv_obj_set_size(s_kb, SCREEN_W, 220);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, on_kb_event, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kb, on_kb_event, LV_EVENT_CANCEL, NULL);

    s_delete_popup = lv_obj_create(s_screen);
    lv_obj_set_size(s_delete_popup, 420, 170);
    lv_obj_align(s_delete_popup, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_bg_color(s_delete_popup, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_delete_popup, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_delete_popup, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_delete_popup, 16, LV_PART_MAIN);
    lv_obj_clear_flag(s_delete_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *popup_title = lv_label_create(s_delete_popup);
    lv_label_set_text(popup_title, LV_SYMBOL_WARNING "  Confirmation");
    lv_obj_set_style_text_color(popup_title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(popup_title, LV_ALIGN_TOP_MID, 0, 0);

    s_delete_popup_label = lv_label_create(s_delete_popup);
    lv_obj_set_width(s_delete_popup_label, 360);
    lv_obj_set_style_text_align(s_delete_popup_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_delete_popup_label, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_label_set_long_mode(s_delete_popup_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_delete_popup_label, LV_ALIGN_TOP_MID, 0, 42);

    lv_obj_t *btn_cancel_delete = lv_btn_create(s_delete_popup);
    lv_obj_set_size(btn_cancel_delete, 150, 42);
    lv_obj_align(btn_cancel_delete, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel_delete, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_cancel_delete, on_delete_cancelled, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cancel_delete = lv_label_create(btn_cancel_delete);
    lv_label_set_text(lbl_cancel_delete, LV_SYMBOL_CLOSE "  Annuler");
    lv_obj_center(lbl_cancel_delete);

    lv_obj_t *btn_confirm_delete = lv_btn_create(s_delete_popup);
    lv_obj_set_size(btn_confirm_delete, 150, 42);
    lv_obj_align(btn_confirm_delete, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_confirm_delete, lv_color_hex(0xD50000), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_confirm_delete, on_delete_confirmed, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_confirm_delete = lv_label_create(btn_confirm_delete);
    lv_label_set_text(lbl_confirm_delete, LV_SYMBOL_TRASH "  Supprimer");
    lv_obj_center(lbl_confirm_delete);
}

lv_obj_t *scr_parameters_get(void) { return s_screen; }

void scr_parameters_refresh(void)
{
    /* Always reset to PIN phase on entry */
    s_pin_len = 0;
    memset(s_pin_buf, 0, sizeof(s_pin_buf));
    update_pin_display();

    lv_obj_add_flag(s_kb,        LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_cfg_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
    s_pending_delete_uid = -1;
    s_pending_delete_name[0] = '\0';
    lv_obj_clear_flag(s_pin_panel, LV_OBJ_FLAG_HIDDEN);
}
