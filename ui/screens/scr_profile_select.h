#ifndef SCR_PROFILE_SELECT_H
#define SCR_PROFILE_SELECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

void      scr_profile_select_init(void);
lv_obj_t *scr_profile_select_get(void);
void      scr_profile_select_refresh(void); /* reload user list from DB */

#ifdef __cplusplus
}
#endif

#endif /* SCR_PROFILE_SELECT_H */
