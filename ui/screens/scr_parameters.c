#include "scr_parameters.h"
#include "screen_manager.h"
#include "game_db.h"
#include "serial_comm.h"
#include <string.h>

/* ── constants ──────────────────────────────────────────────────────────── */

static const char k_correct_pin[]     = "2106";
static const uint32_t k_purge_duration_ms = 10000; /* 10 s */
static const uint32_t k_purge_tick_ms     = 100;   /* timer period */

/* ── static objects ─────────────────────────────────────────────────────── */

static lv_obj_t *s_screen;

/* PIN phase */
static lv_obj_t *s_pin_panel;
static lv_obj_t *s_pin_display;   /* shows ----  /  ****  /  FAUX */

/* Config phase */
static lv_obj_t *s_cfg_panel;
static lv_obj_t *s_tabview;

/* ── Tab 1: Jeu ── */
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

/* ── Tab 2: Joueurs ── */
static lv_obj_t *s_banner_ta_1;
static lv_obj_t *s_banner_ta_2;
static lv_obj_t *s_banner_ta_3;
static lv_obj_t *s_user_list;
static lv_obj_t *s_new_user_ta;
static lv_obj_t *s_kb;
static lv_obj_t *s_delete_popup;
static lv_obj_t *s_delete_popup_label;
static int        s_pending_delete_uid = -1;
static char       s_pending_delete_name[GAME_DB_NAME_LEN];

/* ── Tab 3: Purge ── */
static lv_obj_t  *s_purge_pump_btn[3];   /* 0=Pompe 1  1=Pompe 2  2=Les deux */
static lv_obj_t  *s_purge_start_btn;
static lv_obj_t  *s_purge_start_lbl;
static lv_obj_t  *s_purge_bar;
static lv_obj_t  *s_purge_lbl_countdown;
static lv_obj_t  *s_purge_lbl_status;
static lv_timer_t *s_purge_timer;
static uint32_t    s_purge_elapsed_ms;
static int         s_purge_pump_sel;     /* 0=pump1 / 1=pump2 / 2=both */

/* PIN state */
static char s_pin_buf[5] = {'\0'};
static int  s_pin_len    = 0;

/* RFID enrollment */
static lv_obj_t   *s_params_rfid_popup;
static lv_obj_t   *s_params_rfid_spinner;
static lv_obj_t   *s_params_rfid_status;
static lv_obj_t   *s_params_rfid_countdown;
static lv_obj_t   *s_params_rfid_cancel_btn;
static lv_obj_t   *s_params_rfid_close_btn;
static lv_timer_t *s_params_rfid_timer      = NULL;
static int         s_params_rfid_enroll_uid = -1;
static uint32_t    s_params_rfid_deadline   = 0;

/* ── forward declarations ───────────────────────────────────────────────── */

static void on_delete_user(lv_event_t *e);
static void stop_purge(void);static void on_params_enrollment_result(rfid_enroll_result_t result);
static void on_admin_toggle(lv_event_t *e);
static void on_rfid_badge_btn(lv_event_t *e);
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

/* ── user list helpers ──────────────────────────────────────────────────── */

