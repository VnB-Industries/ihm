#include "game_db.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static sqlite3 *s_db = NULL;

/* ── internal helpers ───────────────────────────────────────────────────── */

static int exec_sql(const char *sql)
{
    char *err = NULL;
    int rc = sqlite3_exec(s_db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] SQL error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
    }
    return rc;
}

static void fill_user(sqlite3_stmt *stmt, user_record_t *u)
{
    u->id             = sqlite3_column_int(stmt, 0);
    const char *n     = (const char *)sqlite3_column_text(stmt, 1);
    strncpy(u->name, n ? n : "", GAME_DB_NAME_LEN - 1);
    u->name[GAME_DB_NAME_LEN - 1] = '\0';
    u->bonus          = sqlite3_column_int(stmt, 2);
    u->malus          = sqlite3_column_int(stmt, 3);
    u->wheel_trigger  = sqlite3_column_int(stmt, 4);
    u->total_cl       = sqlite3_column_int(stmt, 5);
    u->given_modifier = sqlite3_column_int(stmt, 6);
    u->given_to_id    = sqlite3_column_int(stmt, 7);
}

static const char k_user_select[] =
    "SELECT id, name, bonus, malus, wheel_trigger, total_cl,"
    "       given_modifier, given_to_id FROM users";

/* ── public API ─────────────────────────────────────────────────────────── */

int db_init(const char *path)
{
    if (sqlite3_open(path, &s_db) != SQLITE_OK) {
        fprintf(stderr, "[db] Cannot open %s: %s\n", path, sqlite3_errmsg(s_db));
        return -1;
    }

    exec_sql(
        "CREATE TABLE IF NOT EXISTS users ("
        "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name           TEXT    NOT NULL UNIQUE COLLATE NOCASE,"
        "  bonus          INTEGER NOT NULL DEFAULT 0,"
        "  malus          INTEGER NOT NULL DEFAULT 0,"
        "  wheel_trigger  INTEGER NOT NULL DEFAULT 0,"
        "  total_cl       INTEGER NOT NULL DEFAULT 0,"
        "  given_modifier INTEGER NOT NULL DEFAULT 0,"
        "  given_to_id    INTEGER NOT NULL DEFAULT -1"
        ");"
    );

    exec_sql(
        "CREATE TABLE IF NOT EXISTS game_config ("
        "  key   TEXT    PRIMARY KEY,"
        "  value INTEGER NOT NULL DEFAULT 0"
        ");"
    );

    /* Insert default config values (ignored if already present) */
    exec_sql(
        "INSERT OR IGNORE INTO game_config (key, value) VALUES"
        "  ('wheel_trigger_chance',       20),"
        "  ('bonus_wheel_bonus_weight',    1),"
        "  ('bonus_wheel_nothing_weight',  2),"
        "  ('bonus_wheel_malus_weight',    1);"
    );

    return 0;
}

void db_close(void)
{
    if (s_db) { sqlite3_close(s_db); s_db = NULL; }
}

int db_get_all_users(user_record_t *buf, int max_count)
{
    if (!s_db || !buf || max_count <= 0) return 0;

    char sql[512];
    snprintf(sql, sizeof(sql), "%s ORDER BY name;", k_user_select);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && n < max_count)
        fill_user(stmt, &buf[n++]);

    sqlite3_finalize(stmt);
    return n;
}

int db_get_user(int id, user_record_t *out)
{
    if (!s_db || !out) return -1;

    char sql[512];
    snprintf(sql, sizeof(sql), "%s WHERE id = ?;", k_user_select);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, id);

    int rc = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) { fill_user(stmt, out); rc = 0; }
    sqlite3_finalize(stmt);
    return rc;
}

int db_add_user(const char *name)
{
    if (!s_db || !name || name[0] == '\0') return -1;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s_db, "INSERT INTO users (name) VALUES (?);",
                            -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    int new_id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE)
        new_id = (int)sqlite3_last_insert_rowid(s_db);
    sqlite3_finalize(stmt);
    return new_id;
}

int db_update_user(const user_record_t *u)
{
    if (!s_db || !u) return -1;

    const char *sql =
        "UPDATE users SET bonus=?, malus=?, wheel_trigger=?, total_cl=?,"
        "                 given_modifier=?, given_to_id=? WHERE id=?;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, u->bonus);
    sqlite3_bind_int(stmt, 2, u->malus);
    sqlite3_bind_int(stmt, 3, u->wheel_trigger);
    sqlite3_bind_int(stmt, 4, u->total_cl);
    sqlite3_bind_int(stmt, 5, u->given_modifier);
    sqlite3_bind_int(stmt, 6, u->given_to_id);
    sqlite3_bind_int(stmt, 7, u->id);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_finalize(stmt);
    return rc;
}

int db_delete_user(int id)
{
    if (!s_db) return -1;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s_db, "DELETE FROM users WHERE id=?;",
                            -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, id);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_finalize(stmt);
    return rc;
}

int db_get_config(const char *key, int default_val)
{
    if (!s_db || !key) return default_val;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s_db, "SELECT value FROM game_config WHERE key=?;",
                            -1, &stmt, NULL) != SQLITE_OK) return default_val;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);

    int val = default_val;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        val = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return val;
}

int db_set_config(const char *key, int value)
{
    if (!s_db || !key) return -1;

    const char *sql =
        "INSERT OR REPLACE INTO game_config (key, value) VALUES (?,?);";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, value);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_finalize(stmt);
    return rc;
}
