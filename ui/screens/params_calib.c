#include "params_internal.h"
#include "scr_parameters.h"
#include "screen_manager.h"
#include "game_db.h"
#include "serial_comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parametres > Calibration: measure one pump's flow constant.
 *
 * The run ends when the glass is lifted, which the firmware detects; the
 * firmware also bounds the run itself now, so a stuck or unplugged IR sensor
 * cannot leave a pump running indefinitely. */

static const uint32_t k_calib_tick_ms     = 100;

static const char k_cfg_pump1_flow[] = "pump1_pulses_per_cl_x1000";
static const int k_default_pump1_flow_scaled = 540420;
static const int k_default_pump2_flow_scaled = 2800;
static lv_obj_t   *s_calib_sel_row;                     /* container for pump buttons */
static lv_obj_t   *s_calib_pump_btn[GAME_DB_MAX_PUMPS];
static lv_obj_t   *s_calib_qty_ta;
static lv_obj_t   *s_calib_start_btn;
static lv_obj_t   *s_calib_start_lbl;
static lv_obj_t   *s_calib_reset_btn;
static lv_obj_t   *s_calib_status_lbl;
static lv_obj_t   *s_calib_progress_lbl;
static lv_obj_t   *s_calib_constant_lbl;
static lv_timer_t *s_calib_timer;
static bool        s_calib_running;
static int         s_calib_pump_sel;      /* 0-based pump index */
static float       s_calib_flow[GAME_DB_MAX_PUMPS]; /* [0]=pulses/cL, others cL/s */

static void on_calib_timer(lv_timer_t *t);
static void on_calib_pump_sel(lv_event_t *e);
static void on_calib_reset_clicked(lv_event_t *e);
static void on_calib_start_clicked(lv_event_t *e);
static void update_calibration_pump_btns(void);
static void calib_set_status(const char *text, uint32_t color);

int params_flow_float_to_scaled(float value)
{
    if (value <= 0.0f) {
        return 0;
    }
    return (int)(value * 1000.0f + 0.5f);
}

float params_flow_scaled_to_float(int value, float fallback)
{
    if (value <= 0) {
        return fallback;
    }
    return value / 1000.0f;
}

/* Config key for a pump's flow constant. idx0==0 -> pump1 pulses/cL,
 * idx0>=1 -> pump N (=idx0+1) time-based cL/s. */
void params_calib_config_key(int idx0, char *buf, size_t n)
{
    if (idx0 == 0)
        lv_snprintf(buf, (uint32_t)n, "%s", k_cfg_pump1_flow);
    else
        lv_snprintf(buf, (uint32_t)n, "pump%d_flow_clps_x1000", idx0 + 1);
}

int params_calib_default_scaled(int idx0)
{
    return idx0 == 0 ? k_default_pump1_flow_scaled : k_default_pump2_flow_scaled;
}

float params_calib_default_flow(int idx0)
{
    return idx0 == 0 ? 540.42f : 2.8f;
}

void params_calib_save_flow(int idx0)
{
    char key[40];
    params_calib_config_key(idx0, key, sizeof(key));
    db_set_config(key, params_flow_float_to_scaled(s_calib_flow[idx0]));
}

bool params_calib_push_constants(void)
{
    float time_flows[GAME_DB_MAX_PUMPS - 1];
    int tc = params_pump_count - 1;
    if (tc < 0) tc = 0;
    for (int i = 0; i < tc; i++)
        time_flows[i] = s_calib_flow[i + 1];
    serial_comm_set_dispense_constants(s_calib_flow[0], time_flows, tc);
    return true;
}

static void update_calibration_pump_btns(void)
{
    for (int i = 0; i < params_pump_count; i++) {
        if (!s_calib_pump_btn[i]) continue;
        bool active = (i == s_calib_pump_sel);
        lv_obj_set_style_bg_color(s_calib_pump_btn[i],
            active ? lv_color_hex(0xF5C518) : lv_color_hex(0x2A2A4A),
            LV_PART_MAIN);
        lv_obj_t *lbl = lv_obj_get_child(s_calib_pump_btn[i], 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl,
                active ? lv_color_hex(0x1A1A2E) : lv_color_hex(0xEAEAEA),
                LV_PART_MAIN);
        }
    }
}

