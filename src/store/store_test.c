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

/* Settings and the model inventory, attacked at every edge that a
 * caller could reach: keys never written, empty values, overwriting,
 * a full table, and every null the API accepts. */
static void test_settings(void)
{
    char out[64];
    int i;

    printf("settings kept between sessions\n");

    /* A file of its own, so the counts start where this test expects. */
    scrub();
    if (gs_store_open() != GS_OK) {
        check(0, "the store opened");
        return;
    }

    check(gs_store_setting_count() == 0, "a fresh file carries no settings");
    check(gs_store_setting_get("nothing", out, sizeof out) == GS_ERR,
          "a key never written reports an error");
    check(out[0] == '\0', "and leaves the buffer empty");

    check(gs_store_setting_put("disk.mount", "/mnt/d") == GS_OK,
          "a setting was written");
    check(gs_store_setting_count() == 1, "and counted");
    check(gs_store_setting_get("disk.mount", out, sizeof out) == GS_OK &&
          strcmp(out, "/mnt/d") == 0, "and reads back the same text");

    check(gs_store_setting_put("disk.mount", "/mnt/e") == GS_OK,
          "the same key takes a new value");
    check(gs_store_setting_count() == 1, "without growing the table");
    check(gs_store_setting_get("disk.mount", out, sizeof out) == GS_OK &&
          strcmp(out, "/mnt/e") == 0, "and the new value wins");

    check(gs_store_setting_put("empty", "") == GS_OK,
          "an empty value is allowed, since a cleared choice is a choice");
    check(gs_store_setting_get("empty", out, sizeof out) == GS_OK &&
          out[0] == '\0', "and reads back empty");

    check(gs_store_setting_put(NULL, "x") == GS_ERR_ARG, "a null key is refused");
    check(gs_store_setting_put("", "x") == GS_ERR_ARG, "an empty key is refused");
    check(gs_store_setting_put("k", NULL) == GS_ERR_ARG, "a null value is refused");
    check(gs_store_setting_get(NULL, out, sizeof out) == GS_ERR_ARG,
          "reading a null key is refused");
    check(gs_store_setting_get("disk.mount", NULL, 10) == GS_ERR_ARG,
          "reading into nothing is refused");
    check(gs_store_setting_get("disk.mount", out, 0) == GS_ERR_ARG,
          "reading into no room is refused");

    /* A value longer than the buffer comes back cut rather than over. */
    {
        char big[300];
        char small[16];

        memset(big, 'v', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        check(gs_store_setting_put("long", big) == GS_OK,
              "a three hundred byte value is written");
        check(gs_store_setting_get("long", small, sizeof small) == GS_OK,
              "and reads back into a small buffer");
        check(strlen(small) == sizeof small - 1,
              "cut to what the buffer holds");
    }

    printf("the model inventory\n");

    check(gs_store_model_load() == 0, "a fresh file remembers no models");
    check(gs_store_model_at(0) == NULL, "with no row to read");

    check(gs_store_model_remember("a:1b", "/mnt/d/blobs/x", "/mnt/d", 100)
          == GS_OK, "a model was remembered");
    check(gs_store_model_remember("b:2b", "/mnt/d/blobs/y", "/mnt/d", 300)
          == GS_OK, "and a second");
    check(gs_store_model_remember("a:1b", "/mnt/e/blobs/z", "/mnt/e", 900)
          == GS_OK, "the same name takes a new path and size");

    check(gs_store_model_load() == 2, "two models come back, the repeat merged");
    check(gs_store_model_at(0) != NULL &&
          gs_store_model_at(0)->bytes == 900,
          "the largest is first and carries its new size");
    check(gs_store_model_at(0) != NULL &&
          strcmp(gs_store_model_at(0)->root, "/mnt/e") == 0,
          "and its new disk");
    check(gs_store_model_at(-1) == NULL, "a negative index gives nothing");
    check(gs_store_model_at(2) == NULL, "one past the end gives nothing");

    check(gs_store_model_remember(NULL, "p", "r", 1) == GS_ERR_ARG,
          "a null name is refused");
    check(gs_store_model_remember("", "p", "r", 1) == GS_ERR_ARG,
          "an empty name is refused");
    check(gs_store_model_remember("n", NULL, "r", 1) == GS_ERR_ARG,
          "a null path is refused");
    check(gs_store_model_remember("n", "p", NULL, 1) == GS_ERR_ARG,
          "a null disk is refused");
    check(gs_store_model_remember("n", "p", "r", -5) == GS_ERR_ARG,
          "a negative size is refused");
    check(gs_store_model_load() == 2, "and none of those reached the table");

    /* Filling past what the reader holds stops at its limit. */
    for (i = 0; i < 600; i++) {
        char name[32];

        snprintf(name, sizeof name, "bulk-%d:1b", i);
        gs_store_model_remember(name, "/p", "/r", 10 + i);
    }
    check(gs_store_model_load() == 512,
          "a table larger than the reader fills it and stops");

    check(gs_store_model_forget_all() == GS_OK, "the inventory was emptied");
    check(gs_store_model_count() == 0, "and the reader emptied with it");
    check(gs_store_model_load() == 0, "and the table is truly empty");

    /* Everything written survives a close and a reopen, which is the
     * whole point of putting it in a file. */
    gs_store_setting_put("survives", "yes");
    gs_store_model_remember("kept:1b", "/p", "/mnt/d", 42);
    gs_store_close();
    check(gs_store_open() == GS_OK, "the file reopened");
    check(gs_store_setting_get("survives", out, sizeof out) == GS_OK &&
          strcmp(out, "yes") == 0, "the setting outlived the session");
    check(gs_store_model_load() == 1 && gs_store_model_at(0)->bytes == 42,
          "and so did the inventory");

    check(gs_store_setting_count() > 0, "settings exist before closing");
    gs_store_close();
    check(gs_store_setting_count() == 0,
          "and reading with the file shut reports none rather than crashing");
    check(gs_store_setting_put("k", "v") == GS_ERR_ARG,
          "writing with the file shut is refused");
    check(gs_store_model_remember("n", "p", "r", 1) == GS_ERR_ARG,
          "remembering with the file shut is refused");
    check(gs_store_model_load() == 0, "loading with the file shut reads none");
}

int main(void)
{
    gs_log_set_level(GS_LOG_ERROR);

    test_before_open();
    test_identity();
    test_mounting();
    test_migration_clears_old_readings();
    test_reopen_is_idempotent();
    test_settings();

    scrub();
    gs_paths_override(NULL);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
