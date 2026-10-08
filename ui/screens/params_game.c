#include "params_internal.h"
#include "scr_parameters.h"
#include "screen_manager.h"
#include "game_db.h"
#include "serial_comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parametres > Jeu: the game-rule sliders, plus the pump-count slider that the
 * Purge, Calibration and Cocktails tabs size themselves from.
 *
 * The slider row builder lives here because this is the only tab with
 * sliders, and its value-changed handler only refreshes these labels. */

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
static lv_obj_t   *s_sld_pump_count;
static lv_obj_t   *s_lbl_pump_count;

static void on_slider_changed(lv_event_t *e);
static void on_pump_count_changed(lv_event_t *e);
static void update_slider_labels(void);

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

    if (s_sld_pump_count) {
        lv_snprintf(buf, sizeof(buf), "%d", lv_slider_get_value(s_sld_pump_count));
        lv_label_set_text(s_lbl_pump_count, buf);
    }
}

static void on_slider_changed(lv_event_t *e)
{
    (void)e;
    update_slider_labels();
}

/* Changing the pump count resizes three other tabs, so they are rebuilt from
 * here rather than each polling for the change. */
static void on_pump_count_changed(lv_event_t *e)
{
    (void)e;
    int newc = lv_slider_get_value(s_sld_pump_count);
    if (newc < 2) newc = 2;
    if (newc > GAME_DB_MAX_PUMPS) newc = GAME_DB_MAX_PUMPS;

    int previous = params_pump_count;
    params_pump_count = newc;

    params_calib_ensure_flows(previous);
    params_calib_rebuild_pump_btns();
    params_purge_rebuild_pump_btns();
    update_slider_labels();
    params_calib_update_ui();
    params_cocktails_reopen_selected();
}

void params_game_build(lv_obj_t *tab)
{
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);

    lv_obj_t *sc_game = params_make_tab_scroll(tab);

    params_add_section_label(sc_game, "Pompes");
    create_slider_row(sc_game, "Nombre de pompes", 2, GAME_DB_MAX_PUMPS,
                      &s_sld_pump_count, &s_lbl_pump_count);
    lv_obj_add_event_cb(s_sld_pump_count, on_pump_count_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
    {
        lv_obj_t *hint = lv_label_create(sc_game);
        lv_label_set_text(hint,
            "Pompe 1 = debitmetre (pin 7). Pompes 2+ = temporisees.\n"
            "Broches: P1=7  P2=6  P3=5  P4=8  P5=9  P6=3");
        lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, SCREEN_INNER_W - 28);
    }

    params_add_section_label(sc_game, "Chances");
    create_slider_row(sc_game, "Chance roue bonus (%)", 0, 100,
                      &s_sld_trigger, &s_lbl_trigger);

    params_add_section_label(sc_game, "Poids roue bonus");
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

    params_add_section_label(sc_game, "Timeout");
    create_slider_row(sc_game, "Duree timeout (min)", 1, 60,
                      &s_sld_timeout_mins, &s_lbl_timeout_mins);

    params_add_section_label(sc_game, "Limites");
    create_slider_row(sc_game, "Max bonus par joueur",    1, 20,
                      &s_sld_max_bonus, &s_lbl_max_bonus);
    create_slider_row(sc_game, "Max malus par joueur",    1, 20,
                      &s_sld_max_malus, &s_lbl_max_malus);
    create_slider_row(sc_game, "Cooldown entre tours (s)", 0, 300,
                      &s_sld_cooldown, &s_lbl_cooldown);

}

void params_game_refresh(void)
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
    lv_slider_set_value(s_sld_pump_count, params_pump_count, LV_ANIM_OFF);
    update_slider_labels();
}

void params_game_save(void)
{
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

    db_set_config("pump_count", params_pump_count);
}
