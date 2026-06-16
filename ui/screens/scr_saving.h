#ifndef SCR_SAVING_H
#define SCR_SAVING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

void      scr_saving_init(void);
void      scr_saving_refresh(void);
void      scr_saving_deinit(void);
lv_obj_t *scr_saving_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SCR_SAVING_H */
