/* db.h
 *
 * A small door onto a local database file. Nothing above this directory
 * ever sees a SQLite type, so swapping the engine touches this directory
 * alone.
 *
 * Every call returns GS_OK or a negative code from gravestone.h. The last
 * message from the engine is available through gs_db_error.
 */
#ifndef GS_LIB_DB_H
#define GS_LIB_DB_H

typedef struct gs_db gs_db_t;
typedef struct gs_db_stmt gs_db_stmt_t;

/* Opens the file, creating it when absent. Journalling is set so a power
 * cut during a write leaves the file readable. */
gs_db_t *gs_db_open(const char *path);
void     gs_db_close(gs_db_t *db);

/* Runs one or more statements that return nothing. */
int gs_db_exec(gs_db_t *db, const char *sql);

const char *gs_db_error(const gs_db_t *db);
long long   gs_db_last_insert_id(const gs_db_t *db);
long long   gs_db_changes(const gs_db_t *db);

/* Several writes that must all land or none of them. */
int gs_db_begin(gs_db_t *db);
int gs_db_commit(gs_db_t *db);
int gs_db_rollback(gs_db_t *db);

/* A statement with holes in it, filled in before running. Values are bound
 * by position, counting from one, which is what the engine expects. */
gs_db_stmt_t *gs_db_prepare(gs_db_t *db, const char *sql);
void          gs_db_finalise(gs_db_stmt_t *stmt);

int gs_db_bind_text(gs_db_stmt_t *stmt, int position, const char *value);
int gs_db_bind_int(gs_db_stmt_t *stmt, int position, long long value);
int gs_db_bind_blob(gs_db_stmt_t *stmt, int position, const void *data,
                    int bytes);
int gs_db_bind_null(gs_db_stmt_t *stmt, int position);

/* Returns 1 when a row is ready, 0 when the statement is finished, and a
 * negative code on failure. Columns count from zero. */
int gs_db_step(gs_db_stmt_t *stmt);
int gs_db_reset(gs_db_stmt_t *stmt);

const char *gs_db_text(gs_db_stmt_t *stmt, int column);
long long   gs_db_int(gs_db_stmt_t *stmt, int column);
const void *gs_db_blob(gs_db_stmt_t *stmt, int column, int *bytes);
int         gs_db_is_null(gs_db_stmt_t *stmt, int column);

/* The engine version, for logging and for the vendor note. */
const char *gs_db_engine_version(void);

#endif
