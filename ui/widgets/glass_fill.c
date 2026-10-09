#include "glass_fill.h"

/* ── configuration ──────────────────────────────────────────────────────── */

#define GF_WALL_W      4     /* glass outline thickness in pixels           */
#define GF_FOOT_H      10    /* heavy base under the bowl                   */
#define GF_TAPER_PCT   72    /* bottom width as a percentage of the rim     */

/* One colour per pump, so the layers of a cocktail stay apart. */
static const uint32_t gf_layer_colors[GLASS_FILL_MAX_LAYERS] = {
    0xF5A623,   /* amber  */
    0xE94560,   /* red    */
    0x4FC3F7,   /* blue   */
    0x81C784,   /* green  */
    0xBA68C8,   /* purple */
    0xFFD54F,   /* yellow */
};

/* ── private data ───────────────────────────────────────────────────────── */

typedef struct {
    float capacity_cl;
    float level_cl[GLASS_FILL_MAX_LAYERS];
    int   layer_count;
} gf_priv_t;

/* ── drawing helpers ────────────────────────────────────────────────────── */

/* Fill the part of the bowl between y_low (nearer the bottom) and y_high as
 * a trapezoid, following the taper of the walls. */
static void gf_fill_slice(lv_layer_t *layer, int32_t cx, int32_t top,
                          int32_t bottom, int32_t top_hw, int32_t bot_hw,
                          int32_t y_low, int32_t y_high,
                          lv_color_t color, lv_opa_t opa)
{
    if (y_high >= y_low) return;

    int32_t span = bottom - top;
    int32_t hw_low  = bot_hw + (top_hw - bot_hw) * (bottom - y_low)  / span;
    int32_t hw_high = bot_hw + (top_hw - bot_hw) * (bottom - y_high) / span;

    /* A rectangle for the middle and a triangle for each slanted side.  Two
     * triangles sharing the diagonal would leave an anti-aliased seam. */
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = color;
    rect.bg_opa   = opa;
    lv_area_t mid = { .x1 = cx - hw_low, .y1 = y_high,
                      .x2 = cx + hw_low - 1, .y2 = y_low - 1 };
    lv_draw_rect(layer, &rect, &mid);

    if (hw_high <= hw_low) return;

    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = color;
    dsc.opa   = opa;

    dsc.p[0] = (lv_point_precise_t){ cx - hw_high, y_high };
    dsc.p[1] = (lv_point_precise_t){ cx - hw_low,  y_high };
    dsc.p[2] = (lv_point_precise_t){ cx - hw_low,  y_low };
    lv_draw_triangle(layer, &dsc);

    dsc.p[0] = (lv_point_precise_t){ cx + hw_high, y_high };
    dsc.p[1] = (lv_point_precise_t){ cx + hw_low,  y_high };
    dsc.p[2] = (lv_point_precise_t){ cx + hw_low,  y_low };
    lv_draw_triangle(layer, &dsc);
}

static void gf_line(lv_layer_t *layer, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, int32_t width, lv_opa_t opa)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color       = lv_color_hex(0xFFFFFF);
    dsc.opa         = opa;
    dsc.width       = width;
    dsc.round_start = 1;
    dsc.round_end   = 1;
    dsc.p1 = (lv_point_precise_t){ x1, y1 };
    dsc.p2 = (lv_point_precise_t){ x2, y2 };
    lv_draw_line(layer, &dsc);
}

