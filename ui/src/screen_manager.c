#include "screen_manager.h"
#include "serial_comm.h"
#include "game_logic.h"
#include "game_db.h"
#include "scr_home.h"
#include "scr_profile_select.h"
#include "scr_glass_select.h"
#include "scr_cocktail_select.h"
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
    void      (*leave_fn)(void);   /* called when the screen is replaced     */
} screen_entry_t;

static const screen_entry_t k_screens[SCREEN_COUNT] = {
    [SCREEN_HOME]           = { scr_home_get,           scr_home_refresh           },
    [SCREEN_PROFILE_SELECT] = { scr_profile_select_get, scr_profile_select_refresh },
    [SCREEN_GLASS_SELECT]   = { scr_glass_select_get,   scr_glass_select_refresh   },
    [SCREEN_COCKTAIL_SELECT]= { scr_cocktail_select_get, scr_cocktail_select_refresh },
    [SCREEN_WHEEL]          = { scr_gaming_get,          scr_gaming_refresh         },
    [SCREEN_BONUS_WHEEL]    = { scr_bonus_wheel_get,    scr_bonus_wheel_refresh    },
    [SCREEN_GIVE_MODIFIER]  = { scr_give_modifier_get,  scr_give_modifier_refresh  },
    [SCREEN_LEADERBOARD]    = { scr_leaderboard_get,    scr_leaderboard_refresh    },
    [SCREEN_PARAMETERS]     = { scr_parameters_get,     scr_parameters_refresh,    scr_parameters_leave },
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

void screen_manager_attach_input(lv_indev_t *indev)
{
    if(!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;

    lv_indev_add_event_cb(indev, on_indev_touch_event, LV_EVENT_PRESSED, NULL);
    lv_indev_add_event_cb(indev, on_indev_touch_event, LV_EVENT_PRESSING, NULL);
    lv_indev_add_event_cb(indev, on_indev_touch_event, LV_EVENT_RELEASED, NULL);
    lv_indev_add_event_cb(indev, on_indev_touch_event, LV_EVENT_CLICKED, NULL);
}

static void register_input_activity_hooks(void)
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while(indev) {
        screen_manager_attach_input(indev);
        indev = lv_indev_get_next(indev);
    }
}

static void inactivity_timer_cb(lv_timer_t *t)
{
    (void)t;
    if(s_current == SCREEN_SAVING || s_current == SCREEN_COUNT) {
        return;
    }

    /* A calibration ends when the glass is lifted, which routinely takes
     * longer than the inactivity window.  Blanking to the screensaver while a
     * pump is energised would hide the only controls that can stop it. */
    if(serial_comm_job_active()) {
        inactivity_reset();
        return;
    }

    uint32_t now = lv_tick_get();
    uint32_t elapsed = now - s_last_touch_ms;
    if(elapsed >= IHM_INACTIVITY_TIMEOUT_MS) {
        screen_manager_load(SCREEN_SAVING);
    }
}

/* Single owner of the serial link's read side.  Everything the controller
 * sends is parsed once here, inside serial_comm_poll(), and routed to the
 * active job or the RFID queue; no other module reads the fd. */
static void serial_tick(lv_timer_t *t)
{
    (void)t;

    serial_comm_poll();

    char tag[32] = {0};
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

    /* A badge must not hand the machine to someone else while a pour,
     * calibration or purge is running: its outcome belongs to whoever
     * started it.  The tag is dropped, not queued for later. */
    if (serial_comm_job_active()) return;

    user_record_t u;
    if (db_get_user_by_rfid(tag, &u) != 0) return;  /* unknown tag */

    if (s_current == SCREEN_PARAMETERS) {
        if (u.is_admin)
            scr_parameters_unlock_admin();
        return;
    }

    game_set_active_user(u.id);
    screen_manager_load(SCREEN_GLASS_SELECT);
}

/* ── public API ─────────────────────────────────────────────────────────── */

void screen_manager_init(void)
{
    scr_home_init();
    scr_profile_select_init();
    scr_glass_select_init();
    scr_cocktail_select_init();
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
    /* 50 ms: fast enough that a job's PROGRESS and completion are picked up
     * promptly, which is what keeps the pour UI responsive. */
    s_rfid_timer = lv_timer_create(serial_tick, 50, NULL);
}

void screen_manager_service_serial(void)
{
    serial_comm_poll();
}

void screen_manager_load(screen_id_t id)
{
    if (id >= SCREEN_COUNT) return;

    /* Let the outgoing screen release whatever it owns before the incoming one
     * runs.  Skipped when reloading the same screen. */
    if (s_current != SCREEN_COUNT && s_current != id && k_screens[s_current].leave_fn) {
        k_screens[s_current].leave_fn();
    }

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
