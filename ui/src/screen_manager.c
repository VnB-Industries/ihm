#include "screen_manager.h"
#include "serial_comm.h"
#include "game_logic.h"
#include "game_db.h"
#include "scr_home.h"
#include "scr_profile_select.h"
#include "scr_glass_select.h"
#include "scr_gaming.h"
#include "scr_bonus_wheel.h"
#include "scr_give_modifier.h"
#include "scr_leaderboard.h"
#include "scr_parameters.h"
#include "scr_saving.h"

#include "lvgl/lvgl.h"

/* ── screen table ───────────────────────────────────────────────────────── */

typedef struct {
    lv_obj_t *(*get_fn)(void);
    void      (*enter_fn)(void);   /* called every time the screen is shown */
} screen_entry_t;

static const screen_entry_t k_screens[SCREEN_COUNT] = {
    [SCREEN_HOME]           = { scr_home_get,           scr_home_refresh           },
    [SCREEN_PROFILE_SELECT] = { scr_profile_select_get, scr_profile_select_refresh },
    [SCREEN_GLASS_SELECT]   = { scr_glass_select_get,   scr_glass_select_refresh   },
    [SCREEN_WHEEL]          = { scr_gaming_get,          scr_gaming_refresh         },
    [SCREEN_BONUS_WHEEL]    = { scr_bonus_wheel_get,    scr_bonus_wheel_refresh    },
    [SCREEN_GIVE_MODIFIER]  = { scr_give_modifier_get,  scr_give_modifier_refresh  },
    [SCREEN_LEADERBOARD]    = { scr_leaderboard_get,    scr_leaderboard_refresh    },
    [SCREEN_PARAMETERS]     = { scr_parameters_get,     scr_parameters_refresh     },
    [SCREEN_SAVING]         = { scr_saving_get,         scr_saving_refresh         },
};

static screen_id_t s_current = SCREEN_COUNT;

#define IHM_INACTIVITY_TIMEOUT_MS 30000U
#define IHM_INACTIVITY_POLL_MS      500U

static uint32_t s_last_touch_ms = 0;
static lv_timer_t *s_inactivity_timer = NULL;

/* ── RFID dispatch ────────────────────────────────────────────────── */

static int        s_enroll_uid      = -1;
static uint32_t   s_enroll_deadline = 0;
static void     (*s_enroll_cb)(rfid_enroll_result_t) = NULL;
static lv_timer_t *s_rfid_timer = NULL;

static void inactivity_reset(void)
{
    s_last_touch_ms = lv_tick_get();
}

static void on_indev_touch_event(lv_event_t *e)
{
    (void)e;
    inactivity_reset();

    if(screen_manager_current() == SCREEN_SAVING) {
        screen_manager_load(SCREEN_HOME);
    }
}

static void register_input_activity_hooks(void)
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while(indev) {
        if(lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_add_event_cb(indev, on_indev_touch_event, LV_EVENT_PRESSED, NULL);
            lv_indev_add_event_cb(indev, on_indev_touch_event, LV_EVENT_PRESSING, NULL);
            lv_indev_add_event_cb(indev, on_indev_touch_event, LV_EVENT_RELEASED, NULL);
            lv_indev_add_event_cb(indev, on_indev_touch_event, LV_EVENT_CLICKED, NULL);
        }
        indev = lv_indev_get_next(indev);
    }
}

static void inactivity_timer_cb(lv_timer_t *t)
{
    (void)t;
    if(s_current == SCREEN_SAVING || s_current == SCREEN_COUNT) {
        return;
    }

    uint32_t now = lv_tick_get();
    uint32_t elapsed = now - s_last_touch_ms;
    if(elapsed >= IHM_INACTIVITY_TIMEOUT_MS) {
        screen_manager_load(SCREEN_SAVING);
    }
}

static void rfid_tick(lv_timer_t *t)
{
    (void)t;

    char tag[32];
    bool got_tag = serial_comm_read_rfid(tag, sizeof(tag));

    /* ── Enrollment mode ── */
    if (s_enroll_uid >= 0) {
        if (lv_tick_get() >= s_enroll_deadline) {
            void (*cb)(rfid_enroll_result_t) = s_enroll_cb;
            s_enroll_uid = -1;
            s_enroll_cb  = NULL;
            if (cb) cb(RFID_ENROLL_TIMEOUT);
            return;
        }
        if (!got_tag) return;

        user_record_t existing;
        if (db_get_user_by_rfid(tag, &existing) == 0 &&
                existing.id != s_enroll_uid) {
            void (*cb)(rfid_enroll_result_t) = s_enroll_cb;
            s_enroll_uid = -1;
            s_enroll_cb  = NULL;
            if (cb) cb(RFID_ENROLL_TAKEN);
            return;
        }

        db_set_user_rfid(s_enroll_uid, tag);
        void (*cb)(rfid_enroll_result_t) = s_enroll_cb;
        s_enroll_uid = -1;
        s_enroll_cb  = NULL;
        if (cb) cb(RFID_ENROLL_OK);
        return;
    }

    /* ── Normal dispatch ── */
    if (!got_tag) return;
    if (s_current == SCREEN_PARAMETERS) return;   /* admin screen — ignore */

    user_record_t u;
    if (db_get_user_by_rfid(tag, &u) != 0) return;  /* unknown tag */

    game_set_active_user(u.id);
    screen_manager_load(SCREEN_GLASS_SELECT);
}

/* ── public API ─────────────────────────────────────────────────────────── */

void screen_manager_init(void)
{
    scr_home_init();
    scr_profile_select_init();
    scr_glass_select_init();
    scr_gaming_init();
    scr_bonus_wheel_init();
    scr_give_modifier_init();
    scr_leaderboard_init();
    scr_parameters_init();
    scr_saving_init();

    inactivity_reset();
    register_input_activity_hooks();
    s_inactivity_timer = lv_timer_create(inactivity_timer_cb,
                                         IHM_INACTIVITY_POLL_MS, NULL);
    s_rfid_timer = lv_timer_create(rfid_tick, 150, NULL);
}

void screen_manager_load(screen_id_t id)
{
    if (id >= SCREEN_COUNT) return;

    /* Call refresh/enter hook before loading */
    if (k_screens[id].enter_fn)
        k_screens[id].enter_fn();

    lv_obj_t *scr = k_screens[id].get_fn();
    if (!scr) return;

    s_current = id;
    lv_scr_load(scr);

    if(id != SCREEN_SAVING) {
        inactivity_reset();
    }
}

screen_id_t screen_manager_current(void)
{
    return s_current;
}

void screen_manager_rfid_enroll_start(int user_id, uint32_t timeout_ms,
                                      void (*cb)(rfid_enroll_result_t))
{
    s_enroll_uid      = user_id;
    s_enroll_deadline = lv_tick_get() + timeout_ms;
    s_enroll_cb       = cb;
}

void screen_manager_rfid_enroll_cancel(void)
{
    s_enroll_uid = -1;
    s_enroll_cb  = NULL;
}
