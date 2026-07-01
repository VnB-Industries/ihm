#include "platform_linux.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/kd.h>

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

static int s_console_tty_fd = -1;
static bool s_console_graphics_mode = false;

static void terminal_set_cursor_visible(bool visible)
{
    const char *seq = visible ? "\x1b[?25h" : "\x1b[?25l";
    size_t len = strlen(seq);

    int tty_fd = open("/dev/tty", O_WRONLY | O_NOCTTY);
    if(tty_fd >= 0) {
        (void)write(tty_fd, seq, len);
        close(tty_fd);
    }

    if(isatty(STDOUT_FILENO)) {
        (void)write(STDOUT_FILENO, seq, len);
    }
}

static void console_set_graphics_mode(void)
{
    if(s_console_tty_fd >= 0) {
        return;
    }

    s_console_tty_fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if(s_console_tty_fd < 0) {
        return;
    }

    if(ioctl(s_console_tty_fd, KDSETMODE, KD_GRAPHICS) == 0) {
        s_console_graphics_mode = true;
    }
}

static void restore_terminal_cursor(void)
{
    terminal_set_cursor_visible(true);

    if(s_console_tty_fd >= 0) {
        if(s_console_graphics_mode) {
            (void)ioctl(s_console_tty_fd, KDSETMODE, KD_TEXT);
        }
        close(s_console_tty_fd);
        s_console_tty_fd = -1;
        s_console_graphics_mode = false;
    }
}

bool platform_linux_init(void)
{
    /* Hide cursor while the app owns the screen. */
   // terminal_set_cursor_visible(false);
    //console_set_graphics_mode();
    //atexit(restore_terminal_cursor);

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