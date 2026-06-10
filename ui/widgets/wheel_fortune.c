#include "wheel_fortune.h"
#include <string.h>

/* ── configuration ──────────────────────────────────────────────────────── */

#define WF_ARC_STEPS      8    /* polygon vertices per arc – 8 is smooth enough */
#define WF_POINTER_H      18   /* pointer triangle height in pixels           */

/* ── private data ───────────────────────────────────────────────────────── */

typedef struct {
    lv_obj_t       *canvas;
    lv_draw_buf_t  *draw_buf;       /* canvas draw buffer (v9 aligned)        */
    wf_segment_t    segs[WHEEL_FORTUNE_MAX_SEGMENTS];
    uint8_t         seg_count;
    int32_t         rotation;       /* current rotation offset (degrees)      */
    bool            spinning;
    lv_coord_t      radius;
    wf_result_cb_t  result_cb;
} wf_priv_t;

/* ── forward declarations ───────────────────────────────────────────────── */

static void wf_redraw(wf_priv_t *p);

/* ── drawing helpers ────────────────────────────────────────────────────── */

/*
 * Coordinate convention used throughout:
 *   0° = top (12 o'clock), positive angles go clockwise.
 *   x = cx + r * sin(deg)
 *   y = cy - r * cos(deg)
 */
static inline lv_coord_t wf_x(lv_coord_t cx, lv_coord_t r, int16_t deg)
{
    return (lv_coord_t)(cx + (int32_t)r * lv_trigo_sin(deg) / LV_TRIGO_SIN_MAX);
}

static inline lv_coord_t wf_y(lv_coord_t cy, lv_coord_t r, int16_t deg)
{
    /* cos(deg) = sin(deg + 90) */
    return (lv_coord_t)(cy - (int32_t)r * lv_trigo_sin((int16_t)(deg + 90)) /
                        LV_TRIGO_SIN_MAX);
}

static void wf_draw_segment(lv_layer_t *layer, lv_coord_t cx, lv_coord_t cy,
                             lv_coord_t r, int32_t start_deg, int32_t end_deg,
                             lv_color_t color)
{
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = color;
    dsc.opa   = LV_OPA_COVER;

    for (int i = 0; i < WF_ARC_STEPS; i++) {
        int32_t deg1 = start_deg + (int32_t)i       * (end_deg - start_deg) / WF_ARC_STEPS;
        int32_t deg2 = start_deg + (int32_t)(i + 1) * (end_deg - start_deg) / WF_ARC_STEPS;
        int16_t d1 = (int16_t)(((deg1 % 360) + 360) % 360);
        int16_t d2 = (int16_t)(((deg2 % 360) + 360) % 360);
        dsc.p[0] = (lv_point_precise_t){ cx, cy };
        dsc.p[1] = (lv_point_precise_t){ wf_x(cx, r, d1), wf_y(cy, r, d1) };
        dsc.p[2] = (lv_point_precise_t){ wf_x(cx, r, d2), wf_y(cy, r, d2) };
        lv_draw_triangle(layer, &dsc);
    }
}

