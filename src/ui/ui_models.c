/* ui_models.c
 *
 * What the three panes under the bar show, and how it is drawn.
 *
 * The left pane holds models from the Ollama library snapshot that could run
 * on this machine. The middle pane holds the disks the system has mounted.
 * The right pane holds the models already sitting on the disk that is
 * chosen. The bar across the top is the same list as the left pane, folded
 * down to one line so a model can be picked without scrolling a pane.
 *
 * The lists live here as file statics. A repaint runs over the same rows
 * many times a second, and copying names into the state on every frame
 * would buy nothing.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "catalogue.h"
#include "detect.h"
#include "library.h"
#include "log.h"
#include "str.h"
#include "window.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Room for every row the catalogue can hold, so the pane never quietly
 * drops models on a machine with enough memory to run them all. */
#define UI_MAX_MODELS GS_CATALOGUE_MAX
#define PANE_PAD      10

/* Indices into the catalogue, largest model first. Runnable holds every
 * model this machine could load. Filtered holds the ones in the category
 * the chooser is set to, in the same order. */
static int  runnable[UI_MAX_MODELS];
static int  runnable_count;
static int  filtered[UI_MAX_MODELS];
static int  filtered_count;

/* One slot per category, plus slot 0 for all of them together. */
#define UI_CATEGORIES ((int)GS_KIND_COUNT + 1)
static int  category_total[UI_CATEGORIES];

int gs_ui_panes_category_count(void) { return UI_CATEGORIES; }

const char *gs_ui_panes_category_name(int index)
{
    if (index <= 0)
        return "All models";
    if (index >= UI_CATEGORIES)
        return "unknown";
    return gs_catalogue_kind_name((gs_catalogue_kind_t)(index - 1));
}

int gs_ui_panes_category_total(int index)
{
    if (index < 0 || index >= UI_CATEGORIES)
        return 0;
    return category_total[index];
}

/* Case blind substring, since a search for QWEN should find qwen3.5. */
static int name_contains(const char *name, const char *wanted)
{
    size_t n = strlen(wanted);
    size_t i;

    if (n == 0)
        return 1;
    for (i = 0; name[i] != '\0'; i++) {
        size_t k = 0;

        while (k < n && name[i + k] != '\0' &&
               tolower((unsigned char)name[i + k]) ==
               tolower((unsigned char)wanted[k]))
            k++;
        if (k == n)
            return 1;
    }
    return 0;
}

/* Rebuilds the filtered list from the category the state is set to and
 * whatever sits in the search field. The kind is worked out again on
 * every change rather than stored, since two thousand short string
 * searches cost nothing next to one keystroke. */
static void apply_category(gs_ui_state_t *state)
{
    int i;

    if (state->category_sel < 0 || state->category_sel >= UI_CATEGORIES)
        state->category_sel = 0;

    filtered_count = 0;
    for (i = 0; i < runnable_count; i++) {
        const gs_catalogue_entry_t *entry = gs_catalogue_at(runnable[i]);

        if (entry == NULL)
            continue;
        if (state->category_sel != 0 &&
            (int)gs_catalogue_kind(entry) != state->category_sel - 1)
            continue;
        if (!name_contains(entry->file, state->search))
            continue;
        filtered[filtered_count++] = runnable[i];
    }

    state->category_count = UI_CATEGORIES;
    state->model_count = filtered_count;
    state->model_scroll = 0;
    state->model_sel = -1;
}

/* True when the two names share the model half, the text before the
 * colon, so an alias like latest can still meet its explicit twin. */
static int same_model(const char *a, const char *b)
{
    const char *ca = strchr(a, ':');
    const char *cb = strchr(b, ':');
    size_t la = ca != NULL ? (size_t)(ca - a) : strlen(a);
    size_t lb = cb != NULL ? (size_t)(cb - b) : strlen(b);

    return la == lb && strncmp(a, b, la) == 0;
}

int gs_ui_panes_locate(gs_ui_state_t *state, const gs_library_entry_t *owned)
{
    int found = -1;
    int i;

    if (state == NULL || owned == NULL || owned->name[0] == '\0')
        return GS_ERR_ARG;

    /* Only rows this machine can run are on offer, so only those are
     * searched. An exact name wins outright, and an alias settles for a
     * row of the same model whose size agrees byte for byte. */
    for (i = 0; i < runnable_count && found < 0; i++) {
        const gs_catalogue_entry_t *entry = gs_catalogue_at(runnable[i]);

        if (entry != NULL && strcmp(entry->file, owned->name) == 0)
            found = runnable[i];
    }
    for (i = 0; i < runnable_count && found < 0; i++) {
        const gs_catalogue_entry_t *entry = gs_catalogue_at(runnable[i]);

        if (entry != NULL && entry->bytes == owned->bytes &&
            same_model(entry->file, owned->name))
            found = runnable[i];
    }
    if (found < 0)
        return GS_ERR;

    state->category_sel = (int)gs_catalogue_kind(gs_catalogue_at(found)) + 1;
    apply_category(state);

    for (i = 0; i < filtered_count; i++) {
        if (filtered[i] == found) {
            state->model_sel = i;
            state->model_scroll = i > 3 ? i - 3 : 0;
            break;
        }
    }

    gs_log_info("ui: %s on disk is %s in the list", owned->name,
                gs_catalogue_at(found)->file);
    return GS_OK;
}

