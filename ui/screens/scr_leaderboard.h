#ifndef SCR_LEADERBOARD_H
#define SCR_LEADERBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

void      scr_leaderboard_init(void);
lv_obj_t *scr_leaderboard_get(void);
void      scr_leaderboard_refresh(void); /* reload stats from DB */

#ifdef __cplusplus
}
#endif

#endif /* SCR_LEADERBOARD_H */