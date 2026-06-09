#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "game_db.h"
#include "wheel_fortune.h"

/* ── modifier types ─────────────────────────────────────────────────────── */

typedef enum {
    MODIFIER_NONE  =  0,
    MODIFIER_BONUS =  1,
    MODIFIER_MALUS = -1
} modifier_type_t;

/* Token values stored in bonus-wheel segments */
#define BONUS_WHEEL_VAL_NOTHING  0u
#define BONUS_WHEEL_VAL_BONUS    1u
#define BONUS_WHEEL_VAL_MALUS    2u

/* ── wheel segment builders ─────────────────────────────────────────────── */

/**
 * Compute main wheel segments for @p u.
 * Base: 0, 2, 4, 6 cL.  Net shift = (malus – bonus) × 2 cL, floor 0.
 * Writes up to 4 segments into @p segs and sets *count.
 */
void game_compute_wheel_segments(const user_record_t *u,
                                 wf_segment_t *segs, uint8_t *count);

/**
 * Build bonus-wheel segments (BONUS / RIEN / MALUS) from game_config weights.
 * Writes segments into @p segs and sets *count.
 */
void game_compute_bonus_segments(wf_segment_t *segs, uint8_t *count);

/* ── spin result handlers ───────────────────────────────────────────────── */

/**
 * Called after basic wheel spin completes.
 * Records @p cl_value to user stats, rolls for bonus-wheel trigger.
 * @return true if bonus wheel should appear for this user.
 */
bool game_on_basic_spin(int user_id, int cl_value);

/**
 * Called after bonus wheel spin completes.
 * Decrements wheel_trigger, returns the modifier obtained.
 */
modifier_type_t game_on_bonus_spin(int user_id, uint16_t seg_index,
                                    const wf_segment_t *segs, uint8_t seg_count);

/**
 * Apply a modifier: records on giver, increments bonus/malus on target.
 */
void game_apply_modifier(int giver_id, int target_id, modifier_type_t type);

/* ── session state ──────────────────────────────────────────────────────── */

int             game_get_active_user(void);
void            game_set_active_user(int user_id);

modifier_type_t game_get_pending_modifier(void);
void            game_set_pending_modifier(modifier_type_t m);

#ifdef __cplusplus
}
#endif

#endif /* GAME_LOGIC_H */
