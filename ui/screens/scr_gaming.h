#ifndef SCR_GAMING_H
#define SCR_GAMING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

void      scr_gaming_init(void);
lv_obj_t *scr_gaming_get(void);
void      scr_gaming_refresh(void); /* recompute segments for active user */

#ifdef __cplusplus
}
#endif

#endif /* SCR_GAMING_H */
