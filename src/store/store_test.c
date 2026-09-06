/* store_test.c
 *
 * Points the data directory at a scratch path, so nothing here touches the
 * real database.
 */
#include "store.h"
#include "gravestone.h"
#include "detect.h"
#include "log.h"
#include "db.h"
#include "paths.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
static const char *SCRATCH = "/tmp/gravestone-store-test";

static void check(int c, const char *what)
{
    checks++;
    if (c) printf("  ok    %s\n", what);
    else { printf("  FAIL  %s\n", what); failures++; }
}

static void scrub(void)
{
    system("rm -rf /tmp/gravestone-store-test");
}

static gs_detect_report_t sample(void)
{
    gs_detect_report_t r;

    memset(&r, 0, sizeof r);
    gs_str_copy(r.os, sizeof r.os, "Linux");
    gs_str_copy(r.kernel, sizeof r.kernel, "6.18.0-test");
    gs_str_copy(r.arch, sizeof r.arch, "x86_64");
    gs_str_copy(r.cpu_model, sizeof r.cpu_model, "A Test Processor");
    gs_str_copy(r.gpu, sizeof r.gpu, "unknown");
    r.cpu_cores = 8;
    r.ram_total_bytes = 17179869184LL;   /* sixteen gigabytes */
    r.gpu_memory_bytes = 4294967296LL;   /* four gigabytes on the card */
    gs_str_copy(r.gpu, sizeof r.gpu, "A Test Graphics Card");

    r.disk_count = 3;
    gs_str_copy(r.disk[0].mount, sizeof r.disk[0].mount, "/");
    gs_str_copy(r.disk[0].device, sizeof r.disk[0].device, "/dev/sda1");
    gs_str_copy(r.disk[0].fs, sizeof r.disk[0].fs, "ext4");
    r.disk[0].total_bytes = 512110190592LL;
    r.disk[0].free_bytes = 100000000000LL;

    gs_str_copy(r.disk[1].mount, sizeof r.disk[1].mount, "/mnt/c");
    gs_str_copy(r.disk[1].device, sizeof r.disk[1].device, "C:\\");
    gs_str_copy(r.disk[1].fs, sizeof r.disk[1].fs, "9p");
    r.disk[1].total_bytes = 1000203816960LL;
    r.disk[1].free_bytes = 500000000000LL;

    gs_str_copy(r.disk[2].mount, sizeof r.disk[2].mount, "/mnt/backup");
    gs_str_copy(r.disk[2].device, sizeof r.disk[2].device, "/dev/sdb1");
    gs_str_copy(r.disk[2].fs, sizeof r.disk[2].fs, "xfs");
    r.disk[2].total_bytes = 2000000000000LL;
    r.disk[2].free_bytes = 1500000000000LL;

    r.disk_total_bytes = r.disk[0].total_bytes + r.disk[1].total_bytes +
                         r.disk[2].total_bytes;
    r.disk_free_bytes = r.disk[0].free_bytes + r.disk[1].free_bytes +
                        r.disk[2].free_bytes;
    return r;
}

static void test_before_open(void)
{
    char id[17];

    printf("before the database opens\n");
    check(gs_store_install_id(id, sizeof id) == GS_ERR,
          "no install identifier is available");
    check(gs_store_is_mounted() == 0, "nothing reads as mounted");
    check(gs_store_snapshot_count() == 0, "no snapshots are counted");
    check(gs_store_latest(NULL, NULL, 0, NULL, NULL) == GS_ERR,
          "there is nothing to fetch");
    check(gs_store_mount(NULL, NULL) == GS_ERR_ARG, "mounting nothing fails");
}

static void test_identity(void)
{
    char first[17], again[17], second[17];

    printf("identity\n");
    scrub();
    gs_paths_override(SCRATCH);

    check(gs_store_open() == GS_OK, "database opened");
    check(gs_store_install_is_new() == 1,
          "a first open reports the install as new");
    check(gs_store_install_id(first, sizeof first) == GS_OK,
          "an install identifier was made");
    printf("        %s\n", first);
    check(strlen(first) == 16, "sixteen characters long");
    check(strspn(first, "0123456789abcdef") == 16, "all hexadecimal");
    check(gs_store_is_mounted() == 0, "a fresh install has nothing mounted");

    gs_store_close();
    check(gs_store_open() == GS_OK, "database reopened");
    check(gs_store_install_is_new() == 0,
          "a later open reports it as already there");
    gs_store_install_id(again, sizeof again);
    check(strcmp(first, again) == 0,
          "the identifier survives closing and reopening");
    gs_store_close();

    /* A second install directory has to produce a different identifier. */
    system("rm -rf /tmp/gravestone-store-test-two");
    gs_paths_override("/tmp/gravestone-store-test-two");
    gs_store_open();
    gs_store_install_id(second, sizeof second);
    gs_store_close();
    system("rm -rf /tmp/gravestone-store-test-two");
    check(strcmp(first, second) != 0,
          "a separate install gets a separate identifier");

    gs_paths_override(SCRATCH);
}

