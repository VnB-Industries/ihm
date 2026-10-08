#include "params_internal.h"
#include "scr_parameters.h"
#include "screen_manager.h"
#include "game_db.h"
#include "serial_comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parametres > Purge: run the pumps for a fixed time to prime or flush a line.
 *
 * The duration is enforced by the firmware, which ends the run on its own.
 * This tab only reflects progress and offers an early STOP; it is no longer
 * what keeps the pumps from running on, because a UI that must stay alive to
 * shut a pump off is a UI whose crash leaves the pump running. */

static const uint32_t k_purge_duration_ms = 10000; /* 10 s */
static const uint32_t k_purge_tick_ms     = 100;   /* timer period */
static lv_obj_t  *s_purge_sel_row;                         /* container for pump buttons */
static lv_obj_t  *s_purge_pump_btn[GAME_DB_MAX_PUMPS + 1]; /* one per pump + "Tous" */
static lv_obj_t  *s_purge_start_btn;
static lv_obj_t  *s_purge_start_lbl;
static lv_obj_t  *s_purge_bar;
static lv_obj_t  *s_purge_lbl_countdown;
static lv_obj_t  *s_purge_lbl_status;
static lv_timer_t *s_purge_timer;
static uint32_t    s_purge_elapsed_ms;
static int         s_purge_pump_sel;     /* 0..N-1 single pump, N = all */

static void on_purge_timer(lv_timer_t *t);
static void on_purge_pump_sel(lv_event_t *e);
static void on_purge_start_clicked(lv_event_t *e);
static void purge_reset_ui(void);
static void purge_set_status(const char *text, uint32_t color);

/* Number of purge selection buttons: one per pump plus a "Tous" button. */
static int purge_btn_count(void)
{
    return params_pump_count + 1;
}

