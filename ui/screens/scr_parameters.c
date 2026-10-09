#include "params_internal.h"
#include "scr_parameters.h"
#include "screen_manager.h"
#include "game_db.h"
#include "serial_comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parametres screen shell.
 *
 * Owns the PIN gate, the tab bar, the shared layout helpers and the keyboard
 * overlay, and drives each tab through the build / refresh / save / leave
 * entry points declared in params_internal.h.  The tabs themselves live in
 * params_game.c, params_users.c, params_purge.c, params_calib.c and
 * params_cocktails.c.
 *
 * All of that used to be one 1976-line file. */

static const char k_correct_pin[]     = "2106";

/* Shared with the tab files - see params_internal.h. */
lv_obj_t *params_screen;
lv_obj_t *params_tabview;
lv_obj_t *params_kb;
int       params_pump_count = 2;

/* PIN phase */
static lv_obj_t *s_pin_panel;
static lv_obj_t *s_pin_display;   /* shows ----  /  ****  /  FAUX */

/* Config phase */
static lv_obj_t *s_cfg_panel;

/* PIN state */
static char s_pin_buf[5] = {'\0'};
static int  s_pin_len    = 0;

static void show_config_phase(void);
static void on_numpad_clicked(lv_event_t *e);
static void on_kb_event(lv_event_t *e);
static void on_save_clicked(lv_event_t *e);
static void on_back_clicked(lv_event_t *e);

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

/* ── shared layout helpers ──────────────────────────────────────────────── */

void params_add_section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), LV_PART_MAIN);
}

lv_obj_t *params_make_tab_scroll(lv_obj_t *tab)
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

/* ── keyboard ───────────────────────────────────────────────────────────── */

void params_on_ta_focused(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(params_kb, ta);
    lv_obj_clear_flag(params_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(params_kb);
}

static void on_kb_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
        lv_obj_add_flag(params_kb, LV_OBJ_FLAG_HIDDEN);
}

/* ── phase switching ────────────────────────────────────────────────────── */

/* Hand control to the tabs: each loads its own state from the DB instead of
 * this function reaching into every tab's widgets. */
static void show_config_phase(void)
{
    params_pump_count = db_get_config("pump_count", 2);
    if (params_pump_count < 2) params_pump_count = 2;
    if (params_pump_count > GAME_DB_MAX_PUMPS) params_pump_count = GAME_DB_MAX_PUMPS;

    params_game_refresh();
    params_calib_refresh();
    params_purge_refresh();
    params_users_refresh();
    params_cocktails_refresh();

    lv_obj_add_flag(s_pin_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_cfg_panel, LV_OBJ_FLAG_HIDDEN);
}

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

static void on_save_clicked(lv_event_t *e)
{
    (void)e;
    params_game_save();
    params_calib_save();
    params_users_save();
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    /* Cleanup happens in scr_parameters_leave(), which the screen manager
     * calls on the way out. */
    screen_manager_load(SCREEN_HOME);
}

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_parameters_init(void)
{
    params_screen = lv_obj_create(NULL);
    lv_obj_set_size(params_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(params_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* ━━━━━━━━━━ PIN PANEL ━━━━━━━━━━ */
    s_pin_panel = lv_obj_create(params_screen);
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
    s_cfg_panel = lv_obj_create(params_screen);
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
    params_tabview = lv_tabview_create(s_cfg_panel);
    lv_tabview_set_tab_bar_position(params_tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(params_tabview, 42);
    lv_obj_set_size(params_tabview, SCREEN_W, SCREEN_H - 36 - 56);
    lv_obj_align(params_tabview, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_color(params_tabview, lv_color_hex(0x16213E), LV_PART_MAIN);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(params_tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, LV_PART_MAIN);


    lv_obj_t *tab_game = lv_tabview_add_tab(params_tabview,
                                            LV_SYMBOL_SETTINGS "  Jeu");
    lv_obj_set_style_bg_color(tab_game, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_game, 0, LV_PART_MAIN);
    params_game_build(tab_game);

    lv_obj_t *tab_players = lv_tabview_add_tab(params_tabview,
                                               LV_SYMBOL_LIST "  Joueurs");
    lv_obj_set_style_bg_color(tab_players, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_players, 0, LV_PART_MAIN);
    params_users_build(tab_players);

    lv_obj_t *tab_purge = lv_tabview_add_tab(params_tabview,
                                             LV_SYMBOL_REFRESH "  Purge");
    params_purge_build(tab_purge);

    lv_obj_t *tab_calib = lv_tabview_add_tab(params_tabview,
                                             LV_SYMBOL_EDIT "  Calibration");
    params_calib_build(tab_calib);

    lv_obj_t *tab_ck = lv_tabview_add_tab(params_tabview,
                                          LV_SYMBOL_LIST "  Cocktails");
    lv_obj_set_style_bg_color(tab_ck, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_ck, 0, LV_PART_MAIN);
    params_cocktails_build(tab_ck);

    /* ── Keyboard overlay ──────────────────────────────────────────────── */
    params_kb = lv_keyboard_create(params_screen);
    lv_obj_set_size(params_kb, SCREEN_W, 220);
    lv_obj_align(params_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(params_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(params_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(params_kb, on_kb_event, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(params_kb, on_kb_event, LV_EVENT_CANCEL, NULL);

    params_users_build_overlays(params_screen);
}

lv_obj_t *scr_parameters_get(void) { return params_screen; }

void scr_parameters_unlock_admin(void)
{
    show_config_phase();
}

void scr_parameters_leave(void)
{
    /* Anything still driving a pump must be shut down before this screen's
     * controls go off-screen. */
    params_purge_leave();
    params_calib_leave();
    params_users_leave();

    if (params_kb)
        lv_obj_add_flag(params_kb, LV_OBJ_FLAG_HIDDEN);
}

void scr_parameters_refresh(void)
{
    scr_parameters_leave();

    /* Always reset to PIN phase on entry */
    s_pin_len = 0;
    memset(s_pin_buf, 0, sizeof(s_pin_buf));
    update_pin_display();

    lv_obj_add_flag(params_kb,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_cfg_panel,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_pin_panel, LV_OBJ_FLAG_HIDDEN);
}