static void test_mounting(void)
{
    gs_detect_report_t r = sample();
    gs_detect_report_t back;
    char print[17] = {0};
    char id_before[17], id_after[17];
    long long mounted_at = 0, last_seen = 0;
    int changed = -1;

    printf("mounting a device\n");
    gs_store_open();
    gs_store_install_id(id_before, sizeof id_before);

    check(gs_store_mount(&r, &changed) == GS_OK, "the reading was stored");
    check(changed == 0, "the first mount is not reported as a change");
    check(gs_store_is_mounted() == 1, "the device now reads as mounted");
    check(gs_store_snapshot_count() == 1, "one configuration is recorded");

    check(gs_store_latest(&back, print, sizeof print, &mounted_at,
                          &last_seen) == GS_OK, "it can be read back");
    check(strcmp(back.os, "Linux") == 0, "the operating system came back");
    check(strcmp(back.cpu_model, "A Test Processor") == 0,
          "the processor name came back");
    check(back.cpu_cores == 8, "the core count came back");
    check(back.ram_total_bytes == 17179869184LL,
          "sixteen gigabytes came back intact");
    check(mounted_at > 0 && last_seen > 0, "both timestamps are set");
    check(strlen(print) == 16, "the fingerprint came back");

    printf("the disks and the graphics card\n");
    check(strcmp(back.gpu, "A Test Graphics Card") == 0,
          "the graphics card name came back");
    check(back.gpu_memory_bytes == 4294967296LL,
          "four gigabytes of graphics memory came back");
    check(back.disk_count == 3, "all three disks came back");
    check(strcmp(back.disk[0].mount, "/") == 0,
          "the first disk kept its mount point");
    check(strcmp(back.disk[1].device, "C:\\") == 0,
          "a backslash in a device name survived the round trip");
    check(strcmp(back.disk[2].fs, "xfs") == 0,
          "the third disk kept its filesystem");
    check(back.disk[2].total_bytes == 2000000000000LL,
          "two terabytes came back intact");
    check(back.disk[0].total_bytes == 512110190592LL &&
          back.disk[1].total_bytes == 1000203816960LL,
          "the disks came back in the order they went in");

    printf("mounting the same device again\n");
    changed = -1;
    check(gs_store_mount(&r, &changed) == GS_OK, "the second mount worked");
    check(changed == 0, "it is not reported as a change");
    check(gs_store_snapshot_count() == 1,
          "no second row was written for the same hardware");

    printf("free space alone is not a hardware change\n");
    r.disk_free_bytes = 42;
    r.disk[0].free_bytes = 7;
    r.disk[1].free_bytes = 11;
    changed = -1;
    gs_store_mount(&r, &changed);
    check(changed == 0, "using up disk does not count as new hardware");
    check(gs_store_snapshot_count() == 1, "still one configuration");
    memset(&back, 0, sizeof back);
    gs_store_latest(&back, NULL, 0, NULL, NULL);
    check(back.disk_free_bytes == 42,
          "the new free space was recorded on the existing row");
    check(back.disk[0].free_bytes == 7 && back.disk[1].free_bytes == 11,
          "each disk row was refreshed rather than replaced");
    check(back.disk_count == 3, "no duplicate disk rows were written");

    printf("attaching a drive counts as new hardware\n");
    r.disk_count = 2;
    r.disk_total_bytes = r.disk[0].total_bytes + r.disk[1].total_bytes;
    changed = -1;
    check(gs_store_mount(&r, &changed) == GS_OK, "the shorter list stored");
    check(changed == 1, "removing a drive is reported as a change");
    check(gs_store_snapshot_count() == 2, "a second configuration exists");
    memset(&back, 0, sizeof back);
    gs_store_latest(&back, NULL, 0, NULL, NULL);
    check(back.disk_count == 2, "the newest configuration has two disks");

    r.disk_count = 3;
    r.disk_total_bytes = r.disk[0].total_bytes + r.disk[1].total_bytes +
                         r.disk[2].total_bytes;
    gs_store_mount(&r, NULL);
    check(gs_store_snapshot_count() == 3, "putting it back is a third");

    printf("swapping the hardware\n");
    r.ram_total_bytes = 34359738368LL;   /* thirty two gigabytes */
    changed = -1;
    check(gs_store_mount(&r, &changed) == GS_OK, "the new reading stored");
    check(changed == 1, "this one is reported as a change");
    check(gs_store_snapshot_count() == 4, "a fourth configuration exists");

    gs_store_install_id(id_after, sizeof id_after);
    check(strcmp(id_before, id_after) == 0,
          "the install identifier survived the hardware change");

    gs_store_latest(&back, NULL, 0, NULL, NULL);
    check(back.ram_total_bytes == 34359738368LL,
          "the newest configuration is the one that comes back");

    printf("what survives a restart\n");
    gs_store_close();
    gs_store_open();
    check(gs_store_snapshot_count() == 4, "every configuration is still there");
    check(gs_store_is_mounted() == 1, "it still reads as mounted");
    memset(&back, 0, sizeof back);
    gs_store_latest(&back, NULL, 0, NULL, NULL);
    check(back.ram_total_bytes == 34359738368LL,
          "the newest one is still newest");
    check(back.disk_count == 3, "its disks came back with it");
    check(back.gpu_memory_bytes == 4294967296LL,
          "so did the graphics memory");
    gs_store_close();
}

