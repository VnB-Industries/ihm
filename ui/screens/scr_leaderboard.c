#include "scr_leaderboard.h"
#include "screen_manager.h"
#include "game_db.h"
#include "game_logic.h"
#include <time.h>

static lv_obj_t *s_screen;
static lv_obj_t *s_chart;
static lv_obj_t *s_legend;
static lv_obj_t *s_table;

/* Layout split of the area below the title (percent of SCREEN_INNER_H) */
#define LEADERBOARD_CHART_H_PCT   60
#define LEADERBOARD_LEGEND_H      28
#define LEADERBOARD_SECTION_GAP   8

/* Chart data resolution */
#define LEADERBOARD_CHART_POINTS  8    /* points drawn per line on the graph */
#define LEADERBOARD_HISTORY_CAP   64   /* max raw history rows read per user */

/* Y-axis settings */
#define LEADERBOARD_Y_AXIS_W  40
#define LEADERBOARD_AXIS_GAP  20

static cl_history_point_t s_history_buf[LEADERBOARD_HISTORY_CAP];
static lv_chart_series_t *s_chart_series[GAME_DB_MAX_USERS];
static int s_chart_series_count;

static const lv_color_t k_chart_palette[] = {
    LV_COLOR_MAKE(0xE7, 0x4C, 0x3C), /* red    */
    LV_COLOR_MAKE(0x2E, 0xCC, 0x71), /* green  */
    LV_COLOR_MAKE(0x34, 0x98, 0xDB), /* blue   */
    LV_COLOR_MAKE(0xF3, 0x9C, 0x12), /* orange */
    LV_COLOR_MAKE(0x9B, 0x59, 0xB6), /* purple */
    LV_COLOR_MAKE(0x1A, 0xBC, 0x9C), /* teal   */
    LV_COLOR_MAKE(0xF1, 0xC4, 0x0F), /* yellow */
    LV_COLOR_MAKE(0xE8, 0x4C, 0xA0), /* pink   */
};
#define CHART_PALETTE_SIZE (sizeof(k_chart_palette) / sizeof(k_chart_palette[0]))

enum {
    LEADERBOARD_COL_PLAYER = 0,
    LEADERBOARD_COL_TOTAL,
    LEADERBOARD_COL_BONUS,
    LEADERBOARD_COL_MALUS,
    LEADERBOARD_COL_GIVEN,
    LEADERBOARD_COL_COUNT
};

static void leaderboard_apply_column_widths(void)
{
    lv_coord_t player_w = SCREEN_INNER_W * 28 / 100;
    lv_coord_t total_w  = SCREEN_INNER_W * 14 / 100;
    lv_coord_t bonus_w  = SCREEN_INNER_W * 10 / 100;
    lv_coord_t malus_w  = SCREEN_INNER_W * 10 / 100;
    lv_coord_t given_w  = SCREEN_INNER_W - player_w - total_w - bonus_w - malus_w;

    lv_table_set_column_width(s_table, LEADERBOARD_COL_PLAYER, player_w);
    lv_table_set_column_width(s_table, LEADERBOARD_COL_TOTAL, total_w);
    lv_table_set_column_width(s_table, LEADERBOARD_COL_BONUS, bonus_w);
    lv_table_set_column_width(s_table, LEADERBOARD_COL_MALUS, malus_w);
    lv_table_set_column_width(s_table, LEADERBOARD_COL_GIVEN, given_w);
}

static lv_obj_t *s_y_scale;

/* ── event callbacks ────────────────────────────────────────────────────── */

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    screen_manager_load(SCREEN_HOME);
}


