#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "lvgl/lvgl.h"
#include "screen_manager.h"
#include "game_db.h"
#include "game_logic.h"
#include "platform_linux.h"
#include "serial_comm.h"

#ifndef IHM_DB_PATH
#define IHM_DB_PATH "./game.db"
#endif

/* Set from a signal handler, so nothing but an assignment happens in there. */
static volatile sig_atomic_t s_shutdown_requested = 0;

static void on_terminate_signal(int sig)
{
    (void)sig;
    s_shutdown_requested = 1;
}

/* systemd sends SIGTERM on `systemctl stop`.  Without this the process is
 * killed outright: pumps keep running until the controller's own timeout and
 * SQLite is cut off mid-write. */
static void install_signal_handlers(void)
{
    struct sigaction sa;
    sa.sa_handler = on_terminate_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;              /* no SA_RESTART: let the loop notice at once */

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
}

static void sync_flow_constants_from_db(void)
{
    int pump_count = db_get_config("pump_count", 2);
    if (pump_count < 2) {
        pump_count = 2;
    }
    if (pump_count > SERIAL_MAX_PUMPS) {
        pump_count = SERIAL_MAX_PUMPS;
    }

    int pump1_scaled = db_get_config("pump1_pulses_per_cl_x1000", 540420);
    if (pump1_scaled <= 0) {
        pump1_scaled = 540420;
    }
    float pump1 = pump1_scaled / 1000.0f;

    float time_flows[SERIAL_MAX_PUMPS - 1];
    int time_count = pump_count - 1;
    for (int i = 0; i < time_count; i++) {
        char key[40];
        snprintf(key, sizeof(key), "pump%d_flow_clps_x1000", i + 2);
        int scaled = db_get_config(key, 2800);
        if (scaled <= 0) {
            scaled = 2800;
        }
        time_flows[i] = scaled / 1000.0f;
    }

    serial_comm_set_dispense_constants(pump1, time_flows, time_count);
}

int main(void)
{
    install_signal_handlers();

    lv_init();

    /* lv_init() seeds LVGL's RNG with a fixed constant, so every boot would
     * otherwise replay the exact same wheel results.  game_logic owns the
     * randomness the game depends on; seed it from the clock. */
    game_rng_seed((uint32_t)time(NULL) ^ (uint32_t)getpid());

    if(!platform_linux_init()) {
        return 1;
    }

    if(db_init(IHM_DB_PATH) != 0) {
        return 1;
    }

    /* Load constants into memory regardless of serial link availability. */
    sync_flow_constants_from_db();

    if(!serial_comm_init()) {
        fprintf(stderr, "[serial] startup without USB serial link\n");
    }

    screen_manager_init();
    /* A touchscreen that turns up later (USB re-enumeration) gets the same
     * activity hooks as the ones screen_manager_init() found. */
    platform_linux_set_pointer_added_cb(screen_manager_attach_input);
    screen_manager_load(SCREEN_HOME);

    while(!s_shutdown_requested) {
        uint32_t sleep_time_ms = lv_timer_handler();
        if(sleep_time_ms == LV_NO_TIMER_READY) {
            sleep_time_ms = LV_DEF_REFR_PERIOD;
        }
        usleep(sleep_time_ms * 1000U);
    }

    fprintf(stderr, "[main] shutting down\n");

    /* Order matters: stop the pumps before letting go of the serial link. */
    serial_comm_send_stop();
    serial_comm_deinit();
    db_close();
    return 0;
}
