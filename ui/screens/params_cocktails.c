#include "params_internal.h"
#include "scr_parameters.h"
#include "screen_manager.h"
#include "game_db.h"
#include "serial_comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parametres > Cocktails: recipe CRUD, one quantity row per pump. */

static lv_obj_t   *s_ck_list;
static lv_obj_t   *s_ck_new_ta;
static lv_obj_t   *s_ck_editor;
static lv_obj_t   *s_ck_editor_title;
static lv_obj_t   *s_ck_pump_row[GAME_DB_MAX_PUMPS];
static lv_obj_t   *s_ck_mode_dd[GAME_DB_MAX_PUMPS];
static lv_obj_t   *s_ck_value_ta[GAME_DB_MAX_PUMPS];
static lv_obj_t   *s_ck_status_lbl;
static int         s_ck_selected_id = -1;


static void on_ck_select(lv_event_t *e);
static void on_ck_delete(lv_event_t *e);
static void on_ck_add_clicked(lv_event_t *e);
static void on_ck_mode_changed(lv_event_t *e);
static void on_ck_save_clicked(lv_event_t *e);

static void on_ck_select(lv_event_t *e)
{
    params_cocktails_open_editor((int)(intptr_t)lv_event_get_user_data(e));
}

static void on_ck_delete(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    db_delete_cocktail(id);
    if (s_ck_selected_id == id)
        params_cocktails_close_editor();
    params_cocktails_refresh_list();
}

void params_cocktails_refresh_list(void)
{
    if (!s_ck_list) return;
    lv_obj_clean(s_ck_list);

    cocktail_record_t cks[GAME_DB_MAX_COCKTAILS];
    int count = db_get_all_cocktails(cks, GAME_DB_MAX_COCKTAILS);

    for (int i = 0; i < count; i++) {
        lv_obj_t *row = lv_obj_create(s_ck_list);
        lv_obj_set_size(row, SCREEN_INNER_W - 28, 52);
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

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, cks[i].name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
        lv_obj_set_flex_grow(lbl, 1);

        lv_obj_t *edit_btn = lv_btn_create(row);
        lv_obj_set_size(edit_btn, 140, 40);
        lv_obj_set_style_bg_color(edit_btn, lv_color_hex(0x0F3460), LV_PART_MAIN);
        lv_obj_set_style_radius(edit_btn, 6, LV_PART_MAIN);
        lv_obj_add_event_cb(edit_btn, on_ck_select, LV_EVENT_CLICKED,
                            (void *)(intptr_t)cks[i].id);
        lv_obj_t *elbl = lv_label_create(edit_btn);
        lv_label_set_text(elbl, LV_SYMBOL_EDIT "  Recette");
        lv_obj_center(elbl);

        lv_obj_t *del = lv_btn_create(row);
        lv_obj_set_size(del, 60, 40);
        lv_obj_set_style_bg_color(del, lv_color_hex(0xD50000), LV_PART_MAIN);
        lv_obj_add_event_cb(del, on_ck_delete, LV_EVENT_CLICKED,
                            (void *)(intptr_t)cks[i].id);
        lv_obj_t *dl = lv_label_create(del);
        lv_label_set_text(dl, LV_SYMBOL_CLOSE);
        lv_obj_center(dl);
    }
}

void params_cocktails_close_editor(void)
{
    s_ck_selected_id = -1;
    if (s_ck_editor)
        lv_obj_add_flag(s_ck_editor, LV_OBJ_FLAG_HIDDEN);
}

