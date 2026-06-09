#ifndef SCR_BONUS_WHEEL_H
#define SCR_BONUS_WHEEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

void      scr_bonus_wheel_init(void);
lv_obj_t *scr_bonus_wheel_get(void);
void      scr_bonus_wheel_refresh(void); /* reload segments, reset state */

#ifdef __cplusplus
}
#endif

#endif /* SCR_BONUS_WHEEL_H */
