/* ui_settings.c
 *
 * What the interface remembers between sessions, and the check that runs
 * when it starts. The chosen disk, category and model go into the local
 * database, together with an inventory of the models that disk held.
 *
 * A session opening on a machine whose disk has gone away still knows
 * what the user had, so it can say which disk to plug back in rather
 * than showing an empty pane with no explanation.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_DISK     "disk.mount"
#define KEY_CATEGORY "pane.category"
#define KEY_MODEL    "model.chosen"
#define KEY_FLEET    "models.chosen"

static int saved_disk_index(const char *mount);

void gs_ui_settings_record_models(const gs_ui_state_t *state)
{
    const gs_detect_disk_t *disk;
    int i;

    if (state == NULL)
        return;
    disk = gs_ui_panes_disk(state->disk_sel);
    if (disk == NULL)
        return;

    /* A walk that found nothing is a signal rather than a fact worth
     * writing down. A drive that has gone away, a share still attaching,
     * or a mount that landed on the wrong volume all read as an empty
     * disk, and wiping the record then would lose the only list of what
     * the person owns. The old record survives instead, which is what
     * puts the missing drive message on the screen. */
    if (gs_library_count() == 0) {
        gs_log_warn("settings: %s holds no models, so the record of what "
                    "was there is kept rather than replaced", disk->mount);
        return;
    }

    /* The table describes one disk at a time, so the old contents go
     * before the new walk is written. */
    gs_store_model_forget_all();
    for (i = 0; i < gs_library_count(); i++) {
        const gs_library_entry_t *e = gs_library_at(i);

        if (e != NULL)
            gs_store_model_remember(e->name, e->path, disk->mount, e->bytes);
    }
    gs_log_debug("settings: %d models recorded against %s",
                 gs_library_count(), disk->mount);
}

/* Adds one name to a newline separated list, unless it is already there
 * or will not fit whole. */
static void add_name(char *list, size_t cap, const char *name)
{
    size_t have = strlen(list);
    size_t len = strlen(name);
    const char *at = list;

    if (len == 0)
        return;
    while ((at = strstr(at, name)) != NULL) {
        if ((at == list || at[-1] == '\n') &&
            (at[len] == '\0' || at[len] == '\n'))
            return;
        at += len;
    }
    if (have + (have > 0 ? 1 : 0) + len + 1 > cap)
        return;
    if (have > 0)
        list[have++] = '\n';
    memcpy(list + have, name, len + 1);
}

/* True when the library loaded right now holds a model of that name. */
static int in_library(const char *name, size_t len)
{
    int i;

    for (i = 0; i < gs_library_count(); i++) {
        const gs_library_entry_t *e = gs_library_at(i);

        if (e != NULL && strlen(e->name) == len &&
            strncmp(e->name, name, len) == 0)
            return 1;
    }
    return 0;
}

void gs_ui_settings_merge_chosen(const gs_ui_state_t *state,
                                 const char *saved, char *out, size_t cap)
{
    int i;

    if (out == NULL || cap == 0)
        return;
    out[0] = '\0';
    if (state == NULL)
        return;

    /* Every model ticked in the library that is loaded now. */
    for (i = 0; i < gs_library_count() && i < GS_UI_CHOSEN_MAX; i++) {
        const gs_library_entry_t *e = gs_library_at(i);

        if (e != NULL && gs_ui_fleet_chosen(state, i))
            add_name(out, cap, e->name);
    }

    /* And every name remembered from before whose model is not in that
     * library at all. A disk that is missing when the program starts
     * loads no models, and a save made then used to write an empty list
     * over the one the person chose. A model that is here and unticked
     * is the person's decision, so only a model that is absent keeps its
     * place. */
    if (saved != NULL) {
        const char *at = saved;

        while (*at != '\0') {
            const char *end = strchr(at, '\n');
            size_t len = end != NULL ? (size_t)(end - at) : strlen(at);

            if (len > 0 && len < 256 && !in_library(at, len)) {
                char one[256];

                memcpy(one, at, len);
                one[len] = '\0';
                add_name(out, cap, one);
            }
            if (end == NULL)
                break;
            at = end + 1;
        }
    }
}

