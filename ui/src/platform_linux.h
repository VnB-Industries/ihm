#ifndef PLATFORM_LINUX_H
#define PLATFORM_LINUX_H

#include <stdbool.h>
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool platform_linux_init(void);

/* Called for each touchscreen or mouse attached after start-up, e.g. a USB
 * touchscreen that dropped off and came back.  Devices present at start-up
 * already exist when platform_linux_init() returns, so iterate those. */
void platform_linux_set_pointer_added_cb(void (*cb)(lv_indev_t *indev));

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_LINUX_H */