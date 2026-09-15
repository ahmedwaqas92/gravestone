/* mount_store.c
 *
 * The list of drives this machine had attached, kept in the same
 * database file the rest of the program uses. Reached through the
 * database wrapper rather than through another module, because a wrapper
 * is the only thing allowed to speak to the engine.
 *
 * Kept apart from mount.c so neither file grows past what fits in a head.
 *
 * Every row is treated as text somebody could have edited. The letter is
 * the only field recovered from a row, it has to be one capital A to Z,
 * and the mount point is worked out from it and compared against what the
 * row claims. A row failing either check is dropped whole.
 */
#include "mount.h"
#include "mount_internal.h"

#include "gravestone.h"
#include "db.h"
#include "log.h"
#include "paths.h"
#include "str.h"

#include <string.h>
#include <time.h>

gs_db_t *gs_mount_db;

/* The table is made if it is missing rather than through a numbered
 * step, since the settings side of this file owns the number and two
 * owners of one number would fight over it. */
static const char *const schema =
    "CREATE TABLE IF NOT EXISTS mount_drive ("
    "  letter  TEXT PRIMARY KEY,"
    "  point   TEXT NOT NULL,"
    "  distro  TEXT NOT NULL DEFAULT '',"
    "  seen_at INTEGER NOT NULL,"
    "  misses  INTEGER NOT NULL DEFAULT 0"
    ");";

long long gs_mount_now(void)
{
    return (long long)time(NULL);
}

int gs_mount_open(void)
{
    char path[512];

    if (gs_mount_db != NULL)
        return GS_OK;
    if (gs_paths_db_file(path, sizeof path) != GS_OK)
        return GS_ERR;

    gs_mount_db = gs_db_open(path);
    if (gs_mount_db == NULL)
        return GS_ERR;

    if (gs_db_exec(gs_mount_db, schema) != GS_OK) {
        gs_log_error("mount: %s", gs_db_error(gs_mount_db));
        gs_db_close(gs_mount_db);
        gs_mount_db = NULL;
        return GS_ERR;
    }

    /* A table made before this column existed keeps its old shape, since
     * CREATE TABLE IF NOT EXISTS leaves a table that is already there
     * alone. Adding the column a second time fails, and that failure is
     * the answer that it is already present, so it is swallowed. */
    (void)gs_db_exec(gs_mount_db,
                     "ALTER TABLE mount_drive ADD COLUMN declined INTEGER"
                     " NOT NULL DEFAULT 0;");
    return GS_OK;
}

void gs_mount_close(void)
{
    if (gs_mount_db == NULL)
        return;
    gs_db_close(gs_mount_db);
    gs_mount_db = NULL;
}

int gs_mount_remember(const gs_mount_drive_t *drive)
{
    gs_db_stmt_t *stmt;
    char letter[2];
    char point[16];
    char distro[72];
    int rc;

    if (gs_mount_db == NULL || drive == NULL)
        return GS_ERR_ARG;
    if (!gs_mount_letter_ok(drive->letter))
        return GS_ERR_ARG;
    if (gs_mount_point_for(drive->letter, point, sizeof point) != GS_OK)
        return GS_ERR_ARG;

    letter[0] = drive->letter;
    letter[1] = '\0';
    gs_mount_plain(drive->distro, distro, sizeof distro);

    stmt = gs_db_prepare(gs_mount_db,
        "INSERT INTO mount_drive (letter, point, distro, seen_at, misses)"
        " VALUES (?, ?, ?, ?, 0)"
        " ON CONFLICT(letter) DO UPDATE SET point = excluded.point,"
        " distro = excluded.distro, seen_at = excluded.seen_at,"
        " misses = 0;");
    if (stmt == NULL)
        return GS_ERR;

    gs_db_bind_text(stmt, 1, letter);
    gs_db_bind_text(stmt, 2, point);
    gs_db_bind_text(stmt, 3, distro);
    gs_db_bind_int(stmt, 4, drive->seen_at > 0 ? drive->seen_at
                                               : gs_mount_now());
    rc = gs_db_step(stmt) < 0 ? GS_ERR : GS_OK;
    gs_db_finalise(stmt);
    return rc;
}

