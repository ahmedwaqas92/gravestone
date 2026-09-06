/* db.c
 *
 * The wrapper over the vendored SQLite amalgamation. This is the only file
 * in the project that includes sqlite3.h.
 */
#include "db.h"
#include "sqlite3.h"
#include "gravestone.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct gs_db {
    sqlite3 *handle;
    char error[256];
};

struct gs_db_stmt {
    sqlite3_stmt *handle;
    gs_db_t *owner;
};

static void remember_error(gs_db_t *db)
{
    const char *msg;

    if (db == NULL)
        return;
    msg = db->handle != NULL ? sqlite3_errmsg(db->handle) : "no database";
    strncpy(db->error, msg != NULL ? msg : "unknown", sizeof db->error - 1);
    db->error[sizeof db->error - 1] = '\0';
}

gs_db_t *gs_db_open(const char *path)
{
    gs_db_t *db;
    int rc;

    if (path == NULL || path[0] == '\0') {
        gs_log_error("db: gs_db_open needs a path");
        return NULL;
    }

    db = calloc(1, sizeof *db);
    if (db == NULL)
        return NULL;

    rc = sqlite3_open_v2(path, &db->handle,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        gs_log_error("db: cannot open %s: %s", path,
                     db->handle != NULL ? sqlite3_errmsg(db->handle)
                                        : sqlite3_errstr(rc));
        sqlite3_close(db->handle);
        free(db);
        return NULL;
    }

    /* The engine creates the file with whatever the umask allows, which is
     * commonly world readable. This file holds task descriptions and the
     * code models write, so it is narrowed to the owner alone. */
    if (chmod(path, S_IRUSR | S_IWUSR) != 0)
        gs_log_warn("db: could not narrow the permissions on %s", path);

    /* The write ahead log keeps readers working while a write is in
     * progress, and leaves the file readable after a power cut. Foreign
     * keys are off by default in SQLite, which surprises people. */
    if (gs_db_exec(db, "PRAGMA journal_mode = WAL;"
                       "PRAGMA synchronous = NORMAL;"
                       "PRAGMA foreign_keys = ON;"
                       "PRAGMA busy_timeout = 5000;") != GS_OK) {
        gs_log_error("db: cannot set up %s: %s", path, db->error);
        gs_db_close(db);
        return NULL;
    }

    gs_log_debug("db: opened %s with engine %s", path, sqlite3_libversion());
    return db;
}

void gs_db_close(gs_db_t *db)
{
    if (db == NULL)
        return;
    if (db->handle != NULL)
        sqlite3_close_v2(db->handle);
    free(db);
}

int gs_db_exec(gs_db_t *db, const char *sql)
{
    char *message = NULL;
    int rc;

    if (db == NULL || sql == NULL)
        return GS_ERR_ARG;

    rc = sqlite3_exec(db->handle, sql, NULL, NULL, &message);
    if (rc != SQLITE_OK) {
        strncpy(db->error, message != NULL ? message : sqlite3_errstr(rc),
                sizeof db->error - 1);
        db->error[sizeof db->error - 1] = '\0';
        sqlite3_free(message);
        return GS_ERR;
    }
    sqlite3_free(message);
    return GS_OK;
}

const char *gs_db_error(const gs_db_t *db)
{
    return db != NULL ? db->error : "no database";
}

long long gs_db_last_insert_id(const gs_db_t *db)
{
    return db != NULL ? (long long)sqlite3_last_insert_rowid(db->handle) : 0;
}

long long gs_db_changes(const gs_db_t *db)
{
    return db != NULL ? (long long)sqlite3_changes64(db->handle) : 0;
}

int gs_db_begin(gs_db_t *db)
{
    return gs_db_exec(db, "BEGIN IMMEDIATE;");
}

int gs_db_commit(gs_db_t *db)
{
    return gs_db_exec(db, "COMMIT;");
}

int gs_db_rollback(gs_db_t *db)
{
    return gs_db_exec(db, "ROLLBACK;");
}

