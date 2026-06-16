#include "scr_give_modifier.h"
#include "screen_manager.h"
#include "game_db.h"
#include "game_logic.h"

static lv_obj_t *s_screen;
static lv_obj_t *s_title_label;
static lv_obj_t *s_list_cont;

/* ── event callbacks ────────────────────────────────────────────────────── */

static void on_target_clicked(lv_event_t *e)
{
    int target_id = (int)(intptr_t)lv_event_get_user_data(e);
    int giver_id  = game_get_active_user();
    modifier_type_t mod = game_get_pending_modifier();

    game_apply_modifier(giver_id, target_id, mod);
    game_set_pending_modifier(MODIFIER_NONE);

    screen_manager_load(SCREEN_WHEEL);
}

/* ── public API ─────────────────────────────────────────────────────────── */

void scr_give_modifier_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* Dynamic title (set at refresh time) */
    s_title_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xF5C518), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(s_title_label, SCREEN_INNER_W);
    lv_label_set_long_mode(s_title_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 16);

    /* Scrollable player grid */
    s_list_cont = lv_obj_create(s_screen);
    lv_obj_set_size(s_list_cont, SCREEN_INNER_W, SCREEN_H - 100);
    lv_obj_align(s_list_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(s_list_cont, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_list_cont, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list_cont, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_list_cont, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_list_cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_list_cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_list_cont, LV_DIR_VER);
}

lv_obj_t *scr_give_modifier_get(void) { return s_screen; }

void scr_give_modifier_refresh(void)
{
    modifier_type_t mod = game_get_pending_modifier();
    int active_id = game_get_active_user();
    bool allow_nobody = (mod == MODIFIER_BONUS || mod == MODIFIER_MALUS);

    /* Title + button colour depend on modifier type */
    lv_color_t col;
    switch (mod) {
        case MODIFIER_BONUS:
            lv_label_set_text(s_title_label,
                LV_SYMBOL_OK "  BONUS - Qui en profite ?");
            col = lv_color_hex(0x1B5E20); /* dark green */
            break;
        case MODIFIER_MALUS:
            lv_label_set_text(s_title_label,
                LV_SYMBOL_WARNING "  MALUS - Qui l'ecope ?");
            col = lv_color_hex(0x7F0000); /* dark red */
            break;
        case MODIFIER_TIMEOUT_ADD: {
            int mins = db_get_config("timeout_modifier_minutes", 5);
            char tbuf[64];
            lv_snprintf(tbuf, sizeof(tbuf),
                LV_SYMBOL_PAUSE "  +%d min - Qui attend ?", mins);
            lv_label_set_text(s_title_label, tbuf);
            col = lv_color_hex(0x7F3300); /* dark orange */
            break;
        }
        case MODIFIER_TIMEOUT_REMOVE: {
            int mins = db_get_config("timeout_modifier_minutes", 5);
            char tbuf[64];
            lv_snprintf(tbuf, sizeof(tbuf),
                LV_SYMBOL_PLAY "  -%d min - Qui en profite ?", mins);
            lv_label_set_text(s_title_label, tbuf);
            col = lv_color_hex(0x003D7F); /* dark blue */
            break;
        }
        default:
            lv_label_set_text(s_title_label, "Choisissez un joueur");
            col = lv_color_hex(0x444444);
            break;
    }

    lv_obj_clean(s_list_cont);

    if (allow_nobody) {
        lv_obj_t *btn_none = lv_btn_create(s_list_cont);
        lv_obj_set_size(btn_none, 210, 80);
        lv_obj_set_style_bg_color(btn_none, lv_color_hex(0x3F3F46), LV_PART_MAIN);
        lv_obj_set_style_radius(btn_none, 10, LV_PART_MAIN);
        lv_obj_add_event_cb(btn_none, on_target_clicked, LV_EVENT_CLICKED,
                            (void *)(intptr_t)-1);

        lv_obj_t *lbl_none = lv_label_create(btn_none);
        lv_label_set_text(lbl_none, "Personne");
        lv_obj_set_style_text_color(lbl_none, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
        lv_obj_align(lbl_none, LV_ALIGN_TOP_MID, 0, 6);

        lv_obj_t *sub_none = lv_label_create(btn_none);
        lv_label_set_text(sub_none, "Ne rien appliquer");
        lv_obj_set_style_text_color(sub_none, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
        lv_obj_align(sub_none, LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    user_record_t users[GAME_DB_MAX_USERS];
    int count = db_get_all_users(users, GAME_DB_MAX_USERS);

    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_btn_create(s_list_cont);
        lv_obj_set_size(btn, 210, 80);
        lv_obj_set_style_bg_color(btn, col, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, on_target_clicked, LV_EVENT_CLICKED,
                            (void *)(intptr_t)users[i].id);

        /* Name */
        char name_buf[80];
        if (users[i].id == active_id)
            lv_snprintf(name_buf, sizeof(name_buf), "%s (moi)", users[i].name);
        else
            lv_snprintf(name_buf, sizeof(name_buf), "%s", users[i].name);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, name_buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 6);

        /* Current modifier status */
        char info[32];
        lv_snprintf(info, sizeof(info), "B+%d M+%d",
                    users[i].bonus, users[i].malus);
        lv_obj_t *sub = lv_label_create(btn);
        lv_label_set_text(sub, info);
        lv_obj_set_style_text_color(sub, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
}
