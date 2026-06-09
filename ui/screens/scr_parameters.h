#ifndef SCR_PARAMETERS_H
#define SCR_PARAMETERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

void      scr_parameters_init(void);
lv_obj_t *scr_parameters_get(void);
void      scr_parameters_refresh(void); /* reset to PIN phase */

#ifdef __cplusplus
}
#endif

#endif /* SCR_PARAMETERS_H */
