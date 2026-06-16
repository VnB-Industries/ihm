#include "scr_saving.h"
#include "screen_manager.h"
#include <stdlib.h>
#include <time.h>

static lv_obj_t    *s_screen;
static lv_obj_t    *s_logo_img;
static lv_timer_t  *s_anim_timer = NULL;
static int16_t      s_pos_x;
static int16_t      s_pos_y;
static int16_t      s_vel_x;
static int16_t      s_vel_y;
static uint16_t     s_img_w = 150;  /* Approximate logo width */
static uint16_t     s_img_h = 150;  /* Approximate logo height */

static void anim_timer_cb(lv_timer_t *t)
{
	(void)t;
	
	/* Update position with velocity */
	s_pos_x += s_vel_x;
	s_pos_y += s_vel_y;
	
	/* Screen bounds checking and bouncing */
	if (s_pos_x < 0) {
		s_pos_x = 0;
		s_vel_x = -s_vel_x;  /* Bounce horizontally */
	}
	if (s_pos_x + s_img_w > SCREEN_W) {
		s_pos_x = SCREEN_W - s_img_w;
		s_vel_x = -s_vel_x;
	}
	
	if (s_pos_y < 0) {
		s_pos_y = 0;
		s_vel_y = -s_vel_y;  /* Bounce vertically */
	}
	if (s_pos_y + s_img_h > SCREEN_H) {
		s_pos_y = SCREEN_H - s_img_h;
		s_vel_y = -s_vel_y;
	}
	
	/* Update image position */
	lv_obj_set_pos(s_logo_img, s_pos_x, s_pos_y);
}

static void init_random_velocity(void)
{
	/* Fixed velocity: slow movement (2 pixels/frame) */
	s_vel_x = 2;
	s_vel_y = 2;
}

static void on_wake_touch(lv_event_t *e)
{
	(void)e;
	screen_manager_load(SCREEN_HOME);
}

void scr_saving_init(void)
{
	s_screen = lv_obj_create(NULL);
	lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
	lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x05070B), LV_PART_MAIN);
	lv_obj_set_style_border_width(s_screen, 0, LV_PART_MAIN);
	lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(s_screen, on_wake_touch, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(s_screen, on_wake_touch, LV_EVENT_PRESSED, NULL);

	/* Create image from logo_veille.png */
	s_logo_img = lv_image_create(s_screen);
	lv_image_set_src(s_logo_img, "A:images/logo_veille.png");
	lv_obj_update_layout(s_screen);
	
	/* Get actual image dimensions */
	s_img_w = lv_obj_get_width(s_logo_img);
	s_img_h = lv_obj_get_height(s_logo_img);
	
	/* Initialize position to center */
	s_pos_x = (SCREEN_W - s_img_w) / 2;
	s_pos_y = (SCREEN_H - s_img_h) / 2;
	lv_obj_set_pos(s_logo_img, s_pos_x, s_pos_y);
	
	/* Initialize random velocity */
	init_random_velocity();
	
	/* Start animation timer (40ms = ~25fps) */
	s_anim_timer = lv_timer_create(anim_timer_cb, 40, NULL);
}

void scr_saving_refresh(void)
{
	/* No dynamic data for now. */
}

void scr_saving_deinit(void)
{
	if (s_anim_timer) {
		lv_timer_delete(s_anim_timer);
		s_anim_timer = NULL;
	}
}

lv_obj_t *scr_saving_get(void)
{
	return s_screen;
}
