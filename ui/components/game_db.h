#ifndef GAME_DB_H
#define GAME_DB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define GAME_DB_MAX_USERS  32
#define GAME_DB_NAME_LEN   64

typedef struct {
    int  id;
    char name[GAME_DB_NAME_LEN];
    int  bonus;          /* active bonus  – shifts wheel values down */
    int  malus;          /* active malus  – shifts wheel values up   */
    int  wheel_trigger;  /* bonus-wheel appearance counter            */
    int  total_cl;       /* cumulative cL drank                       */
    int  given_modifier; /* +1 = gave bonus, -1 = gave malus, 0 = none */
    int  given_to_id;    /* user-id of recipient, -1 = none           */
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

/* ── config key-value ──────────────────────────────────────────────────── */

int  db_get_config(const char *key, int default_val);
int  db_set_config(const char *key, int value);

#ifdef __cplusplus
}
#endif

#endif /* GAME_DB_H */
