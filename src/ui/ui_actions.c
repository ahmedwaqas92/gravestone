/* ui_actions.c
 *
 * What the controls do once hit testing has named them. Reading the
 * machine, mounting it, scrolling the lists, and taking a model off the
 * disk. The event loop in ui.c stays a switchboard.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "catalogue.h"
#include "detect.h"
#include "library.h"
#include "log.h"
#include "store.h"
#include "str.h"
#include "window.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* Reads the machine, writes it down, and works out what to say about it. */
void gs_ui_refresh_state(gs_ui_state_t *state)
{
    gs_detect_report_t report;
    char print[17] = {0};
    long long mounted_at = 0, last_seen = 0;

    state->mounted = gs_store_is_mounted();

    if (!state->mounted) {
        gs_str_copy(state->status, sizeof state->status,
                    "NO DEVICE MOUNTED");
        gs_str_copy(state->detail, sizeof state->detail,
                    "press Mount Device to read this machine");
        return;
    }

    if (gs_store_latest(&report, print, sizeof print, &mounted_at,
                        &last_seen) == GS_OK) {
        int count = gs_store_snapshot_count();
        snprintf(state->status, sizeof state->status, "DEVICE MOUNTED");
        char ram[32];

        gs_str_bytes(report.ram_total_bytes, ram, sizeof ram);
        snprintf(state->detail, sizeof state->detail,
                 "%s %s  %d cores  %s  %s%s",
                 report.os, report.arch, report.cpu_cores, ram, print,
                 count > 1 ? "  hardware changed" : "");
    }
}

void gs_ui_do_mount(gs_ui_state_t *state)
{
    gs_detect_report_t report;
    int changed = 0;

    if (gs_detect_read(&report) != GS_OK) {
        gs_str_copy(state->status, sizeof state->status, "READ FAILED");
        gs_str_copy(state->detail, sizeof state->detail,
                    "the machine would not answer");
        return;
    }
    if (gs_store_mount(&report, &changed) != GS_OK) {
        gs_str_copy(state->status, sizeof state->status, "STORE FAILED");
        gs_str_copy(state->detail, sizeof state->detail,
                    "the reading could not be written down");
        return;
    }
    state->changed = changed;
    gs_ui_refresh_state(state);
    gs_ui_panes_refresh(state, &report);
    if (changed)
        gs_log_info("ui: the hardware differs from the last mount");
}

/* Moves the list under the pointer by the given number of rows, and says
 * whether anything actually moved. A list shorter than its pane does not
 * scroll at all. */
static int scroll_rows(int *scroll, int count, int visible, int by)
{
    int limit = count - visible;
    int want;

    if (limit < 0)
        limit = 0;
    want = *scroll + by;
    if (want < 0)
        want = 0;
    if (want > limit)
        want = limit;
    if (want == *scroll)
        return 0;
    *scroll = want;
    return 1;
}

int gs_ui_scroll_under(gs_ui_state_t *state, gs_window_t *window, int x,
                        int y, int by)
{
    int w = gs_window_width(window);
    int h = gs_window_height(window);
    int row = -1;
    gs_ui_hit_t hit = gs_ui_hit_test(state, w, h, x, y, &row);

    switch (hit) {
    case GS_UI_HIT_LEFT_ROW:
        return scroll_rows(&state->model_scroll, state->model_count,
                           gs_ui_pane_rows(gs_ui_left_rect(w, h)), by);
    case GS_UI_HIT_RIGHT_ROW:
        return scroll_rows(&state->library_scroll, state->library_count,
                           gs_ui_pane_rows(gs_ui_right_rect(w, h)), by);
    default:
        return 0;
    }
}


/* The removal runs on its own thread so the spinner keeps turning while
 * ollama works through the layers. One removal at a time, and the main
 * thread only reads the library while the worker is out, which is safe
 * because removing mutates the disk rather than the table. */
static struct {
    pthread_t   thread;
    atomic_int  done;
    int         result;
    int         index;
    int         running;
    int         adding;        /* 1 pulls a model in, 0 takes one out */
    long long   free_before;
    long long   expected;      /* the row's size, the percent's bottom */
    char        name[160];
    char        root[128];
} removal;

static void *removal_worker(void *arg)
{
    (void)arg;
    if (removal.adding)
        removal.result = gs_library_pull(removal.name, removal.root);
    else
        removal.result = gs_library_remove(removal.index);
    atomic_store(&removal.done, 1);
    return NULL;
}

