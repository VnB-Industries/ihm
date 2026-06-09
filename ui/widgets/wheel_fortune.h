#ifndef WHEEL_FORTUNE_H
#define WHEEL_FORTUNE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

#define WHEEL_FORTUNE_MAX_SEGMENTS 12

typedef struct {
    const char *label;
    lv_color_t  color;
    uint32_t    value;
} wf_segment_t;

/**
 * Callback fired when the wheel finishes spinning.
 * @param wf          the wheel container object
 * @param seg_index   winning segment index (0-based)
 * @param seg         pointer to the winning segment data
 */
typedef void (*wf_result_cb_t)(lv_obj_t *wf, uint16_t seg_index,
                               const wf_segment_t *seg);

/**
 * Create a wheel-of-fortune widget.
 * @param parent  parent LVGL object
 * @param radius  wheel radius in pixels
 * @return        the container object (pass to all other wf_ functions)
 */
lv_obj_t *wheel_fortune_create(lv_obj_t *parent, lv_coord_t radius);

/** Set (or replace) the segment definitions. */
void wheel_fortune_set_segments(lv_obj_t *wf, const wf_segment_t *segs,
                                uint8_t count);

/** Register a callback invoked when spinning finishes. */
void wheel_fortune_set_result_cb(lv_obj_t *wf, wf_result_cb_t cb);

/**
 * Start the spin animation.
 * @param wf           wheel object
 * @param duration_ms  total animation duration in milliseconds
 * @param result_seg   index of the segment that should win (0-based)
 */
void wheel_fortune_spin(lv_obj_t *wf, uint32_t duration_ms,
                        uint16_t result_seg);

/** Returns true while the spin animation is running. */
bool wheel_fortune_is_spinning(lv_obj_t *wf);

#ifdef __cplusplus
}
#endif

#endif /* WHEEL_FORTUNE_H */