/* An old database has snapshots with no drive rows in them, written
 * before drives were read at all. Opening it has to clear those rather
 * than compare them against a new reading. */
static void test_migration_clears_old_readings(void)
{
    char id_before[17], id_after[17];

    printf("an older database\n");
    scrub();
    gs_paths_override(SCRATCH);

    /* Build one the old way: a snapshot with no drive rows. */
    check(gs_store_open() == GS_OK, "a database was made");
    gs_store_install_id(id_before, sizeof id_before);
    {
        gs_detect_report_t r = sample();

        r.disk_count = 0;
        r.disk_total_bytes = 0;
        r.disk_free_bytes = 0;
        check(gs_store_mount(&r, NULL) == GS_OK,
              "a reading with no drives was stored");
        check(gs_store_snapshot_count() == 1, "it is there");
    }
    gs_store_close();

    /* A database made today is stamped with the current version, so the
     * stamp is wound back to make it look like one written before drives
     * were recorded. */
    {
        char path[512];
        gs_db_t *raw;

        check(gs_paths_db_file(path, sizeof path) == GS_OK,
              "the file was located");
        raw = gs_db_open(path);
        check(raw != NULL, "it opened directly");
        if (raw != NULL) {
            check(gs_db_exec(raw, "PRAGMA user_version = 2;") == GS_OK,
                  "the version was wound back to two");
            gs_db_close(raw);
        }
    }

    /* Reopening now runs the migration, which should take it away. */
    check(gs_store_open() == GS_OK, "reopened");
    check(gs_store_snapshot_count() == 0,
          "the reading with no drives was cleared");
    check(gs_store_is_mounted() == 0, "so the device reads as unmounted");
    check(gs_store_cleared_stale() == 1, "and the clearing was reported");
    /* An install that was already there yet holds nothing is the signal
     * the interface uses to take a fresh reading by itself. */
    check(gs_store_install_is_new() == 0,
          "the install was already there, so this is not a first run");
    gs_store_install_id(id_after, sizeof id_after);
    check(strcmp(id_before, id_after) == 0,
          "the install identifier survived the clearing");

    {
        gs_detect_report_t r = sample();
        int changed = -1;

        check(gs_store_mount(&r, &changed) == GS_OK, "a full reading stored");
        check(changed == 0,
              "and it counts as a first mount rather than a change");
        check(gs_store_snapshot_count() == 1, "one configuration again");
    }
    gs_store_close();
    scrub();
}

static void test_reopen_is_idempotent(void)
{
    printf("opening twice\n");
    check(gs_store_open() == GS_OK, "first open");
    check(gs_store_open() == GS_OK, "second open is harmless");
    gs_store_close();
    gs_store_close();
    check(1, "closing twice does not crash");
}

int main(void)
{
    gs_log_set_level(GS_LOG_ERROR);

    test_before_open();
    test_identity();
    test_mounting();
    test_migration_clears_old_readings();
    test_reopen_is_idempotent();

    scrub();
    gs_paths_override(NULL);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
