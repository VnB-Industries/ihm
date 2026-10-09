#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE   /* glob(), O_CLOEXEC under -std=c11 */
#endif

#include "platform_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
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

#ifndef IHM_DRM_DEVICE
#define IHM_DRM_DEVICE "/dev/dri/card0"
#endif

static int s_console_tty_fd = -1;
static bool s_console_graphics_mode = false;

static lv_display_t *s_disp = NULL;
static void (*s_pointer_added_cb)(lv_indev_t *indev) = NULL;

void platform_linux_set_pointer_added_cb(void (*cb)(lv_indev_t *indev))
{
    s_pointer_added_cb = cb;
}

#if IHM_LV_USE_EVDEV
/* The touchscreen is found by what it can do, not by node number.
 * /dev/input/eventN is handed out in probe order, and on the Pi the USB
 * touchscreen and the HDMI CEC input race for the low numbers at boot: a
 * pinned /dev/input/event2 was the touchscreen on some boots and the HDMI
 * remote on others, leaving touch dead.
 *
 * It is also looked for again whenever none is attached, rather than only on
 * a hotplug notification.  A node is created root-only and udev opens it to
 * the input group a moment later, so an attempt made the instant it appears
 * fails with EACCES; LVGL's own discovery gives up at that point, and a
 * touchscreen that re-enumerated stayed dead until a restart. */

#define TOUCH_RESCAN_MS 1000

static bool s_touch_seen = false;   /* for logging a loss only once */

/* Absolute X and Y axes: a touchscreen (or a tablet), never a mouse, a key
 * source or the HDMI CEC input. */
static bool evdev_is_touchscreen(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if(fd < 0) return false;   /* not ours to read yet, or gone */

    unsigned long abs_bits[(ABS_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {0};
    bool ok = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0;
    close(fd);
    if(!ok) return false;

    const size_t bpl = 8 * sizeof(long);
    bool has_x = abs_bits[ABS_X / bpl] & (1UL << (ABS_X % bpl));
    bool has_y = abs_bits[ABS_Y / bpl] & (1UL << (ABS_Y % bpl));
    return has_x && has_y;
}

static bool touch_attached(void)
{
    for(lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
        if(lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) return true;
    }
    return false;
}

/* Attach the first touchscreen under /dev/input.  lv_evdev deletes its indev
 * on its own when the device is unplugged, which is what lets the next call
 * find the replacement. */
static bool touch_try_attach(void)
{
    glob_t g;
    if(glob("/dev/input/event*", 0, NULL, &g) != 0) {
        globfree(&g);
        return false;
    }

    lv_indev_t *indev = NULL;
    for(size_t i = 0; i < g.gl_pathc && !indev; i++) {
        if(!evdev_is_touchscreen(g.gl_pathv[i])) continue;

        indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, g.gl_pathv[i]);
        if(indev) {
            lv_indev_set_display(indev, s_disp);
            fprintf(stderr, "[platform] touchscreen attached on %s\n", g.gl_pathv[i]);
        }
    }
    globfree(&g);

    if(indev && s_pointer_added_cb) {
        s_pointer_added_cb(indev);
    }
    return indev != NULL;
}

static void touch_watch_cb(lv_timer_t *t)
{
    (void)t;
    if(touch_attached()) return;

    if(s_touch_seen) {
        fprintf(stderr, "[platform] touchscreen lost, waiting for it to come back\n");
        s_touch_seen = false;
    }
    if(touch_try_attach()) s_touch_seen = true;
}
#endif

static void terminal_set_cursor_visible(bool visible)
{
    const char *seq = visible ? "\x1b[?25h" : "\x1b[?25l";
    size_t len = strlen(seq);

    int tty_fd = open("/dev/tty", O_WRONLY | O_NOCTTY);
    /* Best effort: a cursor left visible is cosmetic.  (A (void) cast does
     * not silence warn_unused_result under GCC, hence the empty ifs.) */
    if(tty_fd >= 0) {
        if(write(tty_fd, seq, len) < 0) { }
        close(tty_fd);
    }

    if(isatty(STDOUT_FILENO)) {
        if(write(STDOUT_FILENO, seq, len) < 0) { }
    }
}

/* Stop the kernel text console from drawing into the framebuffer.  Without
 * this, boot messages, the login prompt and the blinking cursor land on top
 * of the UI and stay there until LVGL happens to redraw that area.
 *
 * KDSETMODE is only allowed on the caller's controlling terminal (or with
 * CAP_SYS_TTY_CONFIG), so the service must own the VT: ihm-app.service sets
 * TTYPath=/dev/tty1 with StandardInput=tty-force, which is what makes
 * /dev/tty resolve here. */
static void console_set_graphics_mode(void)
{
    if(s_console_tty_fd >= 0) {
        return;
    }

    s_console_tty_fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if(s_console_tty_fd < 0) {
        fprintf(stderr, "[platform] no controlling tty (%s), console text may "
                        "show over the UI\n", strerror(errno));
        return;
    }

    if(ioctl(s_console_tty_fd, KDSETMODE, KD_GRAPHICS) == 0) {
        s_console_graphics_mode = true;
    } else {
        fprintf(stderr, "[platform] KDSETMODE KD_GRAPHICS failed: %s\n",
                strerror(errno));
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
    /* Own the screen: hide the cursor and silence the text console.  Undone
     * on a normal exit, which SIGTERM from systemd leads to. */
    terminal_set_cursor_visible(false);
    console_set_graphics_mode();
    atexit(restore_terminal_cursor);

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
    s_disp = disp;

#if IHM_LV_USE_EVDEV
    s_touch_seen = touch_try_attach();
    if(!s_touch_seen) {
        /* Not fatal: the watch below attaches it as soon as it can be opened. */
        fprintf(stderr, "[platform] no touchscreen found yet, waiting for one\n");
    }
    lv_timer_create(touch_watch_cb, TOUCH_RESCAN_MS, NULL);
#endif

    return true;
}