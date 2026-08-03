#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include "lvgl/lvgl.h"
#include "screen_manager.h"
#include "game_db.h"
#include "platform_linux.h"
#include "serial_comm.h"

#ifndef IHM_DB_PATH
#define IHM_DB_PATH "./game.db"
#endif

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
    lv_init();

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
    screen_manager_load(SCREEN_HOME);

    while(1) {
        uint32_t sleep_time_ms = lv_timer_handler();
        if(sleep_time_ms == LV_NO_TIMER_READY) {
            sleep_time_ms = LV_DEF_REFR_PERIOD;
        }
        usleep(sleep_time_ms * 1000U);
    }

    db_close();
    serial_comm_deinit();
    return 0;
}
