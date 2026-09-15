/* schema.c
 *
 * The tables this module keeps, and the steps that bring a file written by
 * an older build up to date.
 */
#include "store_internal.h"
#include "gravestone.h"
#include "db.h"
#include "log.h"

#include <stddef.h>

const char *gs_store_schema =
    "CREATE TABLE IF NOT EXISTS install ("
    "  id          TEXT PRIMARY KEY,"
    "  created_at  INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS snapshot ("
    "  id           INTEGER PRIMARY KEY,"
    "  install_id   TEXT NOT NULL REFERENCES install(id),"
    "  fingerprint  TEXT NOT NULL,"
    "  taken_at     INTEGER NOT NULL,"
    "  last_seen_at INTEGER NOT NULL,"
    "  os           TEXT NOT NULL,"
    "  kernel       TEXT NOT NULL,"
    "  arch         TEXT NOT NULL,"
    "  cpu_model    TEXT NOT NULL,"
    "  cpu_cores    INTEGER NOT NULL,"
    "  ram_bytes    INTEGER NOT NULL,"
    "  disk_total   INTEGER NOT NULL,"
    "  disk_free    INTEGER NOT NULL,"
    "  gpu          TEXT NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS snapshot_by_time"
    "  ON snapshot(install_id, taken_at DESC);"
    "CREATE TABLE IF NOT EXISTS setting ("
    "  key        TEXT PRIMARY KEY,"
    "  value      TEXT NOT NULL,"
    "  updated_at INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS known_model ("
    "  id      INTEGER PRIMARY KEY,"
    "  name    TEXT NOT NULL UNIQUE,"
    "  path    TEXT NOT NULL,"
    "  root    TEXT NOT NULL,"
    "  bytes   INTEGER NOT NULL,"
    "  seen_at INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS snapshot_disk ("
    "  id          INTEGER PRIMARY KEY,"
    "  snapshot_id INTEGER NOT NULL REFERENCES snapshot(id) ON DELETE CASCADE,"
    "  position    INTEGER NOT NULL,"
    "  mount       TEXT NOT NULL,"
    "  device      TEXT NOT NULL,"
    "  fs          TEXT NOT NULL,"
    "  total_bytes INTEGER NOT NULL,"
    "  free_bytes  INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS disk_by_snapshot"
    "  ON snapshot_disk(snapshot_id, position);";

/* A database written by an older build lacks the newest columns. Each step
 * runs once, and the version stamped in the file records how far it got. */
int gs_store_migrate(gs_db_t *handle, int *cleared)
{
    gs_db_stmt_t *stmt;
    long long version = 0;

    stmt = gs_db_prepare(handle, "PRAGMA user_version;");
    if (stmt == NULL)
        return GS_ERR;
    if (gs_db_step(stmt) == 1)
        version = gs_db_int(stmt, 0);
    gs_db_finalise(stmt);

    if (version < 2) {
        /* Added when the graphics card started reporting its memory. An
         * error here means the column is already present, which is fine. */
        gs_db_exec(handle,
            "ALTER TABLE snapshot ADD COLUMN gpu_memory INTEGER NOT NULL"
            " DEFAULT 0;");
        if (gs_db_exec(handle, "PRAGMA user_version = 2;") != GS_OK)
            return GS_ERR;
        gs_log_info("store: database moved from version %lld to 2", version);
    }

    if (version < 3) {
        /* Rows written before the drives and the graphics card were read
         * carry neither, and their fingerprint came from a different set
         * of fields, so they cannot be compared against a new reading.
         * Removing them makes the next mount record the machine properly
         * rather than reporting a hardware change that never happened. */
        if (gs_db_exec(handle,
                "DELETE FROM snapshot WHERE id NOT IN"
                " (SELECT DISTINCT snapshot_id FROM snapshot_disk);") != GS_OK)
            return GS_ERR;
        if (gs_db_exec(handle, "PRAGMA user_version = 3;") != GS_OK)
            return GS_ERR;
        if (gs_db_changes(handle) > 0) {
            if (cleared != NULL)
                *cleared = 1;
            gs_log_info("store: cleared %lld reading(s) taken before the "
                        "drives and the graphics card were recorded",
                        gs_db_changes(handle));
        }
    }
    if (version < 4) {
        /* Settings and the model inventory arrived together, so a file
         * written before them has neither table. Creating the schema
         * above already made both, and the version marks the file as
         * carrying them. */
        if (gs_db_exec(handle, "PRAGMA user_version = 4;") != GS_OK)
            return GS_ERR;
        gs_log_info("store: database moved from version %lld to 4", version);
    }
    return GS_OK;
}
