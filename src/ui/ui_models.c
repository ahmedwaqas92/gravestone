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

/* The reading the list above was built from. Every label drawn against
 * that list has to come from this same reading, since free memory moves
 * while the program runs and a second reading would call a row too large
 * inside a panel headed by what fits. */
static gs_detect_report_t held;
static int  held_valid;
static int  filtered[UI_MAX_MODELS];
static int  filtered_count;

/* One slot per category, plus slot 0 for all of them together. */
/* The chooser holds two kinds of question. Everything, then one entry
 * per kind of model, then one entry per place a model would run. A person
 * looking for something to talk to wants the second half, and a person
 * looking for a code model wants the first, so both live in the one list
 * rather than behind two controls. */
#define UI_KINDS      ((int)GS_KIND_COUNT + 1)
#define UI_PLACES     4
#define UI_CATEGORIES (UI_KINDS + UI_PLACES)

/* Which place each of the last four entries stands for, in the order the
 * chooser lists them. */
static const gs_catalogue_fit_t place_of[UI_PLACES] = {
    GS_FIT_EITHER,
    GS_FIT_GRAPHICS,
    GS_FIT_PROCESSOR,
    GS_FIT_PARTIAL
};
static int  category_total[UI_CATEGORIES];

int gs_ui_panes_category_count(void) { return UI_CATEGORIES; }

/* The name of the divided entry, carrying the share of itself a model
 * has to put on the card to be listed. Built from the constant rather
 * than typed out, so the words and the rule cannot disagree. */
static const char *divided_label(void)
{
    static char label[48];

    if (label[0] == '\0')
        snprintf(label, sizeof label, "Divided, %d%% or more on the card",
                 GS_CATALOGUE_SPLIT_FLOOR);
    return label;
}

const char *gs_ui_panes_category_name(int index)
{
    if (index <= 0)
        return "All models";
    if (index < UI_KINDS)
        return gs_catalogue_kind_name((gs_catalogue_kind_t)(index - 1));
    if (index >= UI_CATEGORIES)
        return "unknown";

    switch (place_of[index - UI_KINDS]) {
    case GS_FIT_EITHER:    return "Runs on card or processor";
    case GS_FIT_GRAPHICS:  return "Runs on the card";
    case GS_FIT_PROCESSOR: return "Runs on the processor";
    /* Nothing below the floor reaches the list at all, so the entry names
     * where the divided ones begin. The figure is written out from the
     * constant the listing uses, since two copies of it would drift. */
    default:               return divided_label();
    }
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

    /* The tallies beside the entries count what a person would actually
     * see, so they are worked out here against the search rather than
     * once at load time. A tally claiming two thousand rows over a list
     * showing nine is worse than no tally. */
    for (i = 0; i < UI_CATEGORIES; i++)
        category_total[i] = 0;

    filtered_count = 0;
    for (i = 0; i < runnable_count; i++) {
        const gs_catalogue_entry_t *entry = gs_catalogue_at(runnable[i]);

        if (entry == NULL)
            continue;
        if (!name_contains(entry->file, state->search))
            continue;
        category_total[0]++;
        category_total[(int)gs_catalogue_kind(entry) + 1]++;
        {
            gs_catalogue_fit_t fit = gs_catalogue_fit(entry->bytes,
                                         held_valid ? &held : NULL);
            int k;

            for (k = 0; k < UI_PLACES; k++)
                if (place_of[k] == fit)
                    category_total[UI_KINDS + k]++;
        }
        if (state->category_sel > 0 && state->category_sel < UI_KINDS &&
            (int)gs_catalogue_kind(entry) != state->category_sel - 1)
            continue;
        if (state->category_sel >= UI_KINDS &&
            gs_catalogue_fit(entry->bytes, held_valid ? &held : NULL)
            != place_of[state->category_sel - UI_KINDS])
            continue;
        /* Every row reaching here came out of gs_catalogue_runnable, which
         * already asked gs_catalogue_listed, so a divided row shown under
         * the divided entry is one that clears the floor. */
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

void gs_ui_where(long long bytes, const gs_detect_report_t *machine,
                 char *out, size_t cap)
{
    gs_catalogue_fit_t fit;
    long long on_card = 0;
    long long in_memory = 0;

    if (out == NULL || cap == 0)
        return;
    out[0] = '\0';
    if (machine == NULL || bytes <= 0)
        return;

    fit = gs_catalogue_fit(bytes, machine);
    /* A divided model carries both shares, since a person reading the row
     * wants to know how much of it sits outside the card. Every byte
     * outside crosses the bus once for every word produced.
     *
     * The floor is left out here on purpose. A row in the catalogue list
     * has already cleared it, and a model on the disk that would not have
     * cleared it still runs, so calling it too large would be a lie. */
    if (fit == GS_FIT_PARTIAL &&
        gs_catalogue_share(bytes, machine, &on_card, &in_memory) == GS_OK &&
        on_card + in_memory > 0) {
        long long whole = on_card + in_memory;
        long long card_share = on_card * 100 / whole;

        snprintf(out, cap, "GPU %lld%% CPU %lld%%",
                 card_share, 100 - card_share);
        return;
    }
    switch (fit) {
    case GS_FIT_EITHER:    gs_str_copy(out, cap, "GPU + CPU"); break;
    case GS_FIT_GRAPHICS:  gs_str_copy(out, cap, "GPU");       break;
    case GS_FIT_PROCESSOR: gs_str_copy(out, cap, "CPU");       break;
    default:               gs_str_copy(out, cap, "TOO LARGE"); break;
    }
}

int gs_ui_model_tag(long long bytes, const gs_detect_report_t *machine,
                    gs_ui_tag_t *out)
{
    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    if (machine == NULL || bytes <= 0)
        return 0;

    gs_ui_where(bytes, machine, out->text, sizeof out->text);
    if (out->text[0] == '\0')
        return 0;

    switch (gs_catalogue_fit(bytes, machine)) {
    case GS_FIT_EITHER:
    case GS_FIT_GRAPHICS:
        out->ink = GS_UI_TAG_CARD_INK;
        out->face = GS_UI_TAG_CARD_FACE;
        break;
    case GS_FIT_PROCESSOR:
        out->ink = GS_UI_TAG_CPU_INK;
        out->face = GS_UI_TAG_CPU_FACE;
        break;
    case GS_FIT_PARTIAL:
        out->ink = GS_UI_TAG_SPLIT_INK;
        out->face = GS_UI_TAG_SPLIT_FACE;
        break;
    default:
        out->ink = GS_UI_TAG_NONE_INK;
        out->face = GS_UI_TAG_NONE_FACE;
        break;
    }

    /* The pill is sized from the letters, measured by the glyph width the
     * window reported, since the pass that fills the shape has no window
     * to ask. The face is fixed width, so counting characters is exact. */
    {
        int glyph = gs_ui_glyph_width();

        out->box.w = glyph > 0
                   ? (int)strlen(out->text) * glyph + 2 * GS_UI_TAG_PAD
                   : 0;
        out->box.h = GS_UI_TAG_HEIGHT;
    }
    return out->box.w > 0;
}

const gs_detect_report_t *gs_ui_panes_machine(void)
{
    return held_valid ? &held : NULL;
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
            held_valid = 0;
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

    held = *report;
    held_valid = 1;
    runnable_count = gs_catalogue_runnable(report, runnable, UI_MAX_MODELS);

    /* apply_category fills the tallies, since they have to count what the
     * search leaves rather than the whole list. */
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