static void refresh_user_list(void)
{
    lv_obj_clean(s_user_list);

    user_record_t users[GAME_DB_MAX_USERS];
    int count = db_get_all_users(users, GAME_DB_MAX_USERS);

    for (int i = 0; i < count; i++) {
        lv_obj_t *row = lv_obj_create(s_user_list);
        lv_obj_set_size(row, SCREEN_INNER_W - 28, 56);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x0F3460), LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_gap(row, 8, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Name — grows to fill available space */
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, users[i].name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
        lv_obj_set_flex_grow(lbl, 1);

        lv_obj_t *admin_btn = lv_btn_create(row);
        lv_obj_set_size(admin_btn, 120, 36);
        lv_obj_set_style_bg_color(admin_btn,
            users[i].is_admin ? lv_color_hex(0x00C853) : lv_color_hex(0x444444),
            LV_PART_MAIN);
        lv_obj_set_style_radius(admin_btn, 6, LV_PART_MAIN);
        lv_obj_add_event_cb(admin_btn, on_admin_toggle, LV_EVENT_CLICKED,
                            (void *)(intptr_t)users[i].id);
        lv_obj_t *admin_lbl = lv_label_create(admin_btn);
        lv_label_set_text(admin_lbl,
            users[i].is_admin ? "ADMIN" : "Utilisateur");
        lv_obj_center(admin_lbl);

        /* Badge link/unlink button */
        bool has_badge = (users[i].rfid_tag[0] != '\0');
        lv_obj_t *badge_btn = lv_btn_create(row);
        lv_obj_set_size(badge_btn, 130, 36);
        lv_obj_set_style_bg_color(badge_btn,
            has_badge ? lv_color_hex(0xF57C00) : lv_color_hex(0x0F3460),
            LV_PART_MAIN);
        lv_obj_set_style_radius(badge_btn, 6, LV_PART_MAIN);
        lv_obj_add_event_cb(badge_btn, on_rfid_badge_btn, LV_EVENT_CLICKED,
                            (void *)(intptr_t)users[i].id);
        lv_obj_t *badge_lbl = lv_label_create(badge_btn);
        lv_label_set_text(badge_lbl,
            has_badge ? LV_SYMBOL_WIFI "  Delier" : LV_SYMBOL_WIFI "  Lier");
        lv_obj_center(badge_lbl);

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

    lv_snprintf(buf, sizeof(buf), "%d %%", lv_slider_get_value(s_sld_trigger));
    lv_label_set_text(s_lbl_trigger, buf);
    lv_snprintf(buf, sizeof(buf), "%d",    lv_slider_get_value(s_sld_bonus));
    lv_label_set_text(s_lbl_bonus_w, buf);
    lv_snprintf(buf, sizeof(buf), "%d",    lv_slider_get_value(s_sld_nothing));
    lv_label_set_text(s_lbl_nothing_w, buf);
    lv_snprintf(buf, sizeof(buf), "%d",    lv_slider_get_value(s_sld_malus));
    lv_label_set_text(s_lbl_malus_w, buf);
    lv_snprintf(buf, sizeof(buf), "%d",    lv_slider_get_value(s_sld_max_bonus));
    lv_label_set_text(s_lbl_max_bonus, buf);
    lv_snprintf(buf, sizeof(buf), "%d",    lv_slider_get_value(s_sld_max_malus));
    lv_label_set_text(s_lbl_max_malus, buf);
    lv_snprintf(buf, sizeof(buf), "%d s",  lv_slider_get_value(s_sld_cooldown));
    lv_label_set_text(s_lbl_cooldown, buf);
    lv_snprintf(buf, sizeof(buf), "%d",    lv_slider_get_value(s_sld_timeout_add_w));
    lv_label_set_text(s_lbl_timeout_add_w, buf);
    lv_snprintf(buf, sizeof(buf), "%d",    lv_slider_get_value(s_sld_timeout_rem_w));
    lv_label_set_text(s_lbl_timeout_rem_w, buf);
    lv_snprintf(buf, sizeof(buf), "%d min",lv_slider_get_value(s_sld_timeout_mins));
    lv_label_set_text(s_lbl_timeout_mins, buf);
}

static void show_config_phase(void)
{
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
        char b1[192], b2[192], b3[192];
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

/* ── purge helpers ──────────────────────────────────────────────────────── */

static void update_purge_pump_btns(void)
{
    for (int i = 0; i < 3; i++) {
        bool active = (i == s_purge_pump_sel);
        lv_obj_set_style_bg_color(s_purge_pump_btn[i],
            active ? lv_color_hex(0xF5C518) : lv_color_hex(0x2A2A4A),
            LV_PART_MAIN);
        lv_obj_t *lbl = lv_obj_get_child(s_purge_pump_btn[i], 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl,
                active ? lv_color_hex(0x1A1A2E) : lv_color_hex(0xEAEAEA),
                LV_PART_MAIN);
        }
    }
}

static void stop_purge(void)
{
    if (s_purge_timer) {
        lv_timer_delete(s_purge_timer);
        s_purge_timer = NULL;
    }
    serial_comm_send_stop();

    lv_bar_set_value(s_purge_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(s_purge_lbl_countdown, "10.0 s");
    lv_label_set_text(s_purge_start_lbl, LV_SYMBOL_PLAY "  Demarrer");
    lv_obj_set_style_bg_color(s_purge_start_btn, lv_color_hex(0x00C853), LV_PART_MAIN);
    lv_label_set_text(s_purge_lbl_status, "Pret");
    lv_obj_set_style_text_color(s_purge_lbl_status, lv_color_hex(0x888888), LV_PART_MAIN);

    for (int i = 0; i < 3; i++)
        lv_obj_clear_state(s_purge_pump_btn[i], LV_STATE_DISABLED);
}

static void on_purge_timer(lv_timer_t *t)
{
    (void)t;
    s_purge_elapsed_ms += k_purge_tick_ms;

    uint32_t remaining_ms = (s_purge_elapsed_ms >= k_purge_duration_ms)
                            ? 0
                            : k_purge_duration_ms - s_purge_elapsed_ms;

    int pct = (int)((s_purge_elapsed_ms * 100) / k_purge_duration_ms);
    if (pct > 100) pct = 100;
    lv_bar_set_value(s_purge_bar, pct, LV_ANIM_OFF);

    char buf[16];
    lv_snprintf(buf, sizeof(buf), "%.1f s", (float)remaining_ms / 1000.0f);
    lv_label_set_text(s_purge_lbl_countdown, buf);

    if (s_purge_elapsed_ms >= k_purge_duration_ms) {
        lv_label_set_text(s_purge_lbl_status, "Purge terminee");
        lv_obj_set_style_text_color(s_purge_lbl_status,
                                    lv_color_hex(0x00C853), LV_PART_MAIN);
        stop_purge();
    }
}

/* ── event callbacks ────────────────────────────────────────────────────── */

static const char * const k_numpad_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "DEL", "0", LV_SYMBOL_OK, ""
};

static void on_numpad_clicked(lv_event_t *e)
{
    lv_obj_t  *btnm = lv_event_get_target(e);
    uint32_t   id   = lv_btnmatrix_get_selected_btn(btnm);
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
    if (s_purge_timer)
        stop_purge();
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
    if (db_get_user(uid, &user) != 0)
        return;

    s_pending_delete_uid = uid;
    lv_snprintf(s_pending_delete_name, sizeof(s_pending_delete_name),
                "%s", user.name);

    char msg[160];
    lv_snprintf(msg, sizeof(msg),
                "Supprimer le joueur\n\"%s\" ?", s_pending_delete_name);
    lv_label_set_text(s_delete_popup_label, msg);
    lv_obj_clear_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_delete_popup);
}