static void wf_redraw(wf_priv_t *p)
{
    lv_coord_t r  = p->radius;
    lv_coord_t cx = r;
    lv_coord_t cy = r;

    lv_canvas_fill_bg(p->canvas, lv_color_black(), LV_OPA_TRANSP);

    if (p->seg_count == 0) return;

    lv_layer_t layer;
    lv_canvas_init_layer(p->canvas, &layer);

    int32_t seg_angle = 360 / p->seg_count;

    /* --- pie segments --- */
    for (uint8_t i = 0; i < p->seg_count; i++) {
        int32_t start = (int32_t)i * seg_angle + p->rotation;
        int32_t end   = start + seg_angle;
        wf_draw_segment(&layer, cx, cy, r, start, end, p->segs[i].color);
    }

    /* --- divider lines --- */
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_white();
    line_dsc.width = 2;
    line_dsc.opa   = LV_OPA_COVER;

    for (uint8_t i = 0; i < p->seg_count; i++) {
        int32_t deg = (int32_t)i * seg_angle + p->rotation;
        int16_t d   = (int16_t)(((deg % 360) + 360) % 360);
        line_dsc.p1 = (lv_point_precise_t){ cx, cy };
        line_dsc.p2 = (lv_point_precise_t){ wf_x(cx, r, d), wf_y(cy, r, d) };
        lv_draw_line(&layer, &line_dsc);
    }

    /* --- segment labels (placed at 65 % radius, segment midpoint) --- */
    lv_draw_label_dsc_t lbl_dsc;
    lv_draw_label_dsc_init(&lbl_dsc);
    lbl_dsc.color = lv_color_white();
    lbl_dsc.font  = LV_FONT_DEFAULT;
    lbl_dsc.opa   = LV_OPA_COVER;

    for (uint8_t i = 0; i < p->seg_count; i++) {
        if (!p->segs[i].label) continue;
        int32_t mid = (int32_t)i * seg_angle + seg_angle / 2 + p->rotation;
        int16_t d   = (int16_t)(((mid % 360) + 360) % 360);
        lv_coord_t lr = (lv_coord_t)(r * 65 / 100);
        lv_coord_t lx = wf_x(cx, lr, d);
        lv_coord_t ly = wf_y(cy, lr, d);
        lbl_dsc.text = p->segs[i].label;
        lv_area_t a = { (int32_t)(lx - 16), (int32_t)(ly - 8),
                        (int32_t)(lx + 15), (int32_t)(ly + 7) };
        lv_draw_label(&layer, &lbl_dsc, &a);
    }

    /* --- outer ring border --- */
    lv_draw_arc_dsc_t ring_dsc;
    lv_draw_arc_dsc_init(&ring_dsc);
    ring_dsc.color       = lv_color_white();
    ring_dsc.width       = 3;
    ring_dsc.opa         = LV_OPA_COVER;
    ring_dsc.center.x    = cx;
    ring_dsc.center.y    = cy;
    ring_dsc.radius      = (uint16_t)r;
    ring_dsc.start_angle = 0;
    ring_dsc.end_angle   = 360;
    lv_draw_arc(&layer, &ring_dsc);

    /* --- center hub --- */
    lv_draw_arc_dsc_t hub_dsc;
    lv_draw_arc_dsc_init(&hub_dsc);
    hub_dsc.color       = lv_color_white();
    hub_dsc.width       = (int32_t)(r / 8);
    hub_dsc.opa         = LV_OPA_COVER;
    hub_dsc.center.x    = cx;
    hub_dsc.center.y    = cy;
    hub_dsc.radius      = (uint16_t)(r / 8);
    hub_dsc.start_angle = 0;
    hub_dsc.end_angle   = 360;
    lv_draw_arc(&layer, &hub_dsc);

    lv_canvas_finish_layer(p->canvas, &layer);
}

/* ── animation callbacks ────────────────────────────────────────────────── */

static void wf_anim_exec_cb(void *var, int32_t value)
{
    wf_priv_t *p = (wf_priv_t *)var;
    p->rotation  = (int32_t)(value % 360);
    wf_redraw(p);
}

static void wf_anim_ready_cb(lv_anim_t *a)
{
    wf_priv_t *p = (wf_priv_t *)a->var;
    p->spinning  = false;

    if (!p->result_cb || p->seg_count == 0) return;

    int32_t seg_angle    = 360 / p->seg_count;
    int32_t pointer_deg  = ((-p->rotation) % 360 + 360) % 360;
    uint16_t result_idx  = (uint16_t)((uint32_t)pointer_deg /
                                      (uint32_t)seg_angle) % p->seg_count;

    lv_obj_t *container = lv_obj_get_parent(p->canvas);
    p->result_cb(container, result_idx, &p->segs[result_idx]);
}

/* ── public API ─────────────────────────────────────────────────────────── */

