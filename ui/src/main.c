#include "lvgl/lvgl.h"
#include "screen_manager.h"

/*
 * Platform-specific driver init must be provided here.
 * Uncomment and implement the two lines below according to your lv_drivers setup:
 *   lv_port_disp_init();
 *   lv_port_indev_init();
 */

int main(void)
{
    lv_init();

    /* TODO: initialize your display and input drivers */
    /* lv_port_disp_init(); */
    /* lv_port_indev_init(); */

    screen_manager_init();
    screen_manager_load(SCREEN_HOME);

    while (1) {
        lv_timer_handler();
        /* TODO: add a ~5 ms platform delay, e.g. usleep(5000) */
    }

    return 0;
}
