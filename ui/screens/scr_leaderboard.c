#include "scr_leaderboard.h"
#include "screen_manager.h"
#include "game_db.h"
#include "game_logic.h"

static lv_obj_t *s_screen;
static lv_obj_t *s_table;

/* ── event callbacks ────────────────────────────────────────────────────── */

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    screen_manager_load(SCREEN_HOME);
}

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_leaderboard_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* Title */
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, LV_SYMBOL_LIST "  Classement");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    /* Table */
    s_table = lv_table_create(s_screen);
    lv_obj_set_size(s_table, 760, 390);
    lv_obj_align(s_table, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(s_table, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_table, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_table, lv_color_hex(0x0F3460), LV_PART_MAIN);

    lv_table_set_column_count(s_table, 5);
    lv_table_set_column_width(s_table, 0, 180); /* Joueur   */
    lv_table_set_column_width(s_table, 1, 110); /* Total cL */
    lv_table_set_column_width(s_table, 2,  80); /* Bonus    */
    lv_table_set_column_width(s_table, 3,  80); /* Malus    */
    lv_table_set_column_width(s_table, 4, 250); /* A donné  */

    /* Header row */
    lv_table_set_cell_value(s_table, 0, 0, "Joueur");
    lv_table_set_cell_value(s_table, 0, 1, "Total cL");
    lv_table_set_cell_value(s_table, 0, 2, "Bonus");
    lv_table_set_cell_value(s_table, 0, 3, "Malus");
    lv_table_set_cell_value(s_table, 0, 4, "A donne a");

    /* Back button */
    lv_obj_t *btn_back = lv_btn_create(s_screen);
    lv_obj_set_size(btn_back, 160, 46);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -20, -8);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x444444), LV_PART_MAIN);
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

    for (int i = 0; i < count; i++) {
        uint32_t row = (uint32_t)(i + 1);
        char buf[64];

        lv_table_set_cell_value(s_table, row, 0, users[i].name);

        lv_snprintf(buf, sizeof(buf), "%d cL", users[i].total_cl);
        lv_table_set_cell_value(s_table, row, 1, buf);

        lv_snprintf(buf, sizeof(buf), "+%d", users[i].bonus);
        lv_table_set_cell_value(s_table, row, 2, buf);

        lv_snprintf(buf, sizeof(buf), "+%d", users[i].malus);
        lv_table_set_cell_value(s_table, row, 3, buf);

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
        lv_table_set_cell_value(s_table, row, 4, given);
    }
}
