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

int main(void)
{
    lv_init();

    if(!platform_linux_init()) {
        return 1;
    }

    if(db_init(IHM_DB_PATH) != 0) {
        return 1;
    }

    if(!serial_comm_init()) {
        /* Keep the UI running even when the microcontroller link is unavailable. */
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
