#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "game_db.h"

/* This header and game_logic.c deliberately do not include lvgl.h.  The game
 * rules are the part worth testing on a host without a display, so they speak
 * in plain structs and own their own RNG.  Screens adapt game_segment_t to
 * whatever the wheel widget wants. */

#define GAME_MAX_SEGMENTS 12

/* One wheel slice, display-toolkit agnostic. */
typedef struct {
    const char *label;      /* short text drawn on the slice              */
    const char *image_src;  /* optional image path, NULL for none         */
    uint32_t    color_rgb;  /* 0xRRGGBB                                   */
    uint32_t    value;      /* cL for the main wheel, a token for the bonus */
} game_segment_t;

/* ── modifier types ─────────────────────────────────────────────────────── */

typedef enum {
    MODIFIER_NONE           =  0,
    MODIFIER_BONUS          =  1,
    MODIFIER_MALUS          = -1,
    MODIFIER_TIMEOUT_ADD    =  2,   /* add time to target's spin cooldown (malus) */
    MODIFIER_TIMEOUT_REMOVE = -2    /* remove time from target's spin cooldown (bonus) */
} modifier_type_t;

/* Token values stored in bonus-wheel segments */
#define BONUS_WHEEL_VAL_NOTHING         0u
#define BONUS_WHEEL_VAL_BONUS           1u
#define BONUS_WHEEL_VAL_MALUS           2u
#define BONUS_WHEEL_VAL_TIMEOUT_ADD     3u
#define BONUS_WHEEL_VAL_TIMEOUT_REMOVE  4u

/* ── random numbers ─────────────────────────────────────────────────────── */

/** Seed the game RNG.  Must be called once at startup with something that
 *  changes between boots, otherwise every power-up replays the same spins.
 *  A @p seed of 0 is replaced with a non-zero constant. */
void game_rng_seed(uint32_t seed);

/** Uniform random integer in [@p min, @p max] inclusive.
 *  Rejection-sampled, so unlike a plain modulo it does not favour low values
 *  when the span does not divide 2^32. */
uint32_t game_rand(uint32_t min, uint32_t max);

/* ── wheel segment builders ─────────────────────────────────────────────── */

/**
 * Compute main wheel segments for @p u.
 * Base: 0, 2, 4, 6 cL.  Net shift = (malus – bonus) × 2 cL, floor 0.
 * Writes up to 4 segments into @p segs and sets *count.
 */
void game_compute_wheel_segments(const user_record_t *u,
                                 game_segment_t *segs, uint8_t *count);

/**
 * Build bonus-wheel segments (BONUS / RIEN / MALUS / ±Temps) from
 * game_config weights.  Writes segments into @p segs and sets *count.
 */
void game_compute_bonus_segments(game_segment_t *segs, uint8_t *count);

/* ── spin cooldown ──────────────────────────────────────────────────────── */

/** Seconds @p u must still wait before spinning again; <= 0 means "now".
 *  Reads spin_blocked_until only, so a pending ±Temps modifier and the
 *  per-spin cooldown are both honoured by the same check. */
int64_t game_spin_remaining_seconds(const user_record_t *u);

/* ── spin result handlers ───────────────────────────────────────────────── */

/**
 * Record that @p user_id has just spun the main wheel: stamps
 * last_spin_epoch and arms the configured per-spin cooldown.
 * Call once the controller shows the pour actually started, so a pour it
 * refused (no glass, link down) does not cost the player their turn.
 */
void game_on_spin_started(int user_id);

/**
 * Credit a finished pour to @p user_id.
 * @p measured_cl is what the controller reported actually dispensed, which is
 * what the leaderboard must count: a pour cut short by the glass being lifted
 * delivers less than the wheel promised.
 * Also rolls for a bonus-wheel trigger.
 * @return true if the bonus wheel should appear for this user.
 */
bool game_on_dispense_complete(int user_id, int measured_cl);

/**
 * Called after bonus wheel spin completes.
 * Decrements wheel_trigger, returns the modifier obtained.
 */
modifier_type_t game_on_bonus_spin(int user_id, uint16_t seg_index,
                                    const game_segment_t *segs, uint8_t seg_count);

/**
 * Apply a modifier: records on giver, increments bonus/malus on target.
 */
void game_apply_modifier(int giver_id, int target_id, modifier_type_t type);

/**
 * Leaderboard place of @p users[idx] by total_cl: 1 + the number of users with
 * strictly more cL, so tied players share a place.  A player with 0 cL has no
 * place (returns 0): before anyone has poured, nobody is on the podium.
 */
int game_cl_rank(const user_record_t *users, int count, int idx);

/* ── session state ──────────────────────────────────────────────────────── */

int             game_get_active_user(void);
void            game_set_active_user(int user_id);

modifier_type_t game_get_pending_modifier(void);
void            game_set_pending_modifier(modifier_type_t m);

/* Set when a modifier has just been handed out, so the wheel screen announces
 * it once instead of on every later visit.  The take call returns the flag
 * and clears it. */
void            game_set_modifier_announce(void);
bool            game_take_modifier_announce(void);

int             game_get_selected_glass_cl(void);
void            game_set_selected_glass_cl(int glass_cl);
int             game_get_selected_cocktail_id(void);
void            game_set_selected_cocktail_id(int id);

#ifdef __cplusplus
}
#endif

#endif /* GAME_LOGIC_H */
