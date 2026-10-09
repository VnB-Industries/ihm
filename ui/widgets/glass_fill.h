#ifndef GLASS_FILL_H
#define GLASS_FILL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

#define GLASS_FILL_MAX_LAYERS 6

/**
 * Create a glass that fills up as liquid is poured.
 * Each pump is drawn as its own coloured layer, stacked in index order from
 * the bottom of the glass, so a cocktail shows its ingredients.
 * @param parent  parent LVGL object
 * @param w       widget width in pixels (rim width)
 * @param h       widget height in pixels (rim to foot)
 * @return        the glass object (pass to all other glass_fill_ functions)
 */
lv_obj_t *glass_fill_create(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);

/** Volume, in cL, that fills the glass to the rim.  Clears the layers. */
void glass_fill_set_capacity(lv_obj_t *gf, float capacity_cl);

/**
 * Set the volume poured so far by each pump (index order, cL).
 * Anything above the capacity is clipped at the rim.
 */
void glass_fill_set_levels(lv_obj_t *gf, const float *cl, int count);

#ifdef __cplusplus
}
#endif

#endif /* GLASS_FILL_H */
