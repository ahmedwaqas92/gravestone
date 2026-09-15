/* ui_mount.c
 *
 * The Windows drives, put back on a thread of their own.
 *
 * Putting a drive back can take ten seconds the first time, since the
 * road to root through wsl.exe starts a session on its first use, and it
 * used to run before the window opened, so the window waited on it.
 * It runs here instead, once at startup and then every so often, so a
 * drive plugged in while the program is up comes up on its own and one
 * pulled out has its dead entry cleared. The window polls for what the
 * thread found and refreshes the disks when anything changed.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "log.h"
#include "mount.h"
#include "str.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

/* How often the thread looks again, in seconds. A look at a healthy
 * drive costs a read of the mount table, one statvfs, and one short
 * Windows program listing the drives, about a tenth of a second. */
#define GS_UI_MOUNT_EVERY 15

static struct {
    pthread_t       thread;
    pthread_mutex_t lock;
    atomic_int      stop;
    atomic_int      changed;      /* a drive came or went since the last poll */
    int             running;
    int             passes;
    int             asked;        /* the setup question has been settled */
    char            note[64];     /* the last word for the status line */
    char            last[64];     /* what the pass before this one said */
    char            wants;        /* a drive needing the setup box, or nought */
} watch;

static void *watch_worker(void *arg)
{
    (void)arg;
    for (;;) {
        char note[64] = {0};
        int put_back;
        int i;

        put_back = gs_mount_restore_all(note, sizeof note);

        pthread_mutex_lock(&watch.lock);
        watch.passes++;
        /* The rows are kept once a run, since a miss is counted each
         * time this is called and five in a row forget the drive. Five
         * passes would be a drive unplugged for a minute and a quarter,
         * which is a drive somebody is about to plug back in. */
        if (watch.passes == 1)
            gs_mount_keep_reading();
        /* A drive this machine has no road to root for is asked about
         * once, in the box. The question waits until the road probe has
         * answered, since a probe that timed out decides nothing. */
        if (!watch.asked && gs_mount_root_settled()) {
            watch.wants = gs_mount_wants_setup();
            watch.asked = 1;
            /* The box is opened by the poll, which only runs on a
             * report, and the word for the drive may be the same one
             * as last pass. */
            if (watch.wants != '\0')
                atomic_store(&watch.changed, 1);
        }
        /* A word that repeats is not news. A drive that has gone gives
         * the same warning every pass for as long as it is away, and a
         * refresh of the disks every fifteen seconds on the strength of
         * it would have the window rereading the machine for nothing. */
        if (put_back > 0 || strcmp(note, watch.last) != 0) {
            gs_str_copy(watch.note, sizeof watch.note, note);
            gs_str_copy(watch.last, sizeof watch.last, note);
            atomic_store(&watch.changed, 1);
        } else if (watch.passes == 1) {
            /* The first pass says it finished even when nothing needed
             * doing, so the window reads the disks once the drives are
             * known to be settled. */
            atomic_store(&watch.changed, 1);
        }
        pthread_mutex_unlock(&watch.lock);

        /* Sleeping in short steps rather than one long one, so a stop
         * at shutdown is answered within a moment. */
        for (i = 0; i < GS_UI_MOUNT_EVERY * 4; i++) {
            struct timespec quarter;

            if (atomic_load(&watch.stop))
                return NULL;
            quarter.tv_sec = 0;
            quarter.tv_nsec = 250000000L;
            nanosleep(&quarter, NULL);
        }
        if (atomic_load(&watch.stop))
            return NULL;
    }
}

int gs_ui_mount_begin(void)
{
    if (watch.running)
        return GS_OK;
    /* Nothing to watch off WSL, and starting a thread to find that out
     * every fifteen seconds would be waste. */
    if (!gs_mount_under_wsl())
        return GS_ERR;

    pthread_mutex_init(&watch.lock, NULL);
    atomic_store(&watch.stop, 0);
    atomic_store(&watch.changed, 0);
    watch.passes = 0;
    watch.asked = 0;
    watch.note[0] = '\0';
    watch.last[0] = '\0';
    watch.wants = '\0';
    if (pthread_create(&watch.thread, NULL, watch_worker, NULL) != 0) {
        gs_log_warn("ui: no thread for the drives, they are left as found");
        return GS_ERR;
    }
    watch.running = 1;
    return GS_OK;
}

int gs_ui_mount_running(void)
{
    return watch.running;
}

int gs_ui_mount_poll(gs_ui_state_t *state)
{
    if (state == NULL || !watch.running)
        return 0;
    if (!atomic_load(&watch.changed))
        return 0;

    /* The refresh below rewrites the library table, which a removal or
     * a pull on its thread is reading, and moves the disk a confirm box
     * on the screen is asking about. The report keeps until the window
     * is quiet again, which is a matter of moments. */
    if (gs_ui_remove_running() || gs_ui_attach_running() ||
        gs_ui_confirm_visible(state) || state->confirm_busy)
        return 0;
    atomic_store(&watch.changed, 0);

    pthread_mutex_lock(&watch.lock);
    if (watch.note[0] != '\0') {
        gs_str_copy(state->status, sizeof state->status, watch.note);
        watch.note[0] = '\0';
    }
    if (watch.wants != '\0' && !gs_ui_secret_visible(state)) {
        gs_ui_secret_open(state, watch.wants);
        watch.wants = '\0';
    }
    pthread_mutex_unlock(&watch.lock);

    /* The disks are read again, so one that just came up is in the list
     * and one that went away is out of it, and the saved choice of disk
     * lands on it again along with the models ticked on it. A disk the
     * person picked earlier counts as picked no longer, since the list
     * it was picked from has changed under it. */
    state->disk_picked = 0;
    gs_ui_panes_refresh(state, NULL);
    gs_ui_settings_restore(state);
    return 1;
}

void gs_ui_mount_wait(void)
{
    if (!watch.running)
        return;
    atomic_store(&watch.stop, 1);
    pthread_join(watch.thread, NULL);
    pthread_mutex_destroy(&watch.lock);
    watch.running = 0;
}
