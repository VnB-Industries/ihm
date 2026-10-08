/* Shared internals of the Parametres screen.
 *
 * The screen was one 1976-line file covering five unrelated tabs.  It is now a
 * shell (scr_parameters.c) plus one file per tab, and this header is the only
 * seam between them.  Nothing here is public API: scr_parameters.h stays the
 * screen's outward interface.
 *
 * Each tab owns its own widgets and state as file statics and exposes only
 * build / refresh / save / leave.  What genuinely crosses tabs lives below. */
#ifndef PARAMS_INTERNAL_H
#define PARAMS_INTERNAL_H

#include "lvgl/lvgl.h"
#include "game_db.h"
#include <stdbool.h>
#include <stddef.h>

/* ── shell-owned, shared by the tabs ────────────────────────────────────── */

extern lv_obj_t *params_screen;    /* the screen object          */
extern lv_obj_t *params_tabview;   /* parent of every tab        */
extern lv_obj_t *params_kb;        /* on-screen keyboard overlay */

/* Declared pump count being edited (>= 2).  Several tabs size themselves from
 * it, and the Jeu tab's slider is what changes it. */
extern int params_pump_count;

/* Layout helpers shared by the tabs.  The slider and text-area row builders
 * are not here: each is used by exactly one tab and lives with it. */
void      params_add_section_label(lv_obj_t *parent, const char *text);
lv_obj_t *params_make_tab_scroll(lv_obj_t *tab);

/* Raise the keyboard for a text area.  Attach as an LV_EVENT_FOCUSED handler. */
void      params_on_ta_focused(lv_event_t *e);

/* ── per-tab lifecycle, driven by the shell ─────────────────────────────── */

void params_game_build(lv_obj_t *tab);
void params_game_refresh(void);          /* load sliders from the DB   */
void params_game_save(void);             /* write sliders to the DB    */

void params_users_build(lv_obj_t *tab);
void params_users_build_overlays(lv_obj_t *parent); /* delete + RFID popups */
void params_users_refresh(void);
void params_users_save(void);
void params_users_leave(void);
void params_users_refresh_list(void);

void params_purge_build(lv_obj_t *tab);
void params_purge_refresh(void);
void params_purge_leave(void);
void params_purge_rebuild_pump_btns(void);
void params_purge_stop(void);            /* send STOP and reset the UI */

void params_calib_build(lv_obj_t *tab);
void params_calib_refresh(void);
void params_calib_save(void);
void params_calib_leave(void);
void params_calib_rebuild_pump_btns(void);
void params_calib_update_ui(void);
void params_calib_stop_ui(bool send_stop);
bool params_calib_push_constants(void);
void params_calib_save_flow(int idx0);
/* Load flow constants for pumps that params_pump_count has just grown to cover. */
void params_calib_ensure_flows(int previous_count);

void params_cocktails_build(lv_obj_t *tab);
void params_cocktails_refresh(void);
void params_cocktails_refresh_list(void);
void params_cocktails_open_editor(int cocktail_id);
void params_cocktails_close_editor(void);
/* Rebuild the open editor after the pump count changed. */
void params_cocktails_reopen_selected(void);

/* Flow-constant scaling shared between the Jeu and Calibration tabs. */
int   params_flow_float_to_scaled(float value);
float params_flow_scaled_to_float(int value, float fallback);
void  params_calib_config_key(int idx0, char *buf, size_t n);
int   params_calib_default_scaled(int idx0);
float params_calib_default_flow(int idx0);

#endif /* PARAMS_INTERNAL_H */
