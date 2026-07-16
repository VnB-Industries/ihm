#ifndef SCR_GLASS_SELECT_H
#define SCR_GLASS_SELECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

void      scr_glass_select_init(void);
lv_obj_t *scr_glass_select_get(void);
void      scr_glass_select_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCR_GLASS_SELECT_H */
