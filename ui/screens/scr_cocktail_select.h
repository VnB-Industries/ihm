#ifndef SCR_COCKTAIL_SELECT_H
#define SCR_COCKTAIL_SELECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

void      scr_cocktail_select_init(void);
lv_obj_t *scr_cocktail_select_get(void);
void      scr_cocktail_select_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCR_COCKTAIL_SELECT_H */
