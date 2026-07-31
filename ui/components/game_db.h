#ifndef GAME_DB_H
#define GAME_DB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define GAME_DB_MAX_USERS  32
#define GAME_DB_NAME_LEN   64

/* Multi-pump limits (must match firmware MAX_PUMPS). */
#define GAME_DB_MAX_PUMPS          6
#define GAME_DB_MAX_COCKTAILS      32
#define GAME_DB_COCKTAIL_NAME_LEN  48

typedef struct {
    int     id;
    char    name[GAME_DB_NAME_LEN];
    bool    is_admin;          /* true when this user may unlock settings by RFID */
    int     bonus;             /* active bonus  - shifts wheel values down */
    int     malus;             /* active malus  - shifts wheel values up   */
    int     wheel_trigger;     /* bonus-wheel appearance counter            */
    int     total_cl;          /* cumulative cL drank                       */
    int     given_modifier;    /* +1 = gave bonus, -1 = gave malus, 0 = none */
    int     given_to_id;       /* user-id of recipient, -1 = none           */
    int64_t last_spin_epoch;   /* Unix timestamp of last basic spin         */
    char    rfid_tag[32];      /* linked RFID UID hex string, or "" if none */
} user_record_t;

/**
 * Open (or create) the SQLite database at @p path.
 * Tables and default config are created if they don't exist.
 * Returns 0 on success, non-zero on failure.
 */
int  db_init(const char *path);
void db_close(void);

/* ── user CRUD ─────────────────────────────────────────────────────────── */

/** Fill @p buf with up to @p max_count rows ordered by name.
 *  Returns number of rows written. */
int  db_get_all_users(user_record_t *buf, int max_count);

/** Load a single user by id.  Returns 0 on success, -1 on failure. */
int  db_get_user(int id, user_record_t *out);

/** Insert a new user with the given name.
 *  Returns the new row id, or -1 on error. */
int  db_add_user(const char *name);

/** Persist all fields of @p u back to the DB.  Returns 0 on success. */
int  db_update_user(const user_record_t *u);

/** Delete a user by id.  Returns 0 on success. */
int  db_delete_user(int id);

/** Look up a user by RFID tag hex string.  Returns 0 on success, -1 if not found. */
int  db_get_user_by_rfid(const char *tag, user_record_t *out);

/** Set or clear the RFID tag for user @p user_id.
 *  Pass NULL or "" to unlink.  Returns 0 on success. */
int  db_set_user_rfid(int user_id, const char *tag);

/* ── cL history (for evolution graph) ─────────────────────────────────── */

typedef struct {
    int64_t epoch;      /* Unix timestamp when this total_cl was recorded */
    int     total_cl;   /* cumulative cL at that point in time            */
} cl_history_point_t;

/** Append a history point for @p user_id.  Returns 0 on success, -1 on error. */
int  db_add_cl_history(int user_id, int64_t epoch, int total_cl);

/** Fill @p buf with up to @p max_count history points for @p user_id,
 *  ordered by epoch ascending (oldest first).
 *  Returns number of rows written. */
int  db_get_cl_history(int user_id, cl_history_point_t *buf, int max_count);

/* ── config key-value ──────────────────────────────────────────────────── */

int  db_get_config(const char *key, int default_val);
int  db_set_config(const char *key, int value);

/* ── text config key-value ─────────────────────────────────────────────── */

int  db_get_text_config(const char *key, char *out, int out_size,
                        const char *default_val);
int  db_set_text_config(const char *key, const char *value);

/* ── cocktails ─────────────────────────────────────────────────────────── */

/** Per-pump quantity mode inside a cocktail recipe. */
typedef enum {
    COCKTAIL_QTY_CL      = 0,  /* value = absolute centiliters            */
    COCKTAIL_QTY_PERCENT = 1,  /* value = percent of the selected glass   */
    COCKTAIL_QTY_WHEEL   = 2   /* value ignored; quantity from wheel spin */
} cocktail_qty_mode_t;

typedef struct {
    int pump_index;   /* 1-based pump number                    */
    int mode;         /* cocktail_qty_mode_t                    */
    int value;        /* cL or percent depending on mode        */
} cocktail_pump_t;

typedef struct {
    int  id;
    char name[GAME_DB_COCKTAIL_NAME_LEN];
} cocktail_record_t;

/** Fill @p buf with up to @p max_count cocktails ordered by name.
 *  Returns number of rows written. */
int  db_get_all_cocktails(cocktail_record_t *buf, int max_count);

/** Insert a new cocktail. Returns the new row id, or -1 on error. */
int  db_add_cocktail(const char *name);

/** Delete a cocktail and its pump rows. Returns 0 on success. */
int  db_delete_cocktail(int id);

/** Fill @p buf with up to @p max_count pump rows for @p cocktail_id,
 *  ordered by pump_index. Returns number of rows written. */
int  db_get_cocktail_pumps(int cocktail_id, cocktail_pump_t *buf, int max_count);

/** Replace the whole pump list of @p cocktail_id with @p pumps.
 *  Returns 0 on success, -1 on error. */
int  db_set_cocktail_pumps(int cocktail_id, const cocktail_pump_t *pumps, int count);

#ifdef __cplusplus
}
#endif

#endif /* GAME_DB_H */