void gs_ui_settings_save(const gs_ui_state_t *state)
{
    const gs_detect_disk_t *disk;
    char number[16];
    int keep_saved = 0;

    if (state == NULL)
        return;

    /* A disk chosen last time and away now keeps its place. The pane
     * fell back to the first disk on its own, and writing that over the
     * choice would forget the drive the moment it was unplugged. A disk
     * the user picks while the other is away is written as picked. */
    disk = gs_ui_panes_disk(state->disk_sel);
    if (disk != NULL) {
        char saved[128] = {0};

        (void)gs_store_setting_get(KEY_DISK, saved, sizeof saved);
        if (state->disk_picked || saved[0] == '\0' ||
            saved_disk_index(saved) >= 0)
            gs_store_setting_put(KEY_DISK, disk->mount);
        else
            keep_saved = 1;
    }

    snprintf(number, sizeof number, "%d", state->category_sel);
    gs_store_setting_put(KEY_CATEGORY, number);

    /* The chosen models are written by name, since a position in the
     * library shifts the moment a model is added or removed. */
    {
        char saved[900] = {0};
        char names[900] = {0};

        (void)gs_store_setting_get(KEY_FLEET, saved, sizeof saved);
        gs_ui_settings_merge_chosen(state, saved, names, sizeof names);
        gs_store_setting_put(KEY_FLEET, names);
    }

    {
        const gs_catalogue_entry_t *picked =
            gs_ui_panes_model(state->model_sel);

        gs_store_setting_put(KEY_MODEL, picked != NULL ? picked->file : "");
    }
    /* The record of models describes the chosen disk, so while that disk
     * is away it is left alone too. Rewriting it from the disk the pane
     * fell back to would count that disk's models against the missing
     * one at the next start. */
    if (!keep_saved)
        gs_ui_settings_record_models(state);
}

/* Finds the saved disk among the disks this machine has now. Returns its
 * index, or minus one when the disk is absent. */
static int saved_disk_index(const char *mount)
{
    int i;

    if (mount[0] == '\0')
        return -1;
    for (i = 0; i < gs_ui_panes_disk_count(); i++) {
        const gs_detect_disk_t *d = gs_ui_panes_disk(i);

        if (d != NULL && strcmp(d->mount, mount) == 0)
            return i;
    }
    return -1;
}

/* Ticks the models the last session chose, matching by name so a list
 * that has grown or shrunk still lands on the right ones. */
static void restore_chosen(gs_ui_state_t *state)
{
    char names[900] = {0};
    int i;

    memset(state->model_chosen, 0, sizeof state->model_chosen);
    state->chosen_count = 0;
    if (gs_store_setting_get(KEY_FLEET, names, sizeof names) != GS_OK)
        return;

    for (i = 0; i < gs_library_count() && i < GS_UI_CHOSEN_MAX; i++) {
        const gs_library_entry_t *e = gs_library_at(i);
        const char *at = names;

        if (e == NULL || e->name[0] == '\0')
            continue;
        while ((at = strstr(at, e->name)) != NULL) {
            size_t n = strlen(e->name);
            int starts = at == names || at[-1] == '\n';
            int ends = at[n] == '\0' || at[n] == '\n';

            if (starts && ends) {
                state->model_chosen[i] = 1;
                state->chosen_count++;
                break;
            }
            at += n;
        }
    }
}

void gs_ui_settings_restore(gs_ui_state_t *state)
{
    char mount[128] = {0};
    char number[16] = {0};
    char model[160] = {0};
    int index;

    if (state == NULL)
        return;

    state->settings_ready = gs_store_setting_count() > 0;
    state->alert[0] = '\0';
    if (!state->settings_ready)
        return;

    if (gs_store_setting_get(KEY_CATEGORY, number, sizeof number) == GS_OK) {
        int wanted = atoi(number);

        if (wanted > 0 && wanted < gs_ui_panes_category_count())
            gs_ui_panes_pick_category(state, wanted);
    }

    if (gs_store_setting_get(KEY_DISK, mount, sizeof mount) != GS_OK)
        return;

    index = saved_disk_index(mount);
    if (index >= 0) {
        gs_ui_panes_pick_disk(state, index);
        if (state->library_count == 0 && gs_store_model_load() > 0) {
            char shown[64];

            gs_str_copy(shown, sizeof shown, mount);
            snprintf(state->alert, sizeof state->alert,
                     "%s holds none of the %d models recorded on it",
                     shown, gs_store_model_count());
        }
        gs_ui_settings_record_models(state);
        restore_chosen(state);
    } else {
        int remembered = gs_store_model_load();

        /* The mount is cut to what the line can carry, since a warning
         * that overruns its buffer tells the user nothing. */
        char shown[64];

        gs_str_copy(shown, sizeof shown, mount);
        if (remembered > 0)
            snprintf(state->alert, sizeof state->alert,
                     "%s is missing, with %d model(s) on it. Attach it or "
                     "add a model to a disk that is here.",
                     shown, remembered);
        else
            snprintf(state->alert, sizeof state->alert,
                     "%s is missing. Attach it or choose another disk.",
                     shown);
        gs_log_warn("settings: %s", state->alert);
    }

    if (gs_store_setting_get(KEY_MODEL, model, sizeof model) == GS_OK &&
        model[0] != '\0') {
        int i;

        for (i = 0; i < state->model_count; i++) {
            const gs_catalogue_entry_t *e = gs_ui_panes_model(i);

            if (e != NULL && strcmp(e->file, model) == 0) {
                state->model_sel = i;
                state->model_scroll = i > 3 ? i - 3 : 0;
                break;
            }
        }
    }
}
