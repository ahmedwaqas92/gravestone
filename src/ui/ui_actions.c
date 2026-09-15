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
#include "picker.h"
#include "store.h"
#include "str.h"
#include "window.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* How long a reading of the machine stays good for, in seconds. */
#define MACHINE_AGE 5

const gs_detect_report_t *gs_ui_machine(void)
{
    static gs_detect_report_t held;
    static time_t taken;
    static int have;
    time_t now = time(NULL);

    /* Reading the machine starts the vendor tools, which cost about a
     * third of a second on this laptop, so a panel repainting on every
     * mouse move cannot do it for each row it draws. Free memory moves
     * slowly enough that a reading a few seconds old decides a fit the
     * same way a fresh one would.
     *
     * The stamp is written before the reading rather than after, so a
     * machine whose tools fail is asked again on a timer instead of on
     * every repaint. */
    if (!have || now - taken >= MACHINE_AGE) {
        gs_detect_report_t fresh;

        taken = now;
        if (gs_detect_read(&fresh) == GS_OK) {
            /* The most graphics memory ever seen free is kept rather than
             * the reading of this moment. A model that is loaded holds
             * the card while it sits there, and asking whether it fits
             * against a card it is already filling has it report itself
             * as too large. Models are run one at a time, so the card
             * comes back to this level between them.
             *
             * A desktop that grows its own use leaves this figure high,
             * which reads as more room than there is. What a screen holds
             * is small beside a model, so the error is small. */
            if (have && fresh.gpu_free_bytes < held.gpu_free_bytes)
                fresh.gpu_free_bytes = held.gpu_free_bytes;
            held = fresh;
            have = 1;
        }
    }
    return have ? &held : NULL;
}

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
    gs_ui_hit_t hit;

    /* Over the history, the wheel moves the history. Its bar strip counts
     * as the history too, since a hand resting on the bar means it. A
     * notch arrives as three rows either way, and three rows of the
     * history is three lines of a bubble. */
    if (state->screen == GS_UI_SCREEN_HOME && !gs_ui_fleet_visible(state) &&
        !gs_ui_record_showing(state)) {
        gs_ui_rect_t seen = gs_ui_chat_history_rect(w, h);
        gs_ui_rect_t bar = gs_ui_chat_bar_box(w, h);

        if (gs_ui_rect_contains(seen, x, y) || gs_ui_rect_contains(bar, x, y))
            return gs_ui_chat_scroll_by(state, w, h,
                                        by * GS_UI_BUBBLE_LINE);
    }

    hit = gs_ui_hit_test(state, w, h, x, y, &row);
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

void gs_ui_remove_wait(void)
{
    if (!removal.running)
        return;
    pthread_join(removal.thread, NULL);
    removal.running = 0;
}

int gs_ui_remove_running(void)
{
    return removal.running;
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
    gs_ui_settings_record_models(state);
    gs_log_info("ui: %s", state->detail);
    return 1;
}

/* The file box belongs to the system rather than to this program, and it
 * does not come back until the person answers. Running it here would stop
 * the window being painted for as long as the box is up, so it goes on a
 * thread of its own, exactly as the removal does. */
static struct {
    pthread_t  thread;
    atomic_int done;
    int        result;
    int        running;
    char       path[GS_UI_ATTACH_PATH];
} attach;

static void *attach_worker(void *unused)
{
    (void)unused;
    attach.result = gs_picker_open("Attach a file", attach.path,
                                   sizeof attach.path);
    atomic_store(&attach.done, 1);
    return NULL;
}

int gs_ui_attach_begin(gs_ui_state_t *state)
{
    if (state == NULL)
        return GS_ERR_ARG;
    if (attach.running)
        return GS_ERR;                  /* one box at a time */
    if (state->attach_count >= GS_UI_ATTACH_MAX)
        return GS_ERR;
    if (!gs_picker_available()) {
        gs_str_copy(state->status, sizeof state->status, "NO FILE BOX");
        gs_str_copy(state->detail, sizeof state->detail,
                    "this machine has no way to choose a file");
        return GS_ERR;
    }

    attach.path[0] = '\0';
    attach.result = GS_ERR;
    atomic_store(&attach.done, 0);
    if (pthread_create(&attach.thread, NULL, attach_worker, NULL) != 0)
        return GS_ERR;
    attach.running = 1;
    state->attach_open = 1;
    return GS_OK;
}

int gs_ui_attach_poll(gs_ui_state_t *state)
{
    if (state == NULL || !attach.running)
        return 0;
    if (!atomic_load(&attach.done))
        return 0;

    pthread_join(attach.thread, NULL);
    attach.running = 0;
    state->attach_open = 0;

    if (attach.result == GS_OK && attach.path[0] != '\0' &&
        state->attach_count < GS_UI_ATTACH_MAX) {
        gs_str_copy(state->attached[state->attach_count],
                    GS_UI_ATTACH_PATH, attach.path);
        state->attach_count++;
    }
    return 1;
}

void gs_ui_attach_wait(void)
{
    if (!attach.running)
        return;
    pthread_join(attach.thread, NULL);
    attach.running = 0;
}

int gs_ui_attach_running(void)
{
    return attach.running;
}

void gs_ui_attach_drop(gs_ui_state_t *state, int index)
{
    int i;

    if (state == NULL || index < 0 || index >= state->attach_count)
        return;
    for (i = index; i + 1 < state->attach_count; i++)
        gs_str_copy(state->attached[i], GS_UI_ATTACH_PATH,
                    state->attached[i + 1]);
    state->attach_count--;
    state->attached[state->attach_count][0] = '\0';
}