void params_cocktails_open_editor(int cocktail_id)
{
    if (!s_ck_editor) return;
    s_ck_selected_id = cocktail_id;

    cocktail_record_t cks[GAME_DB_MAX_COCKTAILS];
    int count = db_get_all_cocktails(cks, GAME_DB_MAX_COCKTAILS);
    const char *name = "";
    for (int i = 0; i < count; i++)
        if (cks[i].id == cocktail_id) { name = cks[i].name; break; }
    char title[80];
    lv_snprintf(title, sizeof(title), "Recette: %s", name);
    lv_label_set_text(s_ck_editor_title, title);

    /* Reset all rows, show only the active pumps. */
    for (int i = 0; i < GAME_DB_MAX_PUMPS; i++) {
        if (!s_ck_pump_row[i]) continue;
        if (i < params_pump_count)
            lv_obj_clear_flag(s_ck_pump_row[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_ck_pump_row[i], LV_OBJ_FLAG_HIDDEN);
        lv_dropdown_set_selected(s_ck_mode_dd[i], 0);
        lv_textarea_set_text(s_ck_value_ta[i], "");
        lv_obj_add_state(s_ck_value_ta[i], LV_STATE_DISABLED);
    }

    cocktail_pump_t pumps[GAME_DB_MAX_PUMPS];
    int pn = db_get_cocktail_pumps(cocktail_id, pumps, GAME_DB_MAX_PUMPS);
    for (int i = 0; i < pn; i++) {
        int idx0 = pumps[i].pump_index - 1;
        if (idx0 < 0 || idx0 >= params_pump_count) continue;
        int dd = pumps[i].mode + 1; /* CL->1, PERCENT->2, WHEEL->3 */
        if (dd < 1 || dd > 3) dd = 1;
        lv_dropdown_set_selected(s_ck_mode_dd[idx0], dd);
        if (pumps[i].mode != COCKTAIL_QTY_WHEEL) {
            char vb[16];
            lv_snprintf(vb, sizeof(vb), "%d", pumps[i].value);
            lv_textarea_set_text(s_ck_value_ta[idx0], vb);
            lv_obj_clear_state(s_ck_value_ta[idx0], LV_STATE_DISABLED);
        }
    }

    lv_label_set_text(s_ck_status_lbl, "");
    lv_obj_clear_flag(s_ck_editor, LV_OBJ_FLAG_HIDDEN);
}

static void on_ck_add_clicked(lv_event_t *e)
{
    (void)e;
    const char *name = lv_textarea_get_text(s_ck_new_ta);
    if (!name || name[0] == '\0')
        return;
    int id = db_add_cocktail(name);
    lv_textarea_set_text(s_ck_new_ta, "");
    lv_obj_add_flag(params_kb, LV_OBJ_FLAG_HIDDEN);
    params_cocktails_refresh_list();
    if (id > 0)
        params_cocktails_open_editor(id);
}

static void on_ck_mode_changed(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= GAME_DB_MAX_PUMPS) return;

    int sel = lv_dropdown_get_selected(dd);
    /* Value input is only meaningful for cL (1) and percent (2). */
    if (sel == 1 || sel == 2)
        lv_obj_clear_state(s_ck_value_ta[idx], LV_STATE_DISABLED);
    else
        lv_obj_add_state(s_ck_value_ta[idx], LV_STATE_DISABLED);
}

static void on_ck_save_clicked(lv_event_t *e)
{
    (void)e;
    if (s_ck_selected_id < 0)
        return;

    cocktail_pump_t pumps[GAME_DB_MAX_PUMPS];
    int n = 0;
    for (int i = 0; i < params_pump_count; i++) {
        int dd = lv_dropdown_get_selected(s_ck_mode_dd[i]);
        if (dd == 0)
            continue; /* Aucun -> pump not used */
        int mode = dd - 1;
        int value = 0;
        if (mode != COCKTAIL_QTY_WHEEL) {
            const char *vt = lv_textarea_get_text(s_ck_value_ta[i]);
            /* An empty/zero cL or % field would silently dispense nothing for this pump. */
            if (!vt || !vt[0] || atoi(vt) <= 0) {
                char msg[64];
                lv_snprintf(msg, sizeof(msg),
                            LV_SYMBOL_WARNING "  Valeur manquante (pompe %d)", i + 1);
                lv_label_set_text(s_ck_status_lbl, msg);
                lv_obj_set_style_text_color(s_ck_status_lbl,
                                            lv_color_hex(0xE94560), LV_PART_MAIN);
                return;
            }
            value = atoi(vt);
        }
        pumps[n].pump_index = i + 1;
        pumps[n].mode = mode;
        pumps[n].value = value;
        n++;
    }

    if (db_set_cocktail_pumps(s_ck_selected_id, pumps, n) == 0) {
        lv_label_set_text(s_ck_status_lbl, LV_SYMBOL_OK "  Recette enregistree");
        lv_obj_set_style_text_color(s_ck_status_lbl,
                                    lv_color_hex(0x00C853), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_ck_status_lbl, LV_SYMBOL_WARNING "  Erreur d'enregistrement");
        lv_obj_set_style_text_color(s_ck_status_lbl,
                                    lv_color_hex(0xE94560), LV_PART_MAIN);
    }
}

