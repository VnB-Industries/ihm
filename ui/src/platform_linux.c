#include "platform_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "lvgl/drivers/display/lv_linux_fbdev.h"
#include "lvgl/drivers/display/lv_linux_drm.h"
#include "lvgl/drivers/indev/lv_evdev.h"

#ifndef IHM_FBDEV_DEVICE
#define IHM_FBDEV_DEVICE "/dev/fb0"
#endif

#ifndef IHM_EVDEV_DEVICE
#define IHM_EVDEV_DEVICE "/dev/input/event0"
#endif

#ifndef IHM_DRM_DEVICE
#define IHM_DRM_DEVICE "/dev/dri/card0"
#endif

bool platform_linux_init(void)
{
    /* Hide the terminal cursor for exclusive screen control */
    system("stty -echo -icanon"); /* Disable echo and canonical mode */
    system("setterm -cursor off"); /* Hide cursor via terminfo */

    lv_display_t *disp = NULL;

#if IHM_LV_USE_LINUX_DRM
    disp = lv_linux_drm_create();
    if(disp == NULL) {
        fprintf(stderr, "[platform] failed to create DRM display\n");
        return false;
    }
    if(lv_linux_drm_set_file(disp, IHM_DRM_DEVICE, -1) != LV_RESULT_OK) {
        fprintf(stderr, "[platform] failed to open DRM device %s\n", IHM_DRM_DEVICE);
        return false;
    }
#elif IHM_LV_USE_LINUX_FBDEV
    disp = lv_linux_fbdev_create();
    if(disp == NULL) {
        fprintf(stderr, "[platform] failed to create fbdev display\n");
        return false;
    }
    if(lv_linux_fbdev_set_file(disp, IHM_FBDEV_DEVICE) != LV_RESULT_OK) {
        fprintf(stderr, "[platform] failed to open framebuffer %s\n", IHM_FBDEV_DEVICE);
        return false;
    }
#else
    fprintf(stderr, "[platform] no Linux display backend enabled\n");
    return false;
#endif

    lv_display_set_default(disp);

#if IHM_LV_USE_EVDEV
    lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, IHM_EVDEV_DEVICE);
    if(touch == NULL) {
        fprintf(stderr, "[platform] failed to open input device %s\n", IHM_EVDEV_DEVICE);
        return false;
    }
    lv_indev_set_display(touch, disp);
#endif

    return true;
}