lv_obj_t *wheel_fortune_create(lv_obj_t *parent, lv_coord_t radius)
{
    lv_coord_t size = (lv_coord_t)(radius * 2 + 1);

    /* Container holds canvas + pointer indicator */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, size, (lv_coord_t)(size + WF_POINTER_H + 4));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);

    /* Pointer triangle at the top center */
    lv_obj_t *ptr = lv_label_create(cont);
    lv_label_set_text(ptr, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(ptr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(ptr, LV_ALIGN_TOP_MID, 0, 0);

    /* Canvas for the spinning wheel */
    lv_draw_buf_t *draw_buf = lv_draw_buf_create(size, size,
                                                  LV_COLOR_FORMAT_RGB565,
                                                  LV_STRIDE_AUTO);
    /* RGB565: 2 bytes/px vs 4 for ARGB8888 – halves canvas memory */
    LV_ASSERT_MALLOC(draw_buf);
    lv_draw_buf_clear(draw_buf, NULL);

    lv_obj_t *canvas = lv_canvas_create(cont);
    lv_canvas_set_draw_buf(canvas, draw_buf);
    lv_obj_align(canvas, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Private data */
    wf_priv_t *priv = (wf_priv_t *)lv_malloc(sizeof(wf_priv_t));
    LV_ASSERT_MALLOC(priv);
    lv_memset(priv, 0, sizeof(wf_priv_t));

    priv->canvas   = canvas;
    priv->draw_buf = draw_buf;
    priv->radius   = radius;
    priv->rotation = 0;
    priv->spinning = false;

    lv_obj_set_user_data(cont, priv);

    wf_redraw(priv);
    return cont;
}

void wheel_fortune_set_segments(lv_obj_t *wf, const wf_segment_t *segs,
                                uint8_t count)
{
    wf_priv_t *p = (wf_priv_t *)lv_obj_get_user_data(wf);
    if (!p) return;

    if (count > WHEEL_FORTUNE_MAX_SEGMENTS)
        count = WHEEL_FORTUNE_MAX_SEGMENTS;

    lv_memcpy(p->segs, segs, count * sizeof(wf_segment_t));
    p->seg_count = count;
    wf_redraw(p);
}

void wheel_fortune_set_result_cb(lv_obj_t *wf, wf_result_cb_t cb)
{
    wf_priv_t *p = (wf_priv_t *)lv_obj_get_user_data(wf);
    if (p) p->result_cb = cb;
}

void wheel_fortune_spin(lv_obj_t *wf, uint32_t duration_ms,
                        uint16_t result_seg)
{
    wf_priv_t *p = (wf_priv_t *)lv_obj_get_user_data(wf);
    if (!p || p->spinning || p->seg_count == 0) return;

    int32_t seg_angle = 360 / p->seg_count;

    /*
     * Target rotation: the pointer at 0° (top) must land on the center of
     * result_seg.  Segment i center (without rotation) is at:
     *   i * seg_angle + seg_angle/2
     * With rotation R the center is at (i*seg_angle + seg_angle/2 + R) deg.
     * We want this == 0, so R = -(i*seg_angle + seg_angle/2), normalised to
     * [0, 360).
     */
    int32_t target_rot = -((int32_t)result_seg * seg_angle + seg_angle / 2);
    target_rot = ((target_rot % 360) + 360) % 360;

    int32_t cur   = ((p->rotation % 360) + 360) % 360;
    int32_t delta = ((target_rot - cur) + 360) % 360;
    int32_t end   = p->rotation + 5 * 360 + delta; /* 5 full spins + landing */

    p->spinning = true;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, p);
    lv_anim_set_exec_cb(&a, wf_anim_exec_cb);
    lv_anim_set_values(&a, p->rotation, end);
    lv_anim_set_time(&a, duration_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, wf_anim_ready_cb);
    lv_anim_start(&a);
}

bool wheel_fortune_is_spinning(lv_obj_t *wf)
{
    wf_priv_t *p = (wf_priv_t *)lv_obj_get_user_data(wf);
    return p ? p->spinning : false;
}
