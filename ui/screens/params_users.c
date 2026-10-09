#include "params_internal.h"
#include "scr_parameters.h"
#include "screen_manager.h"
#include "game_db.h"
#include "game_logic.h"
#include "serial_comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parametres > Joueurs: the user list, per-user admin and RFID badge linking,
 * and the home-screen banner texts.
 *
 * The text-area row builder lives here: these banners are the only ones. */

static lv_obj_t *s_banner_ta_1;
static lv_obj_t *s_banner_ta_2;
static lv_obj_t *s_banner_ta_3;
static lv_obj_t *s_user_list;
static lv_obj_t *s_new_user_ta;
static lv_obj_t *s_delete_popup;
static lv_obj_t *s_delete_popup_label;
static lv_obj_t *s_reset_popup;
static lv_obj_t *s_reset_status;
static int        s_pending_delete_uid = -1;
static char       s_pending_delete_name[GAME_DB_NAME_LEN];
static lv_obj_t   *s_params_rfid_popup;
static lv_obj_t   *s_params_rfid_spinner;
static lv_obj_t   *s_params_rfid_status;
static lv_obj_t   *s_params_rfid_countdown;
static lv_obj_t   *s_params_rfid_cancel_btn;
static lv_obj_t   *s_params_rfid_close_btn;
static lv_timer_t *s_params_rfid_timer      = NULL;
static int         s_params_rfid_enroll_uid = -1;
static uint32_t    s_params_rfid_deadline   = 0;

static void on_delete_user(lv_event_t *e);
static void on_admin_toggle(lv_event_t *e);
static void on_rfid_badge_btn(lv_event_t *e);
static void on_params_enrollment_result(rfid_enroll_result_t result);
static void on_params_rfid_cancel(lv_event_t *e);
static void on_params_rfid_close(lv_event_t *e);
static void on_delete_confirmed(lv_event_t *e);
static void on_delete_cancelled(lv_event_t *e);
static void on_add_user_clicked(lv_event_t *e);
static void on_reset_clicked(lv_event_t *e);
static void on_reset_confirmed(lv_event_t *e);
static void on_reset_cancelled(lv_event_t *e);

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
    lv_obj_add_event_cb(ta, params_on_ta_focused, LV_EVENT_FOCUSED, NULL);

    *ta_out = ta;
    return row;
}

void params_users_refresh_list(void)
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

static void on_add_user_clicked(lv_event_t *e)
{
    (void)e;
    const char *name = lv_textarea_get_text(s_new_user_ta);
    if (name && name[0] != '\0') {
        db_add_user(name);
        lv_textarea_set_text(s_new_user_ta, "");
        lv_obj_add_flag(params_kb, LV_OBJ_FLAG_HIDDEN);
        params_users_refresh_list();
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
        params_users_refresh_list();
}

static void on_delete_confirmed(lv_event_t *e)
{
    (void)e;
    if (s_pending_delete_uid >= 0) {
        db_delete_user(s_pending_delete_uid);
        s_pending_delete_uid    = -1;
        s_pending_delete_name[0] = '\0';
        lv_obj_add_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
        params_users_refresh_list();
    }
}

/* Archive every player and their stats to a file, then start from a blank
 * player table.  Game settings and cocktails are not touched. */
static void on_reset_clicked(lv_event_t *e)
{
    (void)e;
    lv_obj_clear_flag(s_reset_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_reset_popup);
}

static void on_reset_cancelled(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_reset_popup, LV_OBJ_FLAG_HIDDEN);
}

static void on_reset_confirmed(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_reset_popup, LV_OBJ_FLAG_HIDDEN);

    char path[320];
    if (db_archive_and_reset_stats(path, sizeof(path)) != 0) {
        lv_label_set_text(s_reset_status,
            LV_SYMBOL_WARNING "  Echec de l'archivage, rien n'a ete supprime");
        lv_obj_set_style_text_color(s_reset_status,
                                    lv_color_hex(0xE94560), LV_PART_MAIN);
        return;
    }

    /* The selected player no longer exists. */
    game_set_active_user(-1);
    params_users_refresh_list();

    const char *base = strrchr(path, '/');
    char msg[200];
    lv_snprintf(msg, sizeof(msg), LV_SYMBOL_OK "  Archive : %s",
                base ? base + 1 : path);
    lv_label_set_text(s_reset_status, msg);
    lv_obj_set_style_text_color(s_reset_status,
                                lv_color_hex(0x00C853), LV_PART_MAIN);
}

static void on_delete_cancelled(lv_event_t *e)
{
    (void)e;
    s_pending_delete_uid    = -1;
    s_pending_delete_name[0] = '\0';
    lv_obj_add_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
}

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
        params_users_refresh_list();
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
    params_users_refresh_list();
}