static void table_draw_cb(lv_event_t * e)
{
    lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t * base_dsc =
        (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(draw_task);

    if(base_dsc->part != LV_PART_ITEMS)
        return;

    uint32_t row = base_dsc->id1;
    uint32_t col = base_dsc->id2;

    lv_draw_fill_dsc_t * fill_dsc = lv_draw_task_get_fill_dsc(draw_task);
    lv_draw_label_dsc_t * label_dsc = lv_draw_task_get_label_dsc(draw_task);

    /* Cell background */
    if(fill_dsc) {
        fill_dsc->color = lv_color_hex(0x0F3460);
        fill_dsc->opa = LV_OPA_COVER;
    }

    /* Text */
    if(label_dsc) {
        label_dsc->color = lv_color_hex(0xFFFFFF);
    }
}

/* ── chart / legend rebuild ────────────────────────────────────────────── */

static void leaderboard_build_legend(user_record_t *users, int count)
{
    lv_obj_clean(s_legend);

    for (int i = 0; i < count; i++) {
        lv_color_t col = k_chart_palette[i % CHART_PALETTE_SIZE];

        lv_obj_t *item = lv_obj_create(s_legend);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(item, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(item, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_gap(item, 4, LV_PART_MAIN);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *dot = lv_obj_create(item);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, col, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);

        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, users[i].name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    }
}

/* Builds one chart line per player: cL total over time, forward-filled into
 * LEADERBOARD_CHART_POINTS evenly-spaced buckets spanning the earliest
 * recorded history point to now. */
static void leaderboard_build_chart(user_record_t *users, int count)
{
    for (int i = 0; i < s_chart_series_count; i++)
        lv_chart_remove_series(s_chart, s_chart_series[i]);
    s_chart_series_count = 0;

    if (count == 0) {
        lv_chart_set_point_count(s_chart, 1);
        if(s_y_scale) {
            lv_scale_set_range(s_y_scale, 0, 1);
        }
        return;
    }

    int64_t min_epoch = -1;
    int64_t max_epoch = (int64_t)time(NULL);

    for (int i = 0; i < count; i++) {
        int n = db_get_cl_history(users[i].id, s_history_buf, LEADERBOARD_HISTORY_CAP);
        if (n > 0 && (min_epoch < 0 || s_history_buf[0].epoch < min_epoch))
            min_epoch = s_history_buf[0].epoch;
    }

    if (min_epoch < 0 || min_epoch >= max_epoch) {
        /* Not enough history yet to draw a meaningful timeline */
        lv_chart_set_point_count(s_chart, 1);
        if (s_y_scale) {
            lv_scale_set_range(s_y_scale, 0, 1);
        }
        return;
    }

    lv_chart_set_point_count(s_chart, LEADERBOARD_CHART_POINTS);

    int64_t span  = max_epoch - min_epoch;
    int     max_y = 1;

    for (int i = 0; i < count && s_chart_series_count < GAME_DB_MAX_USERS; i++) {
        lv_color_t col = k_chart_palette[i % CHART_PALETTE_SIZE];
        lv_chart_series_t *series = lv_chart_add_series(s_chart, col, LV_CHART_AXIS_PRIMARY_Y);
        s_chart_series[s_chart_series_count++] = series;

        int n = db_get_cl_history(users[i].id, s_history_buf, LEADERBOARD_HISTORY_CAP);
        int hist_idx  = 0;
        int last_val  = 0; /* cL is 0 before the player's first recorded point */

        for (int p = 0; p < LEADERBOARD_CHART_POINTS; p++) {
            int64_t bucket_epoch = min_epoch + (span * p) / (LEADERBOARD_CHART_POINTS - 1);

            while (hist_idx < n && s_history_buf[hist_idx].epoch <= bucket_epoch) {
                last_val = s_history_buf[hist_idx].total_cl;
                hist_idx++;
            }

            lv_chart_set_value_by_id(s_chart, series, p, (lv_coord_t)last_val);
            if (last_val > max_y) max_y = last_val;
        }
    }

    int y_max = max_y + max_y / 10 + 1;
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, y_max);
    if (s_y_scale) {
        lv_scale_set_range(s_y_scale, 0, y_max);
    }
}

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_leaderboard_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* Title */
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, LV_SYMBOL_LIST "  Classement");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* Layout: title -> chart -> legend -> table, all within SCREEN_INNER_H */
    lv_coord_t content_top = 52;
    lv_coord_t chart_h     = SCREEN_INNER_H * LEADERBOARD_CHART_H_PCT / 100;
    lv_coord_t legend_h    = LEADERBOARD_LEGEND_H;
    lv_coord_t table_top   = content_top + chart_h + LEADERBOARD_SECTION_GAP
                              + legend_h + LEADERBOARD_SECTION_GAP;
    lv_coord_t table_h     = SCREEN_INNER_H - chart_h - legend_h
                              - 2 * LEADERBOARD_SECTION_GAP;

    /* Evolution chart: one line per player, cL over time */
    /* Y axis scale */
    s_y_scale = lv_scale_create(s_screen);
    lv_obj_set_size(s_y_scale, LEADERBOARD_Y_AXIS_W, chart_h);
    lv_obj_align(s_y_scale, LV_ALIGN_TOP_LEFT, 0, content_top);

    lv_scale_set_mode(s_y_scale, LV_SCALE_MODE_VERTICAL_LEFT);
    lv_scale_set_label_show(s_y_scale, true);
    lv_scale_set_total_tick_count(s_y_scale, 6);
    lv_scale_set_major_tick_every(s_y_scale, 1);
    lv_scale_set_range(s_y_scale, 0, 10);

    lv_obj_set_style_bg_opa(s_y_scale, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_y_scale, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_set_style_line_color(s_y_scale, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_clear_flag(s_y_scale, LV_OBJ_FLAG_SCROLLABLE);

    /* Chart */
    s_chart = lv_chart_create(s_screen);
    lv_obj_set_size(s_chart,
                    SCREEN_INNER_W - LEADERBOARD_Y_AXIS_W,
                    chart_h);

    lv_obj_align_to(s_chart, s_y_scale, LV_ALIGN_OUT_RIGHT_TOP, LEADERBOARD_AXIS_GAP, 0);

    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(s_chart, 4, 6);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_chart, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(0x243B63), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_chart, lv_color_hex(0xEAEAEA), LV_PART_MAIN);

    /* Legend: colored dot + player name, matches chart series order */
    s_legend = lv_obj_create(s_screen);
    lv_obj_set_size(s_legend, SCREEN_INNER_W, legend_h);
    lv_obj_align(s_legend, LV_ALIGN_TOP_MID, 0, content_top + chart_h + LEADERBOARD_SECTION_GAP);
    lv_obj_set_flex_flow(s_legend, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_bg_opa(s_legend, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_legend, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_legend, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_legend, 10, LV_PART_MAIN);
    lv_obj_clear_flag(s_legend, LV_OBJ_FLAG_SCROLLABLE);

    /* Table */
    s_table = lv_table_create(s_screen);

    /*Redraw table cells with custom colors */
    lv_obj_add_flag(s_table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(s_table, table_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);

    lv_obj_set_size(s_table, SCREEN_INNER_W, table_h);
    lv_obj_align(s_table, LV_ALIGN_TOP_MID, 0, table_top);
    lv_obj_set_style_bg_color(s_table, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_table, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_table, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_opa(s_table, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_table, LV_BORDER_SIDE_FULL, LV_PART_ITEMS);

    lv_table_set_column_count(s_table, LEADERBOARD_COL_COUNT);
    leaderboard_apply_column_widths();

    /* Header row */
    lv_table_set_cell_value(s_table, 0, LEADERBOARD_COL_PLAYER, "Joueur");
    lv_table_set_cell_value(s_table, 0, LEADERBOARD_COL_TOTAL, "Total cL");
    lv_table_set_cell_value(s_table, 0, LEADERBOARD_COL_BONUS, "Bonus");
    lv_table_set_cell_value(s_table, 0, LEADERBOARD_COL_MALUS, "Malus");
    lv_table_set_cell_value(s_table, 0, LEADERBOARD_COL_GIVEN, "Dernier Bonus/Malus");

    /* Back button */
    lv_obj_t *btn_back = lv_btn_create(s_screen);
    lv_obj_set_size(btn_back, 130, 40);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn_back, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn_back, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT "  Retour");
    lv_obj_center(lbl_back);
}

lv_obj_t *scr_leaderboard_get(void) { return s_screen; }

void scr_leaderboard_refresh(void)
{
    /* Reset to header row only */
    lv_table_set_row_count(s_table, 1);

    user_record_t users[GAME_DB_MAX_USERS];
    int count = db_get_all_users(users, GAME_DB_MAX_USERS);

    /* Sort by total_cl descending */
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (users[j].total_cl > users[i].total_cl) {
                user_record_t tmp = users[i];
                users[i] = users[j];
                users[j] = tmp;
            }

    leaderboard_build_chart(users, count);
    leaderboard_build_legend(users, count);

    for (int i = 0; i < count; i++) {
        uint32_t row = (uint32_t)(i + 1);
        char buf[64];

        lv_table_set_cell_value(s_table, row, LEADERBOARD_COL_PLAYER, users[i].name);

        lv_snprintf(buf, sizeof(buf), "%d cL", users[i].total_cl);
        lv_table_set_cell_value(s_table, row, LEADERBOARD_COL_TOTAL, buf);

        lv_snprintf(buf, sizeof(buf), "+%d", users[i].bonus);
        lv_table_set_cell_value(s_table, row, LEADERBOARD_COL_BONUS, buf);

        lv_snprintf(buf, sizeof(buf), "+%d", users[i].malus);
        lv_table_set_cell_value(s_table, row, LEADERBOARD_COL_MALUS, buf);

        /* Given-modifier column */
        char given[80] = "-";
        if (users[i].given_modifier != 0 && users[i].given_to_id > 0) {
            user_record_t target;
            if (db_get_user(users[i].given_to_id, &target) == 0) {
                int mins = db_get_config("timeout_modifier_minutes", 5);
                const char *type_str;
                switch ((modifier_type_t)users[i].given_modifier) {
                    case MODIFIER_BONUS:          type_str = "Bonus";        break;
                    case MODIFIER_MALUS:          type_str = "Malus";        break;
                    case MODIFIER_TIMEOUT_ADD: {
                        static char tmbuf[12];
                        lv_snprintf(tmbuf, sizeof(tmbuf), "+%dmin", mins);
                        type_str = tmbuf;
                        break;
                    }
                    case MODIFIER_TIMEOUT_REMOVE: {
                        static char tmbuf[12];
                        lv_snprintf(tmbuf, sizeof(tmbuf), "-%dmin", mins);
                        type_str = tmbuf;
                        break;
                    }
                    default: type_str = "?"; break;
                }
                lv_snprintf(given, sizeof(given), "%s a %s",
                    type_str, target.name);
            }
        }
        lv_table_set_cell_value(s_table, row, LEADERBOARD_COL_GIVEN, given);
    }
}