static void update_purge_pump_btns(void)
{
    int n = purge_btn_count();
    for (int i = 0; i < n; i++) {
        if (!s_purge_pump_btn[i]) continue;
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

/* Recreate the purge pump-selection buttons for the current pump count. */
void params_purge_rebuild_pump_btns(void)
{
    if (!s_purge_sel_row) return;
    lv_obj_clean(s_purge_sel_row);

    int n = purge_btn_count();
    if (s_purge_pump_sel >= n) s_purge_pump_sel = n - 1; /* default: "Tous" */

    for (int i = 0; i < n; i++) {
        char txt[24];
        if (i < params_pump_count)
            lv_snprintf(txt, sizeof(txt), LV_SYMBOL_DOWNLOAD "  Pompe %d", i + 1);
        else
            lv_snprintf(txt, sizeof(txt), LV_SYMBOL_DOWNLOAD "  Toutes");

        s_purge_pump_btn[i] = lv_btn_create(s_purge_sel_row);
        lv_obj_set_size(s_purge_pump_btn[i], 150, 44);
        lv_obj_set_style_radius(s_purge_pump_btn[i], 8, LV_PART_MAIN);
        lv_obj_add_event_cb(s_purge_pump_btn[i], on_purge_pump_sel,
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl = lv_label_create(s_purge_pump_btn[i]);
        lv_label_set_text(lbl, txt);
        lv_obj_center(lbl);
    }
    update_purge_pump_btns();
}

static void on_purge_pump_sel(lv_event_t *e)
{
    s_purge_pump_sel = (int)(intptr_t)lv_event_get_user_data(e);
    update_purge_pump_btns();
}

static void purge_set_status(const char *text, uint32_t color)
{
    lv_label_set_text(s_purge_lbl_status, text);
    lv_obj_set_style_text_color(s_purge_lbl_status, lv_color_hex(color), LV_PART_MAIN);
}

/* Return the controls to their idle look without touching the controller. */
static void purge_reset_ui(void)
{
    if (s_purge_timer) {
        lv_timer_delete(s_purge_timer);
        s_purge_timer = NULL;
    }

    lv_bar_set_value(s_purge_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(s_purge_lbl_countdown, "10.0 s");
    lv_label_set_text(s_purge_start_lbl, LV_SYMBOL_PLAY "  Demarrer");
    lv_obj_set_style_bg_color(s_purge_start_btn, lv_color_hex(0x00C853), LV_PART_MAIN);

    int n = purge_btn_count();
    for (int i = 0; i < n; i++)
        if (s_purge_pump_btn[i])
            lv_obj_clear_state(s_purge_pump_btn[i], LV_STATE_DISABLED);
}

void params_purge_stop(void)
{
    serial_comm_send_stop();
    if (serial_comm_job_active())
        serial_comm_job_clear();

    purge_reset_ui();
    purge_set_status("Pret", 0x888888);
}

/* Watch the purge job and animate the countdown.  The controller decides when
 * the run ends; this only reports it. */
static void on_purge_timer(lv_timer_t *t)
{
    (void)t;

    const serial_job_t *job = serial_comm_job();

    if (job->state == SERIAL_JOB_FAILED) {
        char code[sizeof(job->error)];
        snprintf(code, sizeof(code), "%s", job->error);
        serial_comm_job_clear();
        purge_reset_ui();

        if (strncmp(code, "NO_OBJECT", 9) == 0)
            purge_set_status(LV_SYMBOL_WARNING " Aucun verre detecte", 0xE94560);
        else
            purge_set_status(LV_SYMBOL_WARNING " Erreur controleur", 0xE94560);
        return;
    }

    if (job->state == SERIAL_JOB_DONE) {
        serial_comm_job_clear();
        purge_reset_ui();
        purge_set_status("Purge terminee", 0x00C853);
        return;
    }

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
}

static void on_purge_start_clicked(lv_event_t *e)
{
    (void)e;

    if (s_purge_timer) {
        purge_set_status("Arretee", 0xF5C518);
        params_purge_stop();
        return;
    }

    /* Pump 0 means "all" on the wire. */
    int pump_arg = (s_purge_pump_sel >= params_pump_count) ? 0 : s_purge_pump_sel + 1;

    if (!serial_comm_begin_purge(pump_arg, k_purge_duration_ms)) {
        purge_set_status(LV_SYMBOL_WARNING " Erreur serie", 0xE94560);
        serial_comm_job_clear();
        return;
    }

    int nbtn = purge_btn_count();
    for (int i = 0; i < nbtn; i++)
        if (s_purge_pump_btn[i])
            lv_obj_add_state(s_purge_pump_btn[i], LV_STATE_DISABLED);

    lv_label_set_text(s_purge_start_lbl, LV_SYMBOL_STOP "  Arreter");
    lv_obj_set_style_bg_color(s_purge_start_btn, lv_color_hex(0xD50000), LV_PART_MAIN);
    purge_set_status("Purge en cours...", 0x00C853);

    s_purge_elapsed_ms = 0;
    s_purge_timer = lv_timer_create(on_purge_timer, k_purge_tick_ms, NULL);
}

void params_purge_build(lv_obj_t *tab)
{
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab, 20, LV_PART_MAIN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *purge_desc = lv_label_create(tab);
    lv_label_set_text(purge_desc,
        "Lance les pompes pendant 10 secondes pour nettoyer les tuyaux.");
    lv_obj_set_style_text_color(purge_desc, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_label_set_long_mode(purge_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(purge_desc, SCREEN_W - 80);
    lv_obj_align(purge_desc, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Pump selection (buttons built dynamically per pump count) */
    s_purge_pump_sel = GAME_DB_MAX_PUMPS; /* default: all */

    s_purge_sel_row = lv_obj_create(tab);
    lv_obj_set_size(s_purge_sel_row, SCREEN_W - 80, 48);
    lv_obj_align(s_purge_sel_row, LV_ALIGN_TOP_LEFT, 0, 48);
    lv_obj_set_style_bg_opa(s_purge_sel_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_purge_sel_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_purge_sel_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_purge_sel_row, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_purge_sel_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_purge_sel_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_purge_sel_row, LV_OBJ_FLAG_SCROLLABLE);
    params_purge_rebuild_pump_btns();

    /* Progress bar */
    s_purge_bar = lv_bar_create(tab);
    lv_obj_set_size(s_purge_bar, SCREEN_W - 80, 20);
    lv_obj_align(s_purge_bar, LV_ALIGN_TOP_LEFT, 0, 116);
    lv_bar_set_range(s_purge_bar, 0, 100);
    lv_bar_set_value(s_purge_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_purge_bar, lv_color_hex(0x2A2A4A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_purge_bar, lv_color_hex(0x00C853), LV_PART_INDICATOR);

    /* Countdown label */
    s_purge_lbl_countdown = lv_label_create(tab);
    lv_label_set_text(s_purge_lbl_countdown, "10.0 s");
    lv_obj_set_style_text_color(s_purge_lbl_countdown,
                                lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(s_purge_lbl_countdown, LV_ALIGN_TOP_RIGHT, 0, 148);

    /* Status label */
    s_purge_lbl_status = lv_label_create(tab);
    lv_label_set_text(s_purge_lbl_status, "Pret");
    lv_obj_set_style_text_color(s_purge_lbl_status,
                                lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(s_purge_lbl_status, LV_ALIGN_TOP_LEFT, 0, 148);

    /* Start / Stop button */
    s_purge_start_btn = lv_btn_create(tab);
    lv_obj_set_size(s_purge_start_btn, 240, 60);
    lv_obj_align(s_purge_start_btn, LV_ALIGN_TOP_LEFT, 0, 196);
    lv_obj_set_style_bg_color(s_purge_start_btn, lv_color_hex(0x00C853), LV_PART_MAIN);
    lv_obj_set_style_radius(s_purge_start_btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(s_purge_start_btn, on_purge_start_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_purge_start_lbl = lv_label_create(s_purge_start_btn);
    lv_label_set_text(s_purge_start_lbl, LV_SYMBOL_PLAY "  Demarrer");
    lv_obj_center(s_purge_start_lbl);

}

void params_purge_refresh(void)
{
    params_purge_rebuild_pump_btns();
    purge_reset_ui();
    purge_set_status("Pret", 0x888888);
}

void params_purge_leave(void)
{
    if (s_purge_timer)
        params_purge_stop();
}