gs_db_stmt_t *gs_db_prepare(gs_db_t *db, const char *sql)
{
    gs_db_stmt_t *stmt;

    if (db == NULL || sql == NULL)
        return NULL;

    stmt = calloc(1, sizeof *stmt);
    if (stmt == NULL)
        return NULL;

    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt->handle,
                           NULL) != SQLITE_OK) {
        remember_error(db);
        gs_log_error("db: cannot prepare statement: %s", db->error);
        free(stmt);
        return NULL;
    }
    stmt->owner = db;
    return stmt;
}

void gs_db_finalise(gs_db_stmt_t *stmt)
{
    if (stmt == NULL)
        return;
    sqlite3_finalize(stmt->handle);
    free(stmt);
}

int gs_db_bind_text(gs_db_stmt_t *stmt, int position, const char *value)
{
    if (stmt == NULL)
        return GS_ERR_ARG;
    if (value == NULL)
        return gs_db_bind_null(stmt, position);
    /* SQLITE_TRANSIENT tells the engine to keep its own copy, so the
     * caller may free the string straight away. */
    return sqlite3_bind_text(stmt->handle, position, value, -1,
                             SQLITE_TRANSIENT) == SQLITE_OK
           ? GS_OK : GS_ERR;
}

int gs_db_bind_int(gs_db_stmt_t *stmt, int position, long long value)
{
    if (stmt == NULL)
        return GS_ERR_ARG;
    return sqlite3_bind_int64(stmt->handle, position,
                              (sqlite3_int64)value) == SQLITE_OK
           ? GS_OK : GS_ERR;
}

int gs_db_bind_blob(gs_db_stmt_t *stmt, int position, const void *data,
                    int bytes)
{
    if (stmt == NULL)
        return GS_ERR_ARG;
    if (data == NULL || bytes < 0)
        return gs_db_bind_null(stmt, position);
    return sqlite3_bind_blob(stmt->handle, position, data, bytes,
                             SQLITE_TRANSIENT) == SQLITE_OK
           ? GS_OK : GS_ERR;
}

int gs_db_bind_null(gs_db_stmt_t *stmt, int position)
{
    if (stmt == NULL)
        return GS_ERR_ARG;
    return sqlite3_bind_null(stmt->handle, position) == SQLITE_OK
           ? GS_OK : GS_ERR;
}

int gs_db_step(gs_db_stmt_t *stmt)
{
    int rc;

    if (stmt == NULL)
        return GS_ERR_ARG;

    rc = sqlite3_step(stmt->handle);
    if (rc == SQLITE_ROW)
        return 1;
    if (rc == SQLITE_DONE)
        return 0;

    if (stmt->owner != NULL) {
        remember_error(stmt->owner);
        gs_log_error("db: statement failed: %s", stmt->owner->error);
    }
    return GS_ERR;
}

int gs_db_reset(gs_db_stmt_t *stmt)
{
    if (stmt == NULL)
        return GS_ERR_ARG;
    sqlite3_clear_bindings(stmt->handle);
    return sqlite3_reset(stmt->handle) == SQLITE_OK ? GS_OK : GS_ERR;
}

const char *gs_db_text(gs_db_stmt_t *stmt, int column)
{
    const unsigned char *value;

    if (stmt == NULL)
        return NULL;
    value = sqlite3_column_text(stmt->handle, column);
    return (const char *)value;
}

long long gs_db_int(gs_db_stmt_t *stmt, int column)
{
    return stmt != NULL
           ? (long long)sqlite3_column_int64(stmt->handle, column) : 0;
}

const void *gs_db_blob(gs_db_stmt_t *stmt, int column, int *bytes)
{
    const void *value;

    if (stmt == NULL) {
        if (bytes != NULL)
            *bytes = 0;
        return NULL;
    }
    value = sqlite3_column_blob(stmt->handle, column);
    if (bytes != NULL)
        *bytes = sqlite3_column_bytes(stmt->handle, column);
    return value;
}

int gs_db_is_null(gs_db_stmt_t *stmt, int column)
{
    return stmt != NULL
           ? sqlite3_column_type(stmt->handle, column) == SQLITE_NULL : 1;
}

const char *gs_db_engine_version(void)
{
    return sqlite3_libversion();
}