static void on_admin_toggle(lv_event_t *e)
{
    int uid = (int)(intptr_t)lv_event_get_user_data(e);
    user_record_t user;
    if (db_get_user(uid, &user) != 0)
        return;

    user.is_admin = !user.is_admin;
    if (db_update_user(&user) == 0)
        refresh_user_list();
}

static void on_delete_confirmed(lv_event_t *e)
{
    (void)e;
    if (s_pending_delete_uid >= 0) {
        db_delete_user(s_pending_delete_uid);
        s_pending_delete_uid    = -1;
        s_pending_delete_name[0] = '\0';
        lv_obj_add_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
        refresh_user_list();
    }
}

static void on_delete_cancelled(lv_event_t *e)
{
    (void)e;
    s_pending_delete_uid    = -1;
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

static void on_purge_pump_sel(lv_event_t *e)
{
    s_purge_pump_sel = (int)(intptr_t)lv_event_get_user_data(e);
    update_purge_pump_btns();
}

/* ── RFID badge link/unlink callbacks ────────────────────────────── */

static void on_params_rfid_tick(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    if (now >= s_params_rfid_deadline) {
        lv_label_set_text(s_params_rfid_countdown, "0 s");
        lv_timer_delete(s_params_rfid_timer);
        s_params_rfid_timer = NULL;
        return;
    }
    uint32_t rem = (s_params_rfid_deadline - now + 999) / 1000;
    char buf[12];
    lv_snprintf(buf, sizeof(buf), "%lu s", (unsigned long)rem);
    lv_label_set_text(s_params_rfid_countdown, buf);
}

static void on_params_enrollment_result(rfid_enroll_result_t result)
{
    if (result == RFID_ENROLL_OK) {
        if (s_params_rfid_timer) {
            lv_timer_delete(s_params_rfid_timer);
            s_params_rfid_timer = NULL;
        }
        lv_obj_add_flag(s_params_rfid_spinner,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_params_rfid_cancel_btn, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_params_rfid_status,
                          LV_SYMBOL_OK "  Badge enregistre !");
        lv_obj_set_style_text_color(s_params_rfid_status,
                                    lv_color_hex(0x00C853), LV_PART_MAIN);
        lv_label_set_text(s_params_rfid_countdown, "");
        lv_obj_clear_flag(s_params_rfid_close_btn, LV_OBJ_FLAG_HIDDEN);

    } else if (result == RFID_ENROLL_TAKEN) {
        lv_label_set_text(s_params_rfid_status,
                          LV_SYMBOL_WARNING "  Badge deja utilise !");
        lv_obj_set_style_text_color(s_params_rfid_status,
                                    lv_color_hex(0xE94560), LV_PART_MAIN);
        s_params_rfid_deadline = lv_tick_get() + 30000;
        lv_label_set_text(s_params_rfid_countdown, "30 s");
        screen_manager_rfid_enroll_start(s_params_rfid_enroll_uid, 30000,
                                         on_params_enrollment_result);
        if (s_params_rfid_timer) lv_timer_delete(s_params_rfid_timer);
        s_params_rfid_timer = lv_timer_create(on_params_rfid_tick, 1000, NULL);

    } else {   /* RFID_ENROLL_TIMEOUT */
        if (s_params_rfid_timer) {
            lv_timer_delete(s_params_rfid_timer);
            s_params_rfid_timer = NULL;
        }
        lv_obj_add_flag(s_params_rfid_spinner,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_params_rfid_cancel_btn, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_params_rfid_status, "Delai depasse.");
        lv_obj_set_style_text_color(s_params_rfid_status,
                                    lv_color_hex(0x888888), LV_PART_MAIN);
        lv_label_set_text(s_params_rfid_countdown, "");
        lv_obj_clear_flag(s_params_rfid_close_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_rfid_badge_btn(lv_event_t *e)
{
    int uid = (int)(intptr_t)lv_event_get_user_data(e);
    user_record_t u;
    if (db_get_user(uid, &u) != 0) return;

    if (u.rfid_tag[0] != '\0') {
        /* Has badge — unlink immediately */
        db_set_user_rfid(uid, NULL);
        refresh_user_list();
        return;
    }

    /* No badge — start enrollment */
    s_params_rfid_enroll_uid = uid;
    s_params_rfid_deadline   = lv_tick_get() + 30000;

    lv_label_set_text(s_params_rfid_status, "Presente ton badge");
    lv_obj_set_style_text_color(s_params_rfid_status,
                                lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_label_set_text(s_params_rfid_countdown, "30 s");
    lv_obj_clear_flag(s_params_rfid_spinner,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_params_rfid_cancel_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_params_rfid_close_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_params_rfid_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_params_rfid_popup);

    screen_manager_rfid_enroll_start(uid, 30000, on_params_enrollment_result);
    if (s_params_rfid_timer) lv_timer_delete(s_params_rfid_timer);
    s_params_rfid_timer = lv_timer_create(on_params_rfid_tick, 1000, NULL);
}

static void on_params_rfid_cancel(lv_event_t *e)
{
    (void)e;
    if (s_params_rfid_timer) {
        lv_timer_delete(s_params_rfid_timer);
        s_params_rfid_timer = NULL;
    }
    screen_manager_rfid_enroll_cancel();
    lv_obj_add_flag(s_params_rfid_popup, LV_OBJ_FLAG_HIDDEN);
    s_params_rfid_enroll_uid = -1;
}

static void on_params_rfid_close(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_params_rfid_popup, LV_OBJ_FLAG_HIDDEN);
    s_params_rfid_enroll_uid = -1;
    refresh_user_list();
}

static void on_purge_start_clicked(lv_event_t *e)
{
    (void)e;

    if (s_purge_timer) {
        /* Already running — stop */
        lv_label_set_text(s_purge_lbl_status, "Arretee");
        lv_obj_set_style_text_color(s_purge_lbl_status,
                                    lv_color_hex(0xF5C518), LV_PART_MAIN);
        stop_purge();
        return;
    }

    /* Determine volumes: 999 cL is large enough to not finish before STOP */
    int p1 = (s_purge_pump_sel == 0 || s_purge_pump_sel == 2) ? 999 : 0;
    int p2 = (s_purge_pump_sel == 1 || s_purge_pump_sel == 2) ? 999 : 0;

    if (!serial_comm_send_dispense_cl(p1, p2)) {
        const char *resp = serial_comm_last_response();
        if (resp && strcmp(resp, "ERROR:NO_OBJECT") == 0)
            lv_label_set_text(s_purge_lbl_status, LV_SYMBOL_WARNING " Aucun verre detecte");
        else
            lv_label_set_text(s_purge_lbl_status, LV_SYMBOL_WARNING " Erreur serie");
        lv_obj_set_style_text_color(s_purge_lbl_status,
                                    lv_color_hex(0xE94560), LV_PART_MAIN);
        return;
    }

    /* Lock pump selection during purge */
    for (int i = 0; i < 3; i++)
        lv_obj_add_state(s_purge_pump_btn[i], LV_STATE_DISABLED);

    lv_label_set_text(s_purge_start_lbl, LV_SYMBOL_STOP "  Arreter");
    lv_obj_set_style_bg_color(s_purge_start_btn, lv_color_hex(0xD50000), LV_PART_MAIN);
    lv_label_set_text(s_purge_lbl_status, "Purge en cours...");
    lv_obj_set_style_text_color(s_purge_lbl_status,
                                lv_color_hex(0x00C853), LV_PART_MAIN);

    s_purge_elapsed_ms = 0;
    s_purge_timer = lv_timer_create(on_purge_timer, k_purge_tick_ms, NULL);
}

/* ── builder helpers ────────────────────────────────────────────────────── */

static lv_obj_t *create_slider_row(lv_obj_t *parent, const char *caption,
                                    int min_v, int max_v,
                                    lv_obj_t **sld_out, lv_obj_t **val_lbl_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_INNER_W - 28, 50);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_label_create(row);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_set_width(cap, 300);
    lv_obj_align(cap, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *sld = lv_slider_create(row);
    lv_obj_set_size(sld, 420, 14);
    lv_obj_align(sld, LV_ALIGN_LEFT_MID, 310, 0);
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
    lv_obj_set_size(row, SCREEN_INNER_W - 28, 84);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_label_create(row);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *ta = lv_textarea_create(row);
    lv_obj_set_size(ta, SCREEN_INNER_W - 28, 50);
    lv_obj_align(ta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_add_event_cb(ta, on_ta_focused, LV_EVENT_FOCUSED, NULL);

    *ta_out = ta;
    return row;
}

static void add_section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), LV_PART_MAIN);
}

static lv_obj_t *make_tab_scroll(lv_obj_t *tab)
{
    lv_obj_t *sc = lv_obj_create(tab);
    lv_obj_set_size(sc, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(sc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(sc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sc, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(sc, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(sc, LV_DIR_VER);
    return sc;
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

    lv_obj_t *btn_pin_back = lv_btn_create(s_pin_panel);
    lv_obj_set_size(btn_pin_back, 130, 40);
    lv_obj_align(btn_pin_back, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_pin_back, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_pin_back, 8, LV_PART_MAIN);
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
    lv_obj_clear_flag(s_cfg_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cfg_title = lv_label_create(s_cfg_panel);
    lv_label_set_text(cfg_title, LV_SYMBOL_SETTINGS "  Parametres");
    lv_obj_set_style_text_color(cfg_title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(cfg_title, LV_ALIGN_TOP_MID, 0, 10);

    /* Bottom button row */
    lv_obj_t *btn_row = lv_obj_create(s_cfg_panel);
    lv_obj_set_size(btn_row, SCREEN_W - 40, 44);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(btn_row, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_save = lv_btn_create(btn_row);
    lv_obj_set_size(btn_save, 180, 42);
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

    /* ── Tab view (between title ~36px and footer ~56px) ── */
    s_tabview = lv_tabview_create(s_cfg_panel);
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(s_tabview, 42);
    lv_obj_set_size(s_tabview, SCREEN_W, SCREEN_H - 36 - 56);
    lv_obj_align(s_tabview, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_color(s_tabview, lv_color_hex(0x16213E), LV_PART_MAIN);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(s_tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, LV_PART_MAIN);

    /* ── Tab 1: Jeu ─────────────────────────────────────────────────────── */
    lv_obj_t *tab_game = lv_tabview_add_tab(s_tabview, LV_SYMBOL_SETTINGS "  Jeu");
    lv_obj_set_style_bg_color(tab_game, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_game, 0, LV_PART_MAIN);

    lv_obj_t *sc_game = make_tab_scroll(tab_game);

    add_section_label(sc_game, "Chances");
    create_slider_row(sc_game, "Chance roue bonus (%)", 0, 100,
                      &s_sld_trigger, &s_lbl_trigger);

    add_section_label(sc_game, "Poids roue bonus");
    create_slider_row(sc_game, "Poids BONUS",   0, 5,
                      &s_sld_bonus,         &s_lbl_bonus_w);
    create_slider_row(sc_game, "Poids RIEN",    0, 5,
                      &s_sld_nothing,       &s_lbl_nothing_w);
    create_slider_row(sc_game, "Poids MALUS",   0, 5,
                      &s_sld_malus,         &s_lbl_malus_w);
    create_slider_row(sc_game, "Poids +TEMPS",  0, 5,
                      &s_sld_timeout_add_w, &s_lbl_timeout_add_w);
    create_slider_row(sc_game, "Poids -TEMPS",  0, 5,
                      &s_sld_timeout_rem_w, &s_lbl_timeout_rem_w);

    add_section_label(sc_game, "Timeout");
    create_slider_row(sc_game, "Duree timeout (min)", 1, 60,
                      &s_sld_timeout_mins, &s_lbl_timeout_mins);

    add_section_label(sc_game, "Limites");
    create_slider_row(sc_game, "Max bonus par joueur",    1, 20,
                      &s_sld_max_bonus, &s_lbl_max_bonus);
    create_slider_row(sc_game, "Max malus par joueur",    1, 20,
                      &s_sld_max_malus, &s_lbl_max_malus);
    create_slider_row(sc_game, "Cooldown entre tours (s)", 0, 300,
                      &s_sld_cooldown, &s_lbl_cooldown);

    /* ── Tab 2: Joueurs ─────────────────────────────────────────────────── */
    lv_obj_t *tab_players = lv_tabview_add_tab(s_tabview,
                                               LV_SYMBOL_LIST "  Joueurs");
    lv_obj_set_style_bg_color(tab_players, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_players, 0, LV_PART_MAIN);

    lv_obj_t *sc_players = make_tab_scroll(tab_players);

    add_section_label(sc_players, "Banniere defilante (home)");
    create_textarea_row(sc_players, "Texte 1", "Message principal...",  &s_banner_ta_1);
    create_textarea_row(sc_players, "Texte 2", "Message alternatif...", &s_banner_ta_2);
    create_textarea_row(sc_players, "Texte 3", "Message alternatif...", &s_banner_ta_3);

    add_section_label(sc_players, "Joueurs");

    lv_obj_t *add_row = lv_obj_create(sc_players);
    lv_obj_set_size(add_row, SCREEN_INNER_W - 28, 54);
    lv_obj_set_style_bg_opa(add_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(add_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(add_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(add_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(add_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(add_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(add_row, 10, LV_PART_MAIN);

    s_new_user_ta = lv_textarea_create(add_row);
    lv_obj_set_size(s_new_user_ta, 580, 46);
    lv_textarea_set_one_line(s_new_user_ta, true);
    lv_textarea_set_placeholder_text(s_new_user_ta, "Nom du joueur...");
    lv_obj_add_event_cb(s_new_user_ta, on_ta_focused, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *btn_add = lv_btn_create(add_row);
    lv_obj_set_size(btn_add, 200, 46);
    lv_obj_set_style_bg_color(btn_add, lv_color_hex(0x00C853), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_add, on_add_user_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS "  Ajouter");
    lv_obj_center(lbl_add);

    s_user_list = lv_obj_create(sc_players);
    lv_obj_set_size(s_user_list, SCREEN_INNER_W - 28, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_user_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_user_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_user_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_user_list, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_user_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_user_list, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Tab 3: Purge ───────────────────────────────────────────────────── */
    lv_obj_t *tab_purge = lv_tabview_add_tab(s_tabview,
                                             LV_SYMBOL_REFRESH "  Purge");
    lv_obj_set_style_bg_color(tab_purge, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_purge, 20, LV_PART_MAIN);
    lv_obj_clear_flag(tab_purge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *purge_desc = lv_label_create(tab_purge);
    lv_label_set_text(purge_desc,
        "Lance les pompes pendant 10 secondes pour nettoyer les tuyaux.");
    lv_obj_set_style_text_color(purge_desc, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_label_set_long_mode(purge_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(purge_desc, SCREEN_W - 80);
    lv_obj_align(purge_desc, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Pump selection */
    static const char *k_pump_labels[3] = {
        LV_SYMBOL_DOWNLOAD "  Pompe 1",
        LV_SYMBOL_DOWNLOAD "  Pompe 2",
        LV_SYMBOL_DOWNLOAD "  Les deux"
    };
    s_purge_pump_sel = 2; /* default: both */

    lv_obj_t *sel_row = lv_obj_create(tab_purge);
    lv_obj_set_size(sel_row, LV_SIZE_CONTENT, 48);
    lv_obj_align(sel_row, LV_ALIGN_TOP_LEFT, 0, 48);
    lv_obj_set_style_bg_opa(sel_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(sel_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sel_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(sel_row, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(sel_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sel_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(sel_row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 3; i++) {
        s_purge_pump_btn[i] = lv_btn_create(sel_row);
        lv_obj_set_size(s_purge_pump_btn[i], 160, 44);
        lv_obj_set_style_radius(s_purge_pump_btn[i], 8, LV_PART_MAIN);
        lv_obj_add_event_cb(s_purge_pump_btn[i], on_purge_pump_sel,
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl = lv_label_create(s_purge_pump_btn[i]);
        lv_label_set_text(lbl, k_pump_labels[i]);
        lv_obj_center(lbl);
    }
    update_purge_pump_btns();

    /* Progress bar */
    s_purge_bar = lv_bar_create(tab_purge);
    lv_obj_set_size(s_purge_bar, SCREEN_W - 80, 20);
    lv_obj_align(s_purge_bar, LV_ALIGN_TOP_LEFT, 0, 116);
    lv_bar_set_range(s_purge_bar, 0, 100);
    lv_bar_set_value(s_purge_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_purge_bar, lv_color_hex(0x2A2A4A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_purge_bar, lv_color_hex(0x00C853), LV_PART_INDICATOR);

    /* Countdown label */
    s_purge_lbl_countdown = lv_label_create(tab_purge);
    lv_label_set_text(s_purge_lbl_countdown, "10.0 s");
    lv_obj_set_style_text_color(s_purge_lbl_countdown,
                                lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(s_purge_lbl_countdown, LV_ALIGN_TOP_RIGHT, 0, 148);

    /* Status label */
    s_purge_lbl_status = lv_label_create(tab_purge);
    lv_label_set_text(s_purge_lbl_status, "Pret");
    lv_obj_set_style_text_color(s_purge_lbl_status,
                                lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(s_purge_lbl_status, LV_ALIGN_TOP_LEFT, 0, 148);

    /* Start / Stop button */
    s_purge_start_btn = lv_btn_create(tab_purge);
    lv_obj_set_size(s_purge_start_btn, 240, 60);
    lv_obj_align(s_purge_start_btn, LV_ALIGN_TOP_LEFT, 0, 196);
    lv_obj_set_style_bg_color(s_purge_start_btn, lv_color_hex(0x00C853), LV_PART_MAIN);
    lv_obj_set_style_radius(s_purge_start_btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(s_purge_start_btn, on_purge_start_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_purge_start_lbl = lv_label_create(s_purge_start_btn);
    lv_label_set_text(s_purge_start_lbl, LV_SYMBOL_PLAY "  Demarrer");
    lv_obj_center(s_purge_start_lbl);

    /* ── Keyboard overlay ──────────────────────────────────────────────── */
    s_kb = lv_keyboard_create(s_screen);
    lv_obj_set_size(s_kb, SCREEN_W, 220);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, on_kb_event, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kb, on_kb_event, LV_EVENT_CANCEL, NULL);

    /* ── Delete-user popup ─────────────────────────────────────────────── */
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
    lv_obj_set_style_text_align(s_delete_popup_label,
                                LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_delete_popup_label,
                                lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_label_set_long_mode(s_delete_popup_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_delete_popup_label, LV_ALIGN_TOP_MID, 0, 42);

    lv_obj_t *btn_cancel_delete = lv_btn_create(s_delete_popup);
    lv_obj_set_size(btn_cancel_delete, 150, 42);
    lv_obj_align(btn_cancel_delete, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel_delete, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_cancel_delete, on_delete_cancelled,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cancel_delete = lv_label_create(btn_cancel_delete);
    lv_label_set_text(lbl_cancel_delete, LV_SYMBOL_CLOSE "  Annuler");
    lv_obj_center(lbl_cancel_delete);

    lv_obj_t *btn_confirm_delete = lv_btn_create(s_delete_popup);
    lv_obj_set_size(btn_confirm_delete, 150, 42);
    lv_obj_align(btn_confirm_delete, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_confirm_delete, lv_color_hex(0xD50000), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_confirm_delete, on_delete_confirmed,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_confirm_delete = lv_label_create(btn_confirm_delete);
    lv_label_set_text(lbl_confirm_delete, LV_SYMBOL_TRASH "  Supprimer");
    lv_obj_center(lbl_confirm_delete);

    /* ── RFID enrollment popup ──────────────────────────────────────── */
    s_params_rfid_popup = lv_obj_create(s_screen);
    lv_obj_set_size(s_params_rfid_popup, 420, 250);
    lv_obj_align(s_params_rfid_popup, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_bg_color(s_params_rfid_popup, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_params_rfid_popup, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_params_rfid_popup, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_params_rfid_popup, 16, LV_PART_MAIN);
    lv_obj_clear_flag(s_params_rfid_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_params_rfid_popup, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *rfid_title = lv_label_create(s_params_rfid_popup);
    lv_label_set_text(rfid_title, LV_SYMBOL_WIFI "  Lier un badge RFID");
    lv_obj_set_style_text_color(rfid_title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(rfid_title, LV_ALIGN_TOP_MID, 0, 0);

    s_params_rfid_spinner = lv_spinner_create(s_params_rfid_popup);
    lv_obj_set_size(s_params_rfid_spinner, 54, 54);
    lv_obj_align(s_params_rfid_spinner, LV_ALIGN_TOP_MID, 0, 34);
    lv_spinner_set_anim_params(s_params_rfid_spinner, 1000, 60);

    s_params_rfid_status = lv_label_create(s_params_rfid_popup);
    lv_label_set_text(s_params_rfid_status, "Presente ton badge");
    lv_obj_set_style_text_color(s_params_rfid_status,
                                lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_align(s_params_rfid_status, LV_ALIGN_TOP_MID, 0, 100);

    s_params_rfid_countdown = lv_label_create(s_params_rfid_popup);
    lv_label_set_text(s_params_rfid_countdown, "30 s");
    lv_obj_set_style_text_color(s_params_rfid_countdown,
                                lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(s_params_rfid_countdown, LV_ALIGN_TOP_MID, 0, 128);

    s_params_rfid_cancel_btn = lv_btn_create(s_params_rfid_popup);
    lv_obj_set_size(s_params_rfid_cancel_btn, 150, 42);
    lv_obj_align(s_params_rfid_cancel_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_params_rfid_cancel_btn, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_add_event_cb(s_params_rfid_cancel_btn, on_params_rfid_cancel,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *rfid_cancel_lbl = lv_label_create(s_params_rfid_cancel_btn);
    lv_label_set_text(rfid_cancel_lbl, LV_SYMBOL_CLOSE "  Annuler");
    lv_obj_center(rfid_cancel_lbl);

    s_params_rfid_close_btn = lv_btn_create(s_params_rfid_popup);
    lv_obj_set_size(s_params_rfid_close_btn, 150, 42);
    lv_obj_align(s_params_rfid_close_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(s_params_rfid_close_btn, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_add_flag(s_params_rfid_close_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_params_rfid_close_btn, on_params_rfid_close,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *rfid_close_lbl = lv_label_create(s_params_rfid_close_btn);
    lv_label_set_text(rfid_close_lbl, LV_SYMBOL_OK "  Fermer");
    lv_obj_center(rfid_close_lbl);
}

lv_obj_t *scr_parameters_get(void) { return s_screen; }

void scr_parameters_unlock_admin(void)
{
    show_config_phase();
}

void scr_parameters_refresh(void)
{
    /* Abort any ongoing purge */
    if (s_purge_timer)
        stop_purge();

    /* Always reset to PIN phase on entry */
    s_pin_len = 0;
    memset(s_pin_buf, 0, sizeof(s_pin_buf));
    update_pin_display();

    lv_obj_add_flag(s_kb,           LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_cfg_panel,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
    /* Cancel any in-progress RFID enrollment */
    if (s_params_rfid_timer) {
        lv_timer_delete(s_params_rfid_timer);
        s_params_rfid_timer = NULL;
    }
    screen_manager_rfid_enroll_cancel();
    lv_obj_add_flag(s_params_rfid_popup, LV_OBJ_FLAG_HIDDEN);
    s_params_rfid_enroll_uid    = -1;
    s_pending_delete_uid    = -1;
    s_pending_delete_name[0] = '\0';
    lv_obj_clear_flag(s_pin_panel, LV_OBJ_FLAG_HIDDEN);
}
