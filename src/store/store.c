#include "store.h"
#include "gravestone.h"
#include "db.h"
#include "detect.h"
#include "hash.h"
#include "log.h"
#include "str.h"
#include "paths.h"
#include "store_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static gs_db_t *db;
static char install_id[17];
static int cleared_stale;
static int install_is_new;

static long long now_seconds(void)
{
    return (long long)time(NULL);
}

/* A value nobody can predict from the hardware, so replacing a card
 * leaves the identity untouched. */
static void make_install_id(char *out, size_t cap)
{
    unsigned long long seed;
    char material[128];
    FILE *rnd = fopen("/dev/urandom", "rb");
    unsigned char bytes[16];

    if (rnd != NULL && fread(bytes, 1, sizeof bytes, rnd) == sizeof bytes) {
        fclose(rnd);
        gs_hash_hex(gs_hash_bytes(bytes, sizeof bytes), out, cap);
        return;
    }
    if (rnd != NULL)
        fclose(rnd);

    /* Falling back to the clock and the process number keeps this working
     * on a system without /dev/urandom. */
    snprintf(material, sizeof material, "%lld-%d", now_seconds(),
             (int)getpid());
    seed = gs_hash_text(material);
    gs_hash_hex(seed, out, cap);
    gs_log_warn("store: no random source, the install id is time based");
}

static int load_or_create_install(void)
{
    gs_db_stmt_t *stmt;
    int found = 0;

    stmt = gs_db_prepare(db, "SELECT id FROM install LIMIT 1;");
    if (stmt == NULL)
        return GS_ERR;
    if (gs_db_step(stmt) == 1) {
        const char *value = gs_db_text(stmt, 0);
        if (value != NULL) {
            snprintf(install_id, sizeof install_id, "%s", value);
            found = 1;
        }
    }
    gs_db_finalise(stmt);
    if (found)
        return GS_OK;

    install_is_new = 1;
    make_install_id(install_id, sizeof install_id);

    stmt = gs_db_prepare(db,
        "INSERT INTO install (id, created_at) VALUES (?, ?);");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_text(stmt, 1, install_id);
    gs_db_bind_int(stmt, 2, now_seconds());
    if (gs_db_step(stmt) != 0) {
        gs_db_finalise(stmt);
        return GS_ERR;
    }
    gs_db_finalise(stmt);
    gs_log_info("store: this install is %s", install_id);
    return GS_OK;
}

gs_db_t *gs_store_handle(void)
{
    return db;
}

int gs_store_open(void)
{
    char path[512];

    if (db != NULL)
        return GS_OK;

    cleared_stale = 0;
    install_is_new = 0;

    if (gs_paths_db_file(path, sizeof path) != GS_OK)
        return GS_ERR;

    db = gs_db_open(path);
    if (db == NULL)
        return GS_ERR;

    if (gs_db_exec(db, gs_store_schema) != GS_OK) {
        gs_log_error("store: cannot lay out the tables: %s",
                     gs_db_error(db));
        gs_db_close(db);
        db = NULL;
        return GS_ERR;
    }

    if (gs_store_migrate(db, &cleared_stale) != GS_OK) {
        gs_log_error("store: could not bring the database up to date: %s",
                     gs_db_error(db));
        gs_db_close(db);
        db = NULL;
        return GS_ERR;
    }

    if (load_or_create_install() != GS_OK) {
        gs_db_close(db);
        db = NULL;
        return GS_ERR;
    }
    return GS_OK;
}

void gs_store_close(void)
{
    gs_db_close(db);
    db = NULL;
    install_id[0] = '\0';
}

int gs_store_install_id(char *out, size_t cap)
{
    if (out == NULL || cap < 17)
        return GS_ERR_ARG;
    if (db == NULL)
        return GS_ERR;
    snprintf(out, cap, "%s", install_id);
    return GS_OK;
}