int gs_ui_remove_begin(gs_ui_state_t *state)
{
    const gs_library_entry_t *entry;
    const gs_detect_disk_t *disk;

    if (state == NULL || removal.running)
        return GS_ERR;
    entry = gs_library_at(state->confirm_row);
    if (entry == NULL)
        return GS_ERR_ARG;

    gs_str_copy(removal.name, sizeof removal.name, entry->name);
    removal.index = state->confirm_row;
    removal.adding = 0;
    removal.free_before = -1;
    disk = gs_ui_panes_disk(state->disk_sel);
    if (disk != NULL)
        removal.free_before = disk->free_bytes;
    atomic_store(&removal.done, 0);

    if (pthread_create(&removal.thread, NULL, removal_worker, NULL) != 0) {
        gs_log_warn("ui: no thread for the removal, running it here");
        removal.result = gs_library_remove(removal.index);
        atomic_store(&removal.done, 1);
        removal.running = 1;
        return GS_OK;
    }
    removal.running = 1;
    return GS_OK;
}

int gs_ui_add_begin(gs_ui_state_t *state)
{
    const gs_catalogue_entry_t *fit;
    const gs_detect_disk_t *disk;

    if (state == NULL || removal.running)
        return GS_ERR;
    fit = gs_ui_panes_model(state->confirm_row);
    if (fit == NULL)
        return GS_ERR_ARG;

    gs_str_copy(removal.name, sizeof removal.name, fit->file);
    removal.adding = 1;
    removal.expected = fit->bytes;
    removal.free_before = -1;
    removal.root[0] = '\0';
    disk = gs_ui_panes_disk(state->disk_sel);
    if (disk != NULL) {
        removal.free_before = disk->free_bytes;
        gs_str_copy(removal.root, sizeof removal.root, disk->mount);
    }
    atomic_store(&removal.done, 0);

    if (pthread_create(&removal.thread, NULL, removal_worker, NULL) != 0) {
        gs_log_warn("ui: no thread for the pull, running it here");
        removal.result = gs_library_pull(removal.name, removal.root);
        atomic_store(&removal.done, 1);
    }
    removal.running = 1;
    return GS_OK;
}

int gs_ui_progress_percent(long long partial, long long expected)
{
    long long percent;

    if (expected <= 0 || partial < 0)
        return -1;
    percent = partial * 100 / expected;
    if (percent < 0)
        return -1;
    if (percent > 99)
        percent = 99;
    return (int)percent;
}

int gs_ui_add_progress(void)
{
    long long completed = 0, total = 0;

    if (!removal.running || !removal.adding)
        return -1;
    /* The daemon's own byte counts, since the store sits on a mount that
     * reports every half written file as already full sized. */
    gs_library_pull_progress(&completed, &total);
    if (total > 0)
        return gs_ui_progress_percent(completed, total);
    return gs_ui_progress_percent(0, removal.expected);
}

int gs_ui_remove_poll(gs_ui_state_t *state)
{
    const gs_detect_disk_t *disk;

    if (state == NULL || !removal.running)
        return 0;
    if (!atomic_load(&removal.done))
        return 0;

    pthread_join(removal.thread, NULL);
    removal.running = 0;

    if (removal.result != GS_OK) {
        gs_str_copy(state->status, sizeof state->status,
                    removal.adding ? "ADD FAILED" : "REMOVE FAILED");
        snprintf(state->detail, sizeof state->detail,
                 removal.adding ? "%s did not arrive"
                                : "%s stayed on the disk", removal.name);
        return 1;
    }

    /* The free space on the chosen disk, measured before and again after,
     * is the truth about what came back. Tags share layers, so the
     * difference can undershoot the size the row showed. */
    gs_ui_panes_refresh(state, NULL);

    disk = gs_ui_panes_disk(state->disk_sel);
    if (removal.adding) {
        if (disk != NULL && removal.free_before >= 0 &&
            removal.free_before > disk->free_bytes) {
            char used[32];

            gs_str_bytes(removal.free_before - disk->free_bytes, used,
                         sizeof used);
            snprintf(state->detail, sizeof state->detail,
                     "%s pulled, %s of the disk used", removal.name, used);
        } else {
            snprintf(state->detail, sizeof state->detail,
                     "%s pulled, shared layers were already here",
                     removal.name);
        }
    } else if (disk != NULL && removal.free_before >= 0 &&
               disk->free_bytes > removal.free_before) {
        char back[32];

        gs_str_bytes(disk->free_bytes - removal.free_before, back,
                     sizeof back);
        snprintf(state->detail, sizeof state->detail,
                 "%s removed, %s came back", removal.name, back);
    } else {
        snprintf(state->detail, sizeof state->detail,
                 "%s removed, shared layers kept the space", removal.name);
    }
    gs_log_info("ui: %s", state->detail);
    return 1;
}