/* Recreate the calibration pump-selection buttons for the current pump count. */
void params_calib_rebuild_pump_btns(void)
{
    if (!s_calib_sel_row) return;
    lv_obj_clean(s_calib_sel_row);

    if (s_calib_pump_sel >= params_pump_count) s_calib_pump_sel = 0;

    for (int i = 0; i < params_pump_count; i++) {
        char txt[24];
        lv_snprintf(txt, sizeof(txt), LV_SYMBOL_DOWNLOAD "  Pompe %d", i + 1);
        s_calib_pump_btn[i] = lv_btn_create(s_calib_sel_row);
        lv_obj_set_size(s_calib_pump_btn[i], 150, 44);
        lv_obj_set_style_radius(s_calib_pump_btn[i], 8, LV_PART_MAIN);
        lv_obj_add_event_cb(s_calib_pump_btn[i], on_calib_pump_sel,
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl = lv_label_create(s_calib_pump_btn[i]);
        lv_label_set_text(lbl, txt);
        lv_obj_center(lbl);
    }
    update_calibration_pump_btns();
}

void params_calib_update_ui(void)
{
    char buf[128];
    int idx = s_calib_pump_sel;
    if (idx == 0)
        lv_snprintf(buf, sizeof(buf), "P1: %.3f pulses/cL (debitmetre)",
                    s_calib_flow[0]);
    else
        lv_snprintf(buf, sizeof(buf), "P%d: %.3f cL/s (temporise)",
                    idx + 1, s_calib_flow[idx]);
    lv_label_set_text(s_calib_constant_lbl, buf);

    if (s_calib_running) {
        lv_label_set_text(s_calib_start_lbl, LV_SYMBOL_STOP "  Arreter");
        lv_obj_set_style_bg_color(s_calib_start_btn, lv_color_hex(0xD50000), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_calib_start_lbl, LV_SYMBOL_PLAY "  Demarrer");
        lv_obj_set_style_bg_color(s_calib_start_btn, lv_color_hex(0x00C853), LV_PART_MAIN);
    }

    update_calibration_pump_btns();
}

void params_calib_stop_ui(bool send_stop)
{
    if (send_stop) {
        serial_comm_send_stop();
    }

    if (s_calib_timer) {
        lv_timer_delete(s_calib_timer);
        s_calib_timer = NULL;
    }

    s_calib_running = false;
    screen_manager_set_rfid_poll_enabled(true);

    for (int i = 0; i < params_pump_count; i++)
        if (s_calib_pump_btn[i])
            lv_obj_clear_state(s_calib_pump_btn[i], LV_STATE_DISABLED);
    lv_obj_clear_state(s_calib_qty_ta, LV_STATE_DISABLED);

    params_calib_update_ui();
}

static void on_calib_pump_sel(lv_event_t *e)
{
    if (s_calib_running) {
        return;
    }

    s_calib_pump_sel = (int)(intptr_t)lv_event_get_user_data(e);
    params_calib_update_ui();
}

static void on_calib_reset_clicked(lv_event_t *e)
{
    (void)e;

    int idx0 = s_calib_pump_sel;
    s_calib_flow[idx0] = params_flow_scaled_to_float(params_calib_default_scaled(idx0),
                                              params_calib_default_flow(idx0));
    params_calib_save_flow(idx0);

    if (params_calib_push_constants()) {
        lv_label_set_text(s_calib_status_lbl, "Valeur par defaut appliquee");
        lv_obj_set_style_text_color(s_calib_status_lbl,
                                    lv_color_hex(0x00C853), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_calib_status_lbl,
                          LV_SYMBOL_WARNING "  Defaut sauve, sync serie echouee");
        lv_obj_set_style_text_color(s_calib_status_lbl,
                                    lv_color_hex(0xF5C518), LV_PART_MAIN);
    }

    params_calib_update_ui();
}

static void calib_set_status(const char *text, uint32_t color)
{
    lv_label_set_text(s_calib_status_lbl, text);
    lv_obj_set_style_text_color(s_calib_status_lbl, lv_color_hex(color), LV_PART_MAIN);
}

void params_calib_ensure_flows(int previous_count)
{
    if (previous_count < 0) previous_count = 0;
    for (int i = previous_count; i < params_pump_count && i < GAME_DB_MAX_PUMPS; i++) {
        char key[40];
        params_calib_config_key(i, key, sizeof(key));
        s_calib_flow[i] = params_flow_scaled_to_float(
            db_get_config(key, params_calib_default_scaled(i)),
            params_calib_default_flow(i));
    }
}

/* Watch the calibration job.  Progress, the normal end (glass lifted) and
 * every failure mode now arrive as one job state, rather than as a stream of
 * separately parsed event records. */
static void on_calib_timer(lv_timer_t *t)
{
    (void)t;

    const serial_job_t *job = serial_comm_job();

    switch (job->state) {
        case SERIAL_JOB_PENDING:
            return;

        case SERIAL_JOB_RUNNING: {
            char buf[96];
            lv_snprintf(buf, sizeof(buf), "Progression: %.2f cL",
                        (double)job->progress_cl[0]);
            lv_label_set_text(s_calib_progress_lbl, buf);
            return;
        }

        case SERIAL_JOB_DONE: {
            bool  by_sensor = job->cal_stopped_by_sensor;
            int   idx0      = job->cal_pump_index - 1;
            float constant  = job->cal_constant;

            serial_comm_job_clear();

            if (!by_sensor) {
                /* Stopped by hand: the reading is not a calibration. */
                calib_set_status("Calibration arretee", 0xF5C518);
                params_calib_stop_ui(false);
                return;
            }

            if (idx0 < 0 || idx0 >= params_pump_count)
                idx0 = s_calib_pump_sel;
            s_calib_flow[idx0] = constant;
            params_calib_save_flow(idx0);

            if (params_calib_push_constants()) {
                calib_set_status(LV_SYMBOL_OK "  Calibration appliquee", 0x00C853);
            } else {
                calib_set_status(LV_SYMBOL_WARNING "  Valeur sauvee, sync serie echouee",
                                 0xF5C518);
            }

            params_calib_stop_ui(false);
            params_calib_update_ui();
            return;
        }

        case SERIAL_JOB_FAILED: {
            char code[sizeof(job->error)];
            snprintf(code, sizeof(code), "%s", job->error);
            serial_comm_job_clear();

            if (strncmp(code, "NO_OBJECT", 9) == 0)
                calib_set_status(LV_SYMBOL_WARNING "  Aucun verre detecte", 0xE94560);
            else if (strncmp(code, "CAL_TIMEOUT", 11) == 0)
                calib_set_status(LV_SYMBOL_WARNING "  Delai depasse, verifie le capteur",
                                 0xE94560);
            else
                calib_set_status(LV_SYMBOL_WARNING "  Echec calibration", 0xE94560);

            params_calib_stop_ui(false);
            return;
        }

        case SERIAL_JOB_IDLE:
        default:
            params_calib_stop_ui(false);
            return;
    }
}

static void on_calib_start_clicked(lv_event_t *e)
{
    (void)e;

    if (s_calib_running) {
        calib_set_status("Arret en cours...", 0xF5C518);
        serial_comm_send_stop();
        return;
    }

    const char *qty_txt = lv_textarea_get_text(s_calib_qty_ta);
    if (!qty_txt || qty_txt[0] == '\0') {
        calib_set_status("Entre une quantite en cL", 0xE94560);
        return;
    }

    char *endp = NULL;
    float qty_cl = strtof(qty_txt, &endp);
    if (endp == qty_txt || qty_cl <= 0.0f) {
        calib_set_status("Quantite invalide", 0xE94560);
        return;
    }

    int pump_index = s_calib_pump_sel + 1;
    if (!serial_comm_begin_calibration(pump_index, qty_cl)) {
        calib_set_status(LV_SYMBOL_WARNING "  Echec calibration", 0xE94560);
        serial_comm_job_clear();
        return;
    }

    /* RFID polling consumes from the same link; the job owns it until the run
     * ends. */
    screen_manager_set_rfid_poll_enabled(false);
    s_calib_running = true;

    for (int i = 0; i < params_pump_count; i++)
        if (s_calib_pump_btn[i])
            lv_obj_add_state(s_calib_pump_btn[i], LV_STATE_DISABLED);
    lv_obj_add_state(s_calib_qty_ta, LV_STATE_DISABLED);

    lv_label_set_text(s_calib_progress_lbl, "Progression: 0.00 cL");
    calib_set_status("Retire le verre quand la quantite est atteinte", 0x00C853);

    if (s_calib_timer) {
        lv_timer_delete(s_calib_timer);
    }
    s_calib_timer = lv_timer_create(on_calib_timer, k_calib_tick_ms, NULL);
    params_calib_update_ui();
}

void params_calib_build(lv_obj_t *tab)
{
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab, 20, LV_PART_MAIN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *calib_desc = lv_label_create(tab);
    lv_label_set_text(calib_desc,
        "Selectionne une pompe, entre la quantite cible (cL), puis retire le verre pour terminer.");
    lv_obj_set_style_text_color(calib_desc, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_label_set_long_mode(calib_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(calib_desc, SCREEN_W - 80);
    lv_obj_align(calib_desc, LV_ALIGN_TOP_LEFT, 0, 0);

    s_calib_pump_sel = 0;
    s_calib_running = false;
    s_calib_timer = NULL;
    s_calib_flow[0] = 540.42f;
    for (int i = 1; i < GAME_DB_MAX_PUMPS; i++)
        s_calib_flow[i] = 2.8f;

    s_calib_sel_row = lv_obj_create(tab);
    lv_obj_set_size(s_calib_sel_row, SCREEN_W - 80, 48);
    lv_obj_align(s_calib_sel_row, LV_ALIGN_TOP_LEFT, 0, 56);
    lv_obj_set_style_bg_opa(s_calib_sel_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_calib_sel_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_calib_sel_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_calib_sel_row, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_calib_sel_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_calib_sel_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_calib_sel_row, LV_OBJ_FLAG_SCROLLABLE);
    params_calib_rebuild_pump_btns();

    lv_obj_t *qty_row = lv_obj_create(tab);
    lv_obj_set_size(qty_row, 420, 56);
    lv_obj_align(qty_row, LV_ALIGN_TOP_LEFT, 0, 118);
    lv_obj_set_style_bg_opa(qty_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(qty_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(qty_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(qty_row, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(qty_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(qty_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(qty_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *qty_lbl = lv_label_create(qty_row);
    lv_label_set_text(qty_lbl, "Quantite (cL)");
    lv_obj_set_style_text_color(qty_lbl, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_set_width(qty_lbl, 160);

    s_calib_qty_ta = lv_textarea_create(qty_row);
    lv_obj_set_size(s_calib_qty_ta, 240, 44);
    lv_textarea_set_one_line(s_calib_qty_ta, true);
    lv_textarea_set_placeholder_text(s_calib_qty_ta, "ex: 20");
    lv_obj_add_event_cb(s_calib_qty_ta, params_on_ta_focused, LV_EVENT_FOCUSED, NULL);

    s_calib_start_btn = lv_btn_create(tab);
    lv_obj_set_size(s_calib_start_btn, 220, 58);
    lv_obj_align(s_calib_start_btn, LV_ALIGN_TOP_LEFT, 0, 188);
    lv_obj_set_style_bg_color(s_calib_start_btn, lv_color_hex(0x00C853), LV_PART_MAIN);
    lv_obj_set_style_radius(s_calib_start_btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(s_calib_start_btn, on_calib_start_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_calib_start_lbl = lv_label_create(s_calib_start_btn);
    lv_obj_center(s_calib_start_lbl);

    s_calib_reset_btn = lv_btn_create(tab);
    lv_obj_set_size(s_calib_reset_btn, 220, 58);
    lv_obj_align(s_calib_reset_btn, LV_ALIGN_TOP_LEFT, 236, 188);
    lv_obj_set_style_bg_color(s_calib_reset_btn, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_radius(s_calib_reset_btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(s_calib_reset_btn, on_calib_reset_clicked,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *calib_reset_lbl = lv_label_create(s_calib_reset_btn);
    lv_label_set_text(calib_reset_lbl, LV_SYMBOL_REFRESH "  Defaut pompe");
    lv_obj_center(calib_reset_lbl);

    s_calib_status_lbl = lv_label_create(tab);
    lv_label_set_text(s_calib_status_lbl, "Pret");
    lv_obj_set_style_text_color(s_calib_status_lbl,
                                lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(s_calib_status_lbl, LV_ALIGN_TOP_LEFT, 0, 260);

    s_calib_progress_lbl = lv_label_create(tab);
    lv_label_set_text(s_calib_progress_lbl, "Progression: 0.00 cL");
    lv_obj_set_style_text_color(s_calib_progress_lbl,
                                lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(s_calib_progress_lbl, LV_ALIGN_TOP_LEFT, 0, 292);

    s_calib_constant_lbl = lv_label_create(tab);
    lv_label_set_text(s_calib_constant_lbl, "P1: 540.420 pulses/cL (debitmetre)");
    lv_obj_set_style_text_color(s_calib_constant_lbl,
                                lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_align(s_calib_constant_lbl, LV_ALIGN_TOP_LEFT, 0, 324);

    params_calib_update_ui();

}

void params_calib_refresh(void)
{
    params_calib_ensure_flows(0);
    s_calib_pump_sel = 0;
    params_calib_rebuild_pump_btns();
    lv_textarea_set_text(s_calib_qty_ta, "");
    calib_set_status("Pret", 0x888888);
    lv_label_set_text(s_calib_progress_lbl, "Progression: 0.00 cL");
    params_calib_update_ui();
}

void params_calib_save(void)
{
    for (int i = 0; i < params_pump_count; i++)
        params_calib_save_flow(i);

    /* Without this, DISPENSE commands keep using stale constants until app restart. */
    params_calib_push_constants();
}

void params_calib_leave(void)
{
    if (s_calib_running)
        params_calib_stop_ui(true);
}