int gs_store_mount(const gs_detect_report_t *report, int *changed)
{
    char print[17];
    char latest[17] = {0};
    gs_db_stmt_t *stmt;

    if (report == NULL || db == NULL)
        return GS_ERR_ARG;
    if (gs_detect_fingerprint(report, print, sizeof print) != GS_OK)
        return GS_ERR;

    stmt = gs_db_prepare(db,
        "SELECT fingerprint FROM snapshot WHERE install_id = ?"
        " ORDER BY taken_at DESC, id DESC LIMIT 1;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_text(stmt, 1, install_id);
    if (gs_db_step(stmt) == 1) {
        const char *value = gs_db_text(stmt, 0);
        if (value != NULL)
            snprintf(latest, sizeof latest, "%s", value);
    }
    gs_db_finalise(stmt);

    if (strcmp(latest, print) == 0) {
        /* Same machine as last time, so only the sighting is updated. */
        stmt = gs_db_prepare(db,
            "UPDATE snapshot SET last_seen_at = ?, disk_free = ?"
            " WHERE install_id = ? AND fingerprint = ?;");
        if (stmt == NULL)
            return GS_ERR;
        gs_db_bind_int(stmt, 1, now_seconds());
        gs_db_bind_int(stmt, 2, report->disk_free_bytes);
        gs_db_bind_text(stmt, 3, install_id);
        gs_db_bind_text(stmt, 4, print);
        if (gs_db_step(stmt) != 0) {
            gs_db_finalise(stmt);
            return GS_ERR;
        }
        gs_db_finalise(stmt);

        /* Free space moves without the hardware changing, so the existing
         * rows are refreshed rather than replaced. */
        stmt = gs_db_prepare(db,
            "UPDATE snapshot_disk SET free_bytes = ? WHERE snapshot_id ="
            " (SELECT id FROM snapshot WHERE install_id = ? AND"
            "  fingerprint = ? ORDER BY id DESC LIMIT 1) AND position = ?;");
        if (stmt != NULL) {
            int i;

            for (i = 0; i < report->disk_count; i++) {
                gs_db_bind_int(stmt, 1, report->disk[i].free_bytes);
                gs_db_bind_text(stmt, 2, install_id);
                gs_db_bind_text(stmt, 3, print);
                gs_db_bind_int(stmt, 4, i);
                gs_db_step(stmt);
                gs_db_reset(stmt);
            }
            gs_db_finalise(stmt);
        }

        if (changed != NULL)
            *changed = 0;
        return GS_OK;
    }

    stmt = gs_db_prepare(db,
        "INSERT INTO snapshot (install_id, fingerprint, taken_at,"
        " last_seen_at, os, kernel, arch, cpu_model, cpu_cores, ram_bytes,"
        " disk_total, disk_free, gpu, gpu_memory)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
    if (stmt == NULL)
        return GS_ERR;

    gs_db_bind_text(stmt, 1, install_id);
    gs_db_bind_text(stmt, 2, print);
    gs_db_bind_int(stmt, 3, now_seconds());
    gs_db_bind_int(stmt, 4, now_seconds());
    gs_db_bind_text(stmt, 5, report->os);
    gs_db_bind_text(stmt, 6, report->kernel);
    gs_db_bind_text(stmt, 7, report->arch);
    gs_db_bind_text(stmt, 8, report->cpu_model);
    gs_db_bind_int(stmt, 9, report->cpu_cores);
    gs_db_bind_int(stmt, 10, report->ram_total_bytes);
    gs_db_bind_int(stmt, 11, report->disk_total_bytes);
    gs_db_bind_int(stmt, 12, report->disk_free_bytes);
    gs_db_bind_text(stmt, 13, report->gpu);
    gs_db_bind_int(stmt, 14, report->gpu_memory_bytes);

    if (gs_db_step(stmt) != 0) {
        gs_db_finalise(stmt);
        return GS_ERR;
    }
    gs_db_finalise(stmt);

    /* Every drive gets its own row, so a question about one of them stays
     * answerable later. */
    {
        long long snapshot_id = gs_db_last_insert_id(db);
        int i;

        stmt = gs_db_prepare(db,
            "INSERT INTO snapshot_disk (snapshot_id, position, mount,"
            " device, fs, total_bytes, free_bytes) VALUES (?,?,?,?,?,?,?);");
        if (stmt == NULL)
            return GS_ERR;

        for (i = 0; i < report->disk_count; i++) {
            gs_db_bind_int(stmt, 1, snapshot_id);
            gs_db_bind_int(stmt, 2, i);
            gs_db_bind_text(stmt, 3, report->disk[i].mount);
            gs_db_bind_text(stmt, 4, report->disk[i].device);
            gs_db_bind_text(stmt, 5, report->disk[i].fs);
            gs_db_bind_int(stmt, 6, report->disk[i].total_bytes);
            gs_db_bind_int(stmt, 7, report->disk[i].free_bytes);
            if (gs_db_step(stmt) != 0) {
                gs_db_finalise(stmt);
                return GS_ERR;
            }
            gs_db_reset(stmt);
        }
        gs_db_finalise(stmt);
    }

    if (changed != NULL)
        *changed = latest[0] != '\0';
    gs_log_info("store: recorded a %s configuration, fingerprint %s",
                latest[0] != '\0' ? "changed" : "first", print);
    return GS_OK;
}

int gs_store_snapshot_count(void)
{
    gs_db_stmt_t *stmt;
    int count = 0;

    if (db == NULL)
        return 0;
    stmt = gs_db_prepare(db,
        "SELECT COUNT(*) FROM snapshot WHERE install_id = ?;");
    if (stmt == NULL)
        return 0;
    gs_db_bind_text(stmt, 1, install_id);
    if (gs_db_step(stmt) == 1)
        count = (int)gs_db_int(stmt, 0);
    gs_db_finalise(stmt);
    return count;
}

int gs_store_cleared_stale(void)
{
    return cleared_stale;
}

int gs_store_install_is_new(void)
{
    return install_is_new;
}

int gs_store_is_mounted(void)
{
    return gs_store_snapshot_count() > 0;
}

int gs_store_latest(gs_detect_report_t *report, char *fingerprint,
                    size_t print_cap, long long *mounted_at,
                    long long *last_seen)
{
    gs_db_stmt_t *stmt;
    int found = 0;
    long long snapshot_id = 0;

    if (db == NULL)
        return GS_ERR;

    stmt = gs_db_prepare(db,
        "SELECT fingerprint, taken_at, last_seen_at, os, kernel, arch,"
        " cpu_model, cpu_cores, ram_bytes, disk_total, disk_free, gpu,"
        " gpu_memory, id"
        " FROM snapshot WHERE install_id = ?"
        " ORDER BY taken_at DESC, id DESC LIMIT 1;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_text(stmt, 1, install_id);

    if (gs_db_step(stmt) == 1) {
        const char *value;

        if (fingerprint != NULL && print_cap > 0) {
            value = gs_db_text(stmt, 0);
            snprintf(fingerprint, print_cap, "%s",
                     value != NULL ? value : "");
        }
        if (mounted_at != NULL)
            *mounted_at = gs_db_int(stmt, 1);
        if (last_seen != NULL)
            *last_seen = gs_db_int(stmt, 2);

        if (report != NULL) {
            memset(report, 0, sizeof *report);
            value = gs_db_text(stmt, 3);
            gs_str_copy(report->os, sizeof report->os, value);
            value = gs_db_text(stmt, 4);
            gs_str_copy(report->kernel, sizeof report->kernel, value);
            value = gs_db_text(stmt, 5);
            gs_str_copy(report->arch, sizeof report->arch, value);
            value = gs_db_text(stmt, 6);
            gs_str_copy(report->cpu_model, sizeof report->cpu_model, value);
            report->cpu_cores       = (int)gs_db_int(stmt, 7);
            report->ram_total_bytes = gs_db_int(stmt, 8);
            report->disk_total_bytes = gs_db_int(stmt, 9);
            report->disk_free_bytes  = gs_db_int(stmt, 10);
            value = gs_db_text(stmt, 11);
            gs_str_copy(report->gpu, sizeof report->gpu, value);
            report->gpu_memory_bytes = gs_db_int(stmt, 12);
            snapshot_id = gs_db_int(stmt, 13);
        }
        found = 1;
    }
    gs_db_finalise(stmt);

    if (found && report != NULL && snapshot_id > 0) {
        stmt = gs_db_prepare(db,
            "SELECT mount, device, fs, total_bytes, free_bytes"
            " FROM snapshot_disk WHERE snapshot_id = ?"
            " ORDER BY position;");
        if (stmt != NULL) {
            gs_db_bind_int(stmt, 1, snapshot_id);
            while (gs_db_step(stmt) == 1 &&
                   report->disk_count < GS_DETECT_MAX_DISKS) {
                gs_detect_disk_t *d = &report->disk[report->disk_count];

                gs_str_copy(d->mount, sizeof d->mount, gs_db_text(stmt, 0));
                gs_str_copy(d->device, sizeof d->device, gs_db_text(stmt, 1));
                gs_str_copy(d->fs, sizeof d->fs, gs_db_text(stmt, 2));
                d->total_bytes = gs_db_int(stmt, 3);
                d->free_bytes = gs_db_int(stmt, 4);
                report->disk_count++;
            }
            gs_db_finalise(stmt);
        }
    }
    return found ? GS_OK : GS_ERR;
}

static int store_init(void)
{
    return GS_OK;
}

static int store_run(int argc, char **argv)
{
    gs_detect_report_t report;
    char print[17];
    char id[17];
    long long mounted_at = 0, last_seen = 0;

    (void)argc;
    (void)argv;

    if (gs_store_open() != GS_OK)
        return GS_ERR;

    gs_store_install_id(id, sizeof id);
    printf("install\t%s\n", id);
    printf("mounted\t%s\n", gs_store_is_mounted() ? "yes" : "no");
    printf("snapshots\t%d\n", gs_store_snapshot_count());

    if (gs_store_latest(&report, print, sizeof print, &mounted_at,
                        &last_seen) == GS_OK) {
        char text[2048];
        gs_detect_describe(&report, text, sizeof text);
        printf("fingerprint\t%s\n", print);
        printf("mounted_at\t%lld\n", mounted_at);
        printf("last_seen\t%lld\n", last_seen);
        printf("%s", text);
    }

    gs_store_close();
    return GS_OK;
}

static void store_shutdown(void)
{
    gs_store_close();
}

const gs_module gs_store_module = {
    "store",
    "show what this install has recorded about the machine",
    store_init,
    store_run,
    store_shutdown
};