void params_users_build(lv_obj_t *tab)
{
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);

    lv_obj_t *sc_players = params_make_tab_scroll(tab);

    params_add_section_label(sc_players, "Banniere defilante (home)");
    create_textarea_row(sc_players, "Texte 1", "Message principal...",  &s_banner_ta_1);
    create_textarea_row(sc_players, "Texte 2", "Message alternatif...", &s_banner_ta_2);
    create_textarea_row(sc_players, "Texte 3", "Message alternatif...", &s_banner_ta_3);

    params_add_section_label(sc_players, "Joueurs");

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
    lv_obj_add_event_cb(s_new_user_ta, params_on_ta_focused, LV_EVENT_FOCUSED, NULL);

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

    params_add_section_label(sc_players, "Statistiques");

    lv_obj_t *btn_reset = lv_btn_create(sc_players);
    lv_obj_set_size(btn_reset, 380, 46);
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0xD50000), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_reset, on_reset_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_reset = lv_label_create(btn_reset);
    lv_label_set_text(lbl_reset, LV_SYMBOL_SAVE "  Archiver et remettre a zero");
    lv_obj_center(lbl_reset);

    s_reset_status = lv_label_create(sc_players);
    lv_label_set_text(s_reset_status, "");
    lv_label_set_long_mode(s_reset_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_reset_status, SCREEN_INNER_W - 28);
}

void params_users_build_overlays(lv_obj_t *parent)
{
    (void)parent;
    s_delete_popup = lv_obj_create(params_screen);
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

    /* ── Archive + reset confirmation popup ─────────────────────────── */
    s_reset_popup = lv_obj_create(params_screen);
    lv_obj_set_size(s_reset_popup, 460, 230);
    lv_obj_align(s_reset_popup, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_bg_color(s_reset_popup, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_reset_popup, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_reset_popup, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_reset_popup, 16, LV_PART_MAIN);
    lv_obj_clear_flag(s_reset_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_reset_popup, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *reset_title = lv_label_create(s_reset_popup);
    lv_label_set_text(reset_title, LV_SYMBOL_WARNING "  Confirmation");
    lv_obj_set_style_text_color(reset_title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(reset_title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *reset_msg = lv_label_create(s_reset_popup);
    lv_label_set_text(reset_msg,
        "Les joueurs et leurs stats seront archives dans un fichier, "
        "puis la liste sera videe. Les reglages sont conserves.");
    lv_obj_set_width(reset_msg, 400);
    lv_obj_set_style_text_align(reset_msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(reset_msg, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_label_set_long_mode(reset_msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(reset_msg, LV_ALIGN_TOP_MID, 0, 42);

    lv_obj_t *btn_reset_no = lv_btn_create(s_reset_popup);
    lv_obj_set_size(btn_reset_no, 150, 42);
    lv_obj_align(btn_reset_no, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_reset_no, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_reset_no, on_reset_cancelled, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_reset_no = lv_label_create(btn_reset_no);
    lv_label_set_text(lbl_reset_no, LV_SYMBOL_CLOSE "  Annuler");
    lv_obj_center(lbl_reset_no);

    lv_obj_t *btn_reset_yes = lv_btn_create(s_reset_popup);
    lv_obj_set_size(btn_reset_yes, 190, 42);
    lv_obj_align(btn_reset_yes, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_reset_yes, lv_color_hex(0xD50000), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_reset_yes, on_reset_confirmed, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_reset_yes = lv_label_create(btn_reset_yes);
    lv_label_set_text(lbl_reset_yes, LV_SYMBOL_SAVE "  Archiver");
    lv_obj_center(lbl_reset_yes);

    /* ── RFID enrollment popup ──────────────────────────────────────── */
    s_params_rfid_popup = lv_obj_create(params_screen);
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

void params_users_refresh(void)
{
    char b1[192], b2[192], b3[192];
    db_get_text_config("home_banner_text_1", b1, sizeof(b1),
        "L'abus d'eau est dangereux pour la sante.");
    db_get_text_config("home_banner_text_2", b2, sizeof(b2), "");
    db_get_text_config("home_banner_text_3", b3, sizeof(b3), "");
    lv_textarea_set_text(s_banner_ta_1, b1);
    lv_textarea_set_text(s_banner_ta_2, b2);
    lv_textarea_set_text(s_banner_ta_3, b3);

    lv_label_set_text(s_reset_status, "");
    params_users_refresh_list();
}

void params_users_save(void)
{
    db_set_text_config("home_banner_text_1", lv_textarea_get_text(s_banner_ta_1));
    db_set_text_config("home_banner_text_2", lv_textarea_get_text(s_banner_ta_2));
    db_set_text_config("home_banner_text_3", lv_textarea_get_text(s_banner_ta_3));
}

void params_users_leave(void)
{
    /* An enrollment window left open would keep claiming RFID scans for a
     * screen nobody is looking at. */
    if (s_params_rfid_timer) {
        lv_timer_delete(s_params_rfid_timer);
        s_params_rfid_timer = NULL;
    }
    screen_manager_rfid_enroll_cancel();
    s_params_rfid_enroll_uid = -1;

    if (s_params_rfid_popup)
        lv_obj_add_flag(s_params_rfid_popup, LV_OBJ_FLAG_HIDDEN);
    if (s_delete_popup)
        lv_obj_add_flag(s_delete_popup, LV_OBJ_FLAG_HIDDEN);
    if (s_reset_popup)
        lv_obj_add_flag(s_reset_popup, LV_OBJ_FLAG_HIDDEN);

    s_pending_delete_uid     = -1;
    s_pending_delete_name[0] = '\0';
}
