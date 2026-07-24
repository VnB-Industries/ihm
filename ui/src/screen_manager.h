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

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SCREEN_HOME = 0,
    SCREEN_PROFILE_SELECT,
    SCREEN_GLASS_SELECT,
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

/* ── RFID enrollment ─────────────────────────────────────────────── */

typedef enum {
    RFID_ENROLL_OK,       /* tag successfully linked               */
    RFID_ENROLL_TAKEN,    /* tag already belongs to another user   */
    RFID_ENROLL_TIMEOUT   /* 30 s window elapsed without a scan    */
} rfid_enroll_result_t;

/** Start an RFID enrollment window for @p user_id.
 *  @p cb is called exactly once with the result. */
void screen_manager_rfid_enroll_start(int user_id, uint32_t timeout_ms,
                                      void (*cb)(rfid_enroll_result_t));

/** Cancel an in-progress enrollment (safe to call when none is active). */
void screen_manager_rfid_enroll_cancel(void);

/* Enable/disable periodic RFID serial polling.
 * Useful when another flow needs exclusive access to serial telemetry. */
void screen_manager_set_rfid_poll_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_MANAGER_H */