void gs_ui_panes_pick_category(gs_ui_state_t *state, int index)
{
    if (state == NULL)
        return;
    state->category_sel = index;
    apply_category(state);
    gs_log_debug("ui: category %s holds %d models that fit",
                gs_ui_panes_category_name(state->category_sel),
                state->model_count);
}

static gs_detect_disk_t disks[GS_DETECT_MAX_DISKS];
static int  disk_count;

/* How many models the left pane is showing right now. */
int gs_ui_panes_model_count(void) { return filtered_count; }
int gs_ui_panes_disk_count(void)  { return disk_count; }

const gs_catalogue_entry_t *gs_ui_panes_model(int index)
{
    if (index < 0 || index >= filtered_count)
        return NULL;
    return gs_catalogue_at(filtered[index]);
}

const gs_detect_disk_t *gs_ui_panes_disk(int index)
{
    if (index < 0 || index >= disk_count)
        return NULL;
    return &disks[index];
}

void gs_ui_panes_pick_disk(gs_ui_state_t *state, int index)
{
    const gs_detect_disk_t *disk = gs_ui_panes_disk(index);

    if (state == NULL)
        return;
    if (disk == NULL) {
        state->disk_sel = -1;
        state->library_count = 0;
        state->library_scroll = 0;
        gs_library_release();
        return;
    }

    state->disk_sel = index;
    state->library_count = gs_library_scan(disk->mount);
    state->library_scroll = 0;
    gs_log_info("ui: %s holds %d model files", disk->mount,
                state->library_count);
}

void gs_ui_panes_refresh(gs_ui_state_t *state,
                         const gs_detect_report_t *machine)
{
    gs_detect_report_t own;
    const gs_detect_report_t *report = machine;
    int best = -1;
    int i;

    if (state == NULL)
        return;

    if (gs_catalogue_count() == 0)
        gs_catalogue_load();

    /* Reading the machine can take seconds, since asking the graphics card
     * its name runs another program. A caller that has just read it passes
     * what it read rather than paying for it twice. */
    if (report == NULL) {
        if (gs_detect_read(&own) != GS_OK) {
            gs_log_warn("ui: the machine would not answer, panes left empty");
            runnable_count = 0;
            filtered_count = 0;
            disk_count = 0;
            for (i = 0; i < UI_CATEGORIES; i++)
                category_total[i] = 0;
            state->model_count = 0;
            state->disk_count = 0;
            state->category_count = UI_CATEGORIES;
            return;
        }
        report = &own;
    }

    runnable_count = gs_catalogue_runnable(report, runnable, UI_MAX_MODELS);

    for (i = 0; i < UI_CATEGORIES; i++)
        category_total[i] = 0;
    category_total[0] = runnable_count;
    for (i = 0; i < runnable_count; i++) {
        const gs_catalogue_entry_t *entry = gs_catalogue_at(runnable[i]);

        if (entry != NULL)
            category_total[(int)gs_catalogue_kind(entry) + 1]++;
    }
    apply_category(state);

    char kept_mount[128] = {0};
    if (state->disk_sel >= 0 && state->disk_sel < disk_count)
        gs_str_copy(kept_mount, sizeof kept_mount,
                    disks[state->disk_sel].mount);

    disk_count = report->disk_count;
    if (disk_count > GS_DETECT_MAX_DISKS)
        disk_count = GS_DETECT_MAX_DISKS;
    for (i = 0; i < disk_count; i++)
        disks[i] = report->disk[i];

    state->disk_count = disk_count;

    /* A disk the user already chose stays chosen across a refresh.
     * Otherwise the disk with the most room free starts chosen, since
     * that is the one a model would go on. */
    for (i = 0; i < disk_count && best < 0; i++)
        if (kept_mount[0] != '\0' &&
            strcmp(disks[i].mount, kept_mount) == 0)
            best = i;
    for (i = 0; i < disk_count; i++)
        if (best < 0 || (kept_mount[0] == '\0' &&
                         disks[i].free_bytes > disks[best].free_bytes))
            best = i;
    gs_ui_panes_pick_disk(state, best);

    gs_log_info("ui: %d models fit, %d disks mounted", runnable_count,
                disk_count);
}