static void gf_draw_event_cb(lv_event_t *e)
{
    lv_obj_t  *obj = lv_event_get_target_obj(e);
    gf_priv_t *p   = (gf_priv_t *)lv_obj_get_user_data(obj);
    if (p == NULL) return;

    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    int32_t w      = lv_area_get_width(&a);
    int32_t cx     = a.x1 + w / 2;
    int32_t top    = a.y1 + GF_WALL_W;
    int32_t bottom = a.y2 - GF_FOOT_H;
    int32_t top_hw = w / 2 - GF_WALL_W;
    int32_t bot_hw = top_hw * GF_TAPER_PCT / 100;
    int32_t span   = bottom - top;
    if (span <= 0 || top_hw <= 0) return;

    /* Faint tint so the empty glass still reads as glass. */
    gf_fill_slice(layer, cx, top, bottom, top_hw, bot_hw, bottom, top,
                  lv_color_hex(0xFFFFFF), LV_OPA_10);

    /* Liquid, one layer per pump stacked from the bottom. */
    float cum = 0.0f;
    int32_t y_low = bottom;
    if (p->capacity_cl > 0.0f) {
        for (int i = 0; i < p->layer_count; i++) {
            if (p->level_cl[i] <= 0.0f) continue;
            cum += p->level_cl[i];
            float frac = cum / p->capacity_cl;
            if (frac > 1.0f) frac = 1.0f;
            int32_t y_high = bottom - (int32_t)(frac * (float)span);
            gf_fill_slice(layer, cx, top, bottom, top_hw, bot_hw, y_low, y_high,
                          lv_color_hex(gf_layer_colors[i]), LV_OPA_COVER);
            y_low = y_high;
        }
    }

    /* Bright meniscus on the surface of the liquid. */
    if (y_low < bottom) {
        int32_t hw = bot_hw + (top_hw - bot_hw) * (bottom - y_low) / span;
        gf_line(layer, cx - hw, y_low, cx + hw, y_low, 2, LV_OPA_60);
    }

    /* Reflection streak, then the walls on top so they hide the liquid edges. */
    gf_line(layer, cx - top_hw + 12, top + 14, cx - bot_hw + 10, bottom - 14,
            4, LV_OPA_20);
    gf_line(layer, cx - top_hw, top, cx - bot_hw, bottom, GF_WALL_W, LV_OPA_80);
    gf_line(layer, cx + top_hw, top, cx + bot_hw, bottom, GF_WALL_W, LV_OPA_80);

    lv_draw_rect_dsc_t foot;
    lv_draw_rect_dsc_init(&foot);
    foot.bg_color = lv_color_hex(0xFFFFFF);
    foot.bg_opa   = LV_OPA_80;
    foot.radius   = 3;
    lv_area_t foot_area = {
        .x1 = cx - bot_hw - GF_WALL_W / 2, .y1 = bottom - GF_WALL_W / 2,
        .x2 = cx + bot_hw + GF_WALL_W / 2, .y2 = a.y2,
    };
    lv_draw_rect(layer, &foot, &foot_area);
}

static void gf_delete_event_cb(lv_event_t *e)
{
    lv_obj_t  *obj = lv_event_get_target_obj(e);
    gf_priv_t *p   = (gf_priv_t *)lv_obj_get_user_data(obj);
    lv_free(p);
    lv_obj_set_user_data(obj, NULL);
}

/* ── public API ─────────────────────────────────────────────────────────── */

lv_obj_t *glass_fill_create(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, gf_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(obj, gf_delete_event_cb, LV_EVENT_DELETE, NULL);

    gf_priv_t *priv = (gf_priv_t *)lv_malloc(sizeof(gf_priv_t));
    LV_ASSERT_MALLOC(priv);
    lv_memset(priv, 0, sizeof(gf_priv_t));
    lv_obj_set_user_data(obj, priv);

    return obj;
}

void glass_fill_set_capacity(lv_obj_t *gf, float capacity_cl)
{
    gf_priv_t *p = (gf_priv_t *)lv_obj_get_user_data(gf);
    if (p == NULL) return;
    p->capacity_cl = capacity_cl;
    p->layer_count = 0;
    lv_memset(p->level_cl, 0, sizeof(p->level_cl));
    lv_obj_invalidate(gf);
}

void glass_fill_set_levels(lv_obj_t *gf, const float *cl, int count)
{
    gf_priv_t *p = (gf_priv_t *)lv_obj_get_user_data(gf);
    if (p == NULL) return;
    if (count > GLASS_FILL_MAX_LAYERS) count = GLASS_FILL_MAX_LAYERS;
    if (count < 0) count = 0;
    for (int i = 0; i < count; i++)
        p->level_cl[i] = cl[i];
    p->layer_count = count;
    lv_obj_invalidate(gf);
}