int gs_mount_decline(char letter)
{
    gs_db_stmt_t *stmt;
    char text[2];
    char point[16];
    int rc;

    if (gs_mount_db == NULL)
        return GS_ERR_ARG;
    if (!gs_mount_letter_ok(letter))
        return GS_ERR_ARG;
    if (gs_mount_point_for(letter, point, sizeof point) != GS_OK)
        return GS_ERR_ARG;

    text[0] = letter;
    text[1] = '\0';
    stmt = gs_db_prepare(gs_mount_db,
        "INSERT INTO mount_drive (letter, point, distro, seen_at, misses,"
        " declined) VALUES (?, ?, '', 0, 0, 1)"
        " ON CONFLICT(letter) DO UPDATE SET declined = 1;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_text(stmt, 1, text);
    gs_db_bind_text(stmt, 2, point);
    rc = gs_db_step(stmt) < 0 ? GS_ERR : GS_OK;
    gs_db_finalise(stmt);
    return rc;
}

int gs_mount_declined(char letter)
{
    gs_db_stmt_t *stmt;
    char text[2];
    int said_no = 0;

    if (gs_mount_db == NULL || !gs_mount_letter_ok(letter))
        return 0;
    text[0] = letter;
    text[1] = '\0';

    stmt = gs_db_prepare(gs_mount_db,
        "SELECT declined FROM mount_drive WHERE letter = ?;");
    if (stmt == NULL)
        return 0;
    gs_db_bind_text(stmt, 1, text);
    if (gs_db_step(stmt) == 1)
        said_no = gs_db_int(stmt, 0) != 0;
    gs_db_finalise(stmt);
    return said_no;
}

int gs_mount_recall(gs_mount_drive_t *out, int max)
{
    gs_db_stmt_t *stmt;
    int count = 0;

    if (out == NULL || max <= 0)
        return GS_ERR_ARG;
    memset(out, 0, sizeof *out * (size_t)max);
    if (gs_mount_db == NULL)
        return 0;

    stmt = gs_db_prepare(gs_mount_db,
        "SELECT letter, point, distro, seen_at, misses FROM mount_drive"
        " ORDER BY misses ASC, seen_at DESC;");
    if (stmt == NULL)
        return 0;

    while (count < max && gs_db_step(stmt) == 1) {
        gs_mount_drive_t *d = &out[count];
        const char *text = gs_db_text(stmt, 0);
        char stored[GS_MOUNT_POINT_MAX];

        /* The letter is the only thing recovered from the row, and a row
         * carrying anything other than one capital A to Z is dropped
         * whole rather than repaired. */
        if (text == NULL || text[0] == '\0' || text[1] != '\0')
            continue;
        if (!gs_mount_letter_ok(text[0]))
            continue;
        d->letter = text[0];

        /* The point is worked out rather than read, and the stored one
         * has to match it. A row naming a different path was written by
         * somebody editing the file. */
        gs_str_copy(stored, sizeof stored, gs_db_text(stmt, 1));
        if (gs_mount_point_for(d->letter, d->point, sizeof d->point)
                != GS_OK ||
            strcmp(stored, d->point) != 0) {
            memset(d, 0, sizeof *d);
            continue;
        }

        gs_mount_plain(gs_db_text(stmt, 2), d->distro, sizeof d->distro);
        d->seen_at = gs_db_int(stmt, 3);
        d->misses = (int)gs_db_int(stmt, 4);
        count++;
    }
    gs_db_finalise(stmt);
    return count;
}

int gs_mount_forget(char letter)
{
    gs_db_stmt_t *stmt;
    char text[2];
    int rc;

    if (gs_mount_db == NULL)
        return GS_ERR_ARG;
    if (!gs_mount_letter_ok(letter))
        return GS_ERR_ARG;

    stmt = gs_db_prepare(gs_mount_db,
                         "DELETE FROM mount_drive WHERE letter = ?;");
    if (stmt == NULL)
        return GS_ERR;
    text[0] = letter;
    text[1] = '\0';
    gs_db_bind_text(stmt, 1, text);
    rc = gs_db_step(stmt) < 0 ? GS_ERR : GS_OK;
    gs_db_finalise(stmt);
    return rc;
}

/* Counts one run against a drive that was not found, and drops it once
 * the count reaches the limit. */
static void mark_missing(char letter)
{
    gs_db_stmt_t *stmt;
    char text[2];

    if (gs_mount_db == NULL || !gs_mount_letter_ok(letter))
        return;
    text[0] = letter;
    text[1] = '\0';

    stmt = gs_db_prepare(gs_mount_db,
        "UPDATE mount_drive SET misses = misses + 1 WHERE letter = ?;");
    if (stmt != NULL) {
        gs_db_bind_text(stmt, 1, text);
        (void)gs_db_step(stmt);
        gs_db_finalise(stmt);
    }

    stmt = gs_db_prepare(gs_mount_db,
        "DELETE FROM mount_drive WHERE letter = ? AND misses >= ?;");
    if (stmt == NULL)
        return;
    gs_db_bind_text(stmt, 1, text);
    gs_db_bind_int(stmt, 2, GS_MOUNT_MISS_LIMIT);
    (void)gs_db_step(stmt);
    gs_db_finalise(stmt);
}

int gs_mount_keep_reading(void)
{
    gs_mount_drive_t live[GS_MOUNT_MAX_DRIVES];
    gs_mount_drive_t saved[GS_MOUNT_MAX_DRIVES];
    char distro[72];
    int count;
    int kept = 0;
    int known;
    int i;
    int j;

    if (!gs_mount_under_wsl())
        return 0;

    count = gs_mount_read(live, GS_MOUNT_MAX_DRIVES);
    if (count < 0)
        return 0;
    if (gs_mount_distro(distro, sizeof distro) != GS_OK)
        distro[0] = '\0';

    for (i = 0; i < count; i++) {
        char point[16];

        /* Only a drive proven to answer is written down. A row sitting in
         * the mount table with a dead drive behind it would otherwise be
         * recorded on the first run, and every later start would try to
         * put back something that has never worked. */
        if (gs_mount_point_for(live[i].letter, point, sizeof point) != GS_OK)
            continue;
        if (strcmp(live[i].point, point) != 0)
            continue;
        if (gs_mount_point_refused(point))
            continue;
        if (!gs_mount_answers(point))
            continue;

        live[i].seen_at = gs_mount_now();
        gs_str_copy(live[i].distro, sizeof live[i].distro, distro);
        if (gs_mount_remember(&live[i]) == GS_OK)
            kept++;
    }

    /* Anything written down that the machine no longer shows counts a
     * miss, so a drive somebody unplugged for good stops costing an
     * attempt at every startup. */
    known = gs_mount_recall(saved, GS_MOUNT_MAX_DRIVES);
    for (i = 0; i < known; i++) {
        int seen = 0;

        for (j = 0; j < count; j++)
            if (live[j].letter == saved[i].letter &&
                gs_mount_answers(live[j].point)) {
                seen = 1;
                break;
            }
        if (!seen)
            mark_missing(saved[i].letter);
    }
    return kept;
}

