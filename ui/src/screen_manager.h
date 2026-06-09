#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

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
    SCREEN_COUNT
} screen_id_t;

void         screen_manager_init(void);
void         screen_manager_load(screen_id_t id);
screen_id_t  screen_manager_current(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_MANAGER_H */
