#include "screen_manager.h"
#include "scr_home.h"
#include "scr_profile_select.h"
#include "scr_gaming.h"
#include "scr_bonus_wheel.h"
#include "scr_give_modifier.h"
#include "scr_leaderboard.h"
#include "scr_parameters.h"

#include "lvgl/lvgl.h"

/* ── screen table ───────────────────────────────────────────────────────── */

typedef struct {
    lv_obj_t *(*get_fn)(void);
    void      (*enter_fn)(void);   /* called every time the screen is shown */
} screen_entry_t;

static const screen_entry_t k_screens[SCREEN_COUNT] = {
    [SCREEN_HOME]           = { scr_home_get,           NULL                       },
    [SCREEN_PROFILE_SELECT] = { scr_profile_select_get, scr_profile_select_refresh },
    [SCREEN_WHEEL]          = { scr_gaming_get,          scr_gaming_refresh         },
    [SCREEN_BONUS_WHEEL]    = { scr_bonus_wheel_get,    scr_bonus_wheel_refresh    },
    [SCREEN_GIVE_MODIFIER]  = { scr_give_modifier_get,  scr_give_modifier_refresh  },
    [SCREEN_LEADERBOARD]    = { scr_leaderboard_get,    scr_leaderboard_refresh    },
    [SCREEN_PARAMETERS]     = { scr_parameters_get,     scr_parameters_refresh     },
};

static screen_id_t s_current = SCREEN_COUNT;

/* ── public API ─────────────────────────────────────────────────────────── */

void screen_manager_init(void)
{
    scr_home_init();
    scr_profile_select_init();
    scr_gaming_init();
    scr_bonus_wheel_init();
    scr_give_modifier_init();
    scr_leaderboard_init();
    scr_parameters_init();
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
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 250, 0, false);
}

screen_id_t screen_manager_current(void)
{
    return s_current;
}

