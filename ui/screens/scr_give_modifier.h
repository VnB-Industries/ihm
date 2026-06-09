#ifndef SCR_GIVE_MODIFIER_H
#define SCR_GIVE_MODIFIER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

void      scr_give_modifier_init(void);
lv_obj_t *scr_give_modifier_get(void);
void      scr_give_modifier_refresh(void); /* reload user list, update title */

#ifdef __cplusplus
}
#endif

#endif /* SCR_GIVE_MODIFIER_H */
