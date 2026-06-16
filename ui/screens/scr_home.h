#ifndef SCR_HOME_H
#define SCR_HOME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

void      scr_home_init(void);
void      scr_home_refresh(void);
lv_obj_t *scr_home_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SCR_HOME_H */