void params_cocktails_build(lv_obj_t *tab)
{
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);

    lv_obj_t *sc_ck = params_make_tab_scroll(tab);

    params_add_section_label(sc_ck, "Nouveau cocktail");

    lv_obj_t *ck_add_row = lv_obj_create(sc_ck);
    lv_obj_set_size(ck_add_row, SCREEN_INNER_W - 28, 54);
    lv_obj_set_style_bg_opa(ck_add_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ck_add_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ck_add_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ck_add_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ck_add_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ck_add_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(ck_add_row, 10, LV_PART_MAIN);

    s_ck_new_ta = lv_textarea_create(ck_add_row);
    lv_obj_set_size(s_ck_new_ta, 580, 46);
    lv_textarea_set_one_line(s_ck_new_ta, true);
    lv_textarea_set_placeholder_text(s_ck_new_ta, "Nom du cocktail...");
    lv_obj_add_event_cb(s_ck_new_ta, params_on_ta_focused, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *ck_add_btn = lv_btn_create(ck_add_row);
    lv_obj_set_size(ck_add_btn, 200, 46);
    lv_obj_set_style_bg_color(ck_add_btn, lv_color_hex(0x00C853), LV_PART_MAIN);
    lv_obj_add_event_cb(ck_add_btn, on_ck_add_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ck_add_lbl = lv_label_create(ck_add_btn);
    lv_label_set_text(ck_add_lbl, LV_SYMBOL_PLUS "  Ajouter");
    lv_obj_center(ck_add_lbl);

    params_add_section_label(sc_ck, "Cocktails");

    s_ck_list = lv_obj_create(sc_ck);
    lv_obj_set_size(s_ck_list, SCREEN_INNER_W - 28, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_ck_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ck_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ck_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_ck_list, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_ck_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_ck_list, LV_OBJ_FLAG_SCROLLABLE);

    /* Recipe editor (hidden until a cocktail is selected) */
    s_ck_editor = lv_obj_create(sc_ck);
    lv_obj_set_size(s_ck_editor, SCREEN_INNER_W - 28, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_ck_editor, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ck_editor, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ck_editor, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ck_editor, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_ck_editor, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_ck_editor, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_ck_editor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ck_editor, LV_OBJ_FLAG_HIDDEN);

    s_ck_editor_title = lv_label_create(s_ck_editor);
    lv_label_set_text(s_ck_editor_title, "Recette");
    lv_obj_set_style_text_color(s_ck_editor_title, lv_color_hex(0xF5C518), LV_PART_MAIN);

    lv_obj_t *ck_hint = lv_label_create(s_ck_editor);
    lv_label_set_text(ck_hint,
        "Mode par pompe: Aucun (exclue), cL, % du verre, ou Roue (quantite de la roue).");
    lv_obj_set_style_text_color(ck_hint, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_label_set_long_mode(ck_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ck_hint, SCREEN_INNER_W - 60);

    for (int i = 0; i < GAME_DB_MAX_PUMPS; i++) {
        s_ck_pump_row[i] = lv_obj_create(s_ck_editor);
        lv_obj_set_size(s_ck_pump_row[i], SCREEN_INNER_W - 60, 52);
        lv_obj_set_style_bg_opa(s_ck_pump_row[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_ck_pump_row[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_ck_pump_row[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_gap(s_ck_pump_row[i], 10, LV_PART_MAIN);
        lv_obj_clear_flag(s_ck_pump_row[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(s_ck_pump_row[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s_ck_pump_row[i], LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        char plbl[16];
        lv_snprintf(plbl, sizeof(plbl), "Pompe %d", i + 1);
        lv_obj_t *pl = lv_label_create(s_ck_pump_row[i]);
        lv_label_set_text(pl, plbl);
        lv_obj_set_style_text_color(pl, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
        lv_obj_set_width(pl, 120);

        s_ck_mode_dd[i] = lv_dropdown_create(s_ck_pump_row[i]);
        lv_dropdown_set_options(s_ck_mode_dd[i], "Aucun\ncL\n% verre\nRoue");
        lv_obj_set_width(s_ck_mode_dd[i], 200);
        lv_obj_add_event_cb(s_ck_mode_dd[i], on_ck_mode_changed,
                            LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);

        s_ck_value_ta[i] = lv_textarea_create(s_ck_pump_row[i]);
        lv_obj_set_size(s_ck_value_ta[i], 160, 44);
        lv_textarea_set_one_line(s_ck_value_ta[i], true);
        lv_textarea_set_placeholder_text(s_ck_value_ta[i], "cL / %");
        lv_obj_add_event_cb(s_ck_value_ta[i], params_on_ta_focused, LV_EVENT_FOCUSED, NULL);
    }

    lv_obj_t *ck_save_btn = lv_btn_create(s_ck_editor);
    lv_obj_set_size(ck_save_btn, 220, 48);
    lv_obj_set_style_bg_color(ck_save_btn, lv_color_hex(0x00C853), LV_PART_MAIN);
    lv_obj_set_style_radius(ck_save_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(ck_save_btn, on_ck_save_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ck_save_lbl = lv_label_create(ck_save_btn);
    lv_label_set_text(ck_save_lbl, LV_SYMBOL_SAVE "  Enregistrer la recette");
    lv_obj_center(ck_save_lbl);

    s_ck_status_lbl = lv_label_create(s_ck_editor);
    lv_label_set_text(s_ck_status_lbl, "");
    lv_obj_set_style_text_color(s_ck_status_lbl, lv_color_hex(0x888888), LV_PART_MAIN);

}

void params_cocktails_refresh(void)
{
    params_cocktails_refresh_list();
    params_cocktails_close_editor();
}

void params_cocktails_reopen_selected(void)
{
    if (s_ck_selected_id >= 0)
        params_cocktails_open_editor(s_ck_selected_id);
    else
        params_cocktails_close_editor();
}
