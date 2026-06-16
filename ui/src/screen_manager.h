#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

/* ── Display resolution ─────────────────────────────────────────────────── */
#ifndef SCREEN_W
#define SCREEN_W  1024
#endif

#ifndef SCREEN_H
#define SCREEN_H   600
#endif
/* Inner content width/height with standard 20 px margin on each side */
#define SCREEN_INNER_W  (SCREEN_W - 40)   /* 984 */
#define SCREEN_INNER_H  (SCREEN_H - 90)   /* 510, leaves room for title + footer */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_HOME = 0,
    SCREEN_PROFILE_SELECT,
    SCREEN_WHEEL,
    SCREEN_BONUS_WHEEL,
    SCREEN_GIVE_MODIFIER,
    SCREEN_LEADERBOARD,
    SCREEN_PARAMETERS,
    SCREEN_SAVING,
    SCREEN_COUNT
} screen_id_t;

void         screen_manager_init(void);
void         screen_manager_load(screen_id_t id);
screen_id_t  screen_manager_current(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_MANAGER_H */
