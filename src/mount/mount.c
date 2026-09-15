/* mount.c
 *
 * The half that is the same on every machine. Recognising a Windows
 * drive in the kernel mount table, checking a drive letter, working out
 * the mount point that belongs to it, and keeping the list in the
 * database. The half that touches the machine lives in mount_linux.c and
 * mount_win32.c.
 *
 * One rule runs through the whole module. A value that came out of the
 * database is shown to a person and is never handed to a command. The
 * letter is the only thing recovered from storage, it is checked against
 * a single capital A to Z before anything else happens, and the mount
 * point is worked out from it rather than read back. A row somebody
 * edited by hand can therefore name a different drive letter and nothing
 * more.
 *
 * No unmount here ever carries -l, --lazy, -f or --force, and none ever
 * will. Those two flags detach a filesystem while programs are still
 * writing into it, and both Windows drives on a WSL machine carry the
 * loose 9p cache, so a forced detach throws away everything written
 * since the last time the cache was emptied. A drive somebody is using
 * is reported as busy and left alone.
 */
#include "mount.h"
#include "mount_internal.h"

#include "gravestone.h"
#include "log.h"
#include "str.h"

#include <stdio.h>
#include <string.h>

int gs_mount_distro(char *out, size_t cap)
{
    /* The name Windows registered this Linux under. Reading it from the
     * surroundings is what the rest of the program already does, and the
     * reader there refuses a name carrying anything but letters, digits,
     * dot, dash and underscore. */
    return gs_str_wsl_distro(out, cap);
}

int gs_mount_letter_ok(char letter)
{
    return letter >= 'A' && letter <= 'Z';
}

int gs_mount_point_for(char letter, char *out, size_t cap)
{
    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (!gs_mount_letter_ok(letter))
        return GS_ERR_ARG;
    /* Seven bytes hold /mnt/ plus the letter plus the terminator. */
    if (cap < 7)
        return GS_ERR_ARG;
    out[0] = '/';
    out[1] = 'm';
    out[2] = 'n';
    out[3] = 't';
    out[4] = '/';
    out[5] = (char)(letter - 'A' + 'a');
    out[6] = '\0';
    return GS_OK;
}

/* Paths that are never touched, whatever is written down. The system
 * drive carries the Windows programs this application runs to give the
 * user a file box and to sweep leftover windows, /mnt/wslg carries the
 * sockets the screen is drawn through, and the three above them are
 * whole trees rather than drives. */
static const char *const refused[] = {
    "/", "/mnt", "/mnt/c", "/mnt/wsl", "/mnt/wslg"
};
#define REFUSED_COUNT ((int)(sizeof refused / sizeof refused[0]))

int gs_mount_point_refused(const char *point)
{
    int i;

    if (point == NULL || point[0] == '\0')
        return 1;
    for (i = 0; i < REFUSED_COUNT; i++)
        if (strcmp(point, refused[i]) == 0)
            return 1;
    return 0;
}

int gs_mount_pair_ok(char letter, const char *point)
{
    char want[16];

    if (point == NULL)
        return 0;
    if (gs_mount_point_for(letter, want, sizeof want) != GS_OK)
        return 0;
    return strcmp(point, want) == 0;
}

void gs_mount_plain(const char *text, char *out, size_t cap)
{
    size_t filled = 0;

    if (out == NULL || cap == 0)
        return;
    out[0] = '\0';
    if (text == NULL)
        return;

    while (*text != '\0' && filled + 1 < cap) {
        unsigned char c = (unsigned char)*text++;

        /* Anything below a space moves a terminal cursor about or starts
         * an escape run, and the rest are read by a shell. */
        if (c < 0x20 || c > 0x7e)
            continue;
        if (strchr(";&|<>$`\\\"'\n", (char)c) != NULL)
            continue;
        out[filled++] = (char)c;
    }
    out[filled] = '\0';
}

int gs_mount_hand_command(char letter, char *out, size_t cap)
{
    char point[16];

    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (gs_mount_point_for(letter, point, sizeof point) != GS_OK)
        return GS_ERR_ARG;
    if (snprintf(out, cap, "sudo mount -t drvfs %c: %s", letter, point) < 0)
        return GS_ERR;
    return GS_OK;
}

void gs_mount_unescape(char *text)
{
    char *read = text;
    char *write = text;

    if (text == NULL)
        return;
    while (*read != '\0') {
        if (read[0] == '\\' &&
            read[1] >= '0' && read[1] <= '7' &&
            read[2] >= '0' && read[2] <= '7' &&
            read[3] >= '0' && read[3] <= '7') {
            int value = (read[1] - '0') * 64 + (read[2] - '0') * 8 +
                        (read[3] - '0');

            *write++ = (char)value;
            read += 4;
            continue;
        }
        *write++ = *read++;
    }
    *write = '\0';
}

int gs_mount_parse_line(const char *line, gs_mount_drive_t *out)
{
    char device[256];
    char point[GS_MOUNT_POINT_MAX];
    char fs[64];
    char options[1024];

    if (line == NULL || out == NULL)
        return 0;
    memset(out, 0, sizeof *out);

    if (sscanf(line, "%255s %127s %63s %1023s", device, point, fs,
               options) != 4)
        return 0;

    gs_mount_unescape(device);
    gs_mount_unescape(point);

    /* Windows names the share drvfs in the options whichever filesystem
     * carries it, so that is the dependable mark. WSL version two reports
     * the type as 9p, which is an ordinary Linux filesystem carrying real
     * shares on other machines, and version one reports drvfs there. */
    if (strstr(options, "aname=drvfs") == NULL && strcmp(fs, "drvfs") != 0)
        return 0;

    /* A whole drive is written as one letter and a colon, with the
     * trailing backslash Windows puts on a root. Anything else is a
     * folder shared into the machine rather than a drive, and putting a
     * folder back needs a path this module has no safe way to carry. */
    if (device[0] == '\0' || device[1] != ':')
        return 0;
    if (device[2] != '\0' && !(device[2] == '\\' && device[3] == '\0'))
        return 0;
    if (device[0] >= 'a' && device[0] <= 'z')
        device[0] = (char)(device[0] - 'a' + 'A');
    if (!gs_mount_letter_ok(device[0]))
        return 0;

    out->letter = device[0];
    gs_str_copy(out->point, sizeof out->point, point);
    return 1;
}

int gs_mount_read(gs_mount_drive_t *out, int max)
{
    if (out == NULL || max <= 0)
        return GS_ERR_ARG;
    return gs_mount_table_read(out, max);
}

gs_mount_state_t gs_mount_state(char letter, gs_mount_drive_t *found)
{
    gs_mount_drive_t live[GS_MOUNT_MAX_DRIVES];
    char point[16];
    int count;
    int i;
    int exists = 0;

    if (found != NULL)
        memset(found, 0, sizeof *found);
    if (gs_mount_point_for(letter, point, sizeof point) != GS_OK)
        return GS_MOUNT_FOREIGN;

    count = gs_mount_table_read(live, GS_MOUNT_MAX_DRIVES);
    for (i = 0; i < count; i++) {
        if (strcmp(live[i].point, point) != 0)
            continue;
        /* The point is taken, and by a drive. A drive answering to a
         * different letter is somebody else's and is left alone. */
        if (live[i].letter != letter)
            return GS_MOUNT_FOREIGN;
        if (found != NULL)
            *found = live[i];
        return gs_mount_answers(point) ? GS_MOUNT_HEALTHY : GS_MOUNT_STALE;
    }

    /* Nothing of ours is there. Something else might be, and a real
     * filesystem sitting at that path is never taken down nor covered
     * over, even an empty one, since a disk attached by hand and not yet
     * written to is still somebody's disk. A directory this user cannot
     * read is treated the same way, because what it holds is unknown. */
    if (gs_mount_dir_at(point, &exists)) {
        if (gs_mount_is_mounted(point))
            return GS_MOUNT_FOREIGN;
        if (gs_mount_answers(point) && gs_mount_dir_has_content(point))
            return GS_MOUNT_FOREIGN;
    }
    return GS_MOUNT_ABSENT;
}

const char *gs_mount_state_name(gs_mount_state_t state)
{
    switch (state) {
    case GS_MOUNT_ABSENT:  return "absent";
    case GS_MOUNT_STALE:   return "stale";
    case GS_MOUNT_HEALTHY: return "healthy";
    case GS_MOUNT_FOREIGN: return "foreign";
    }
    return "unknown";
}

const char *gs_mount_outcome_name(gs_mount_outcome_t outcome)
{
    switch (outcome) {
    case GS_MOUNT_OK_ALREADY:   return "already there";
    case GS_MOUNT_OK_REMOUNTED: return "put back";
    case GS_MOUNT_SKIPPED:      return "skipped";
    case GS_MOUNT_BUSY:         return "in use";
    case GS_MOUNT_NO_RIGHTS:    return "no rights";
    case GS_MOUNT_GONE:         return "gone";
    case GS_MOUNT_DETACHED:     return "taken down";
    case GS_MOUNT_TIMED_OUT:    return "timed out";
    case GS_MOUNT_FAILED:       return "refused";
    }
    return "unknown";
}

int gs_mount_outcome_is_warning(gs_mount_outcome_t outcome)
{
    switch (outcome) {
    case GS_MOUNT_OK_ALREADY:
    case GS_MOUNT_OK_REMOUNTED:
    case GS_MOUNT_SKIPPED:
        return 0;
    default:
        return 1;
    }
}

int gs_mount_restore_all(char *note, size_t cap)
{
    gs_mount_drive_t saved[GS_MOUNT_MAX_DRIVES];
    char why[256];
    int known;
    int put_back = 0;
    int warned = 0;
    char warn_letter = '\0';
    gs_mount_outcome_t warn_outcome = GS_MOUNT_SKIPPED;
    int i;

    if (note != NULL && cap > 0)
        note[0] = '\0';
    if (!gs_mount_under_wsl())
        return 0;

    /* Two copies of the program starting together would otherwise have
     * one take a drive down while the other was reading it. */
    if (gs_mount_lock() != GS_OK) {
        gs_log_debug("mount: another copy is already doing this");
        return 0;
    }

    known = gs_mount_recall(saved, GS_MOUNT_MAX_DRIVES);

    /* A drive plugged in after this Linux started is in none of those
     * rows, because nothing has ever seen it working. Windows is asked
     * which drives it has, and any it names that this machine has not
     * mounted joins the list, which is what makes the very first run put
     * a drive up rather than sit there with an empty list. */
    {
        char letters[GS_MOUNT_MAX_DRIVES + 1];
        int j;

        if (gs_mount_windows_letters(letters, sizeof letters) == GS_OK)
            for (j = 0; letters[j] != '\0' &&
                        known < GS_MOUNT_MAX_DRIVES; j++) {
                char point[16];
                int seen = 0;

                if (gs_mount_point_for(letters[j], point, sizeof point)
                        != GS_OK)
                    continue;
                if (gs_mount_point_refused(point))
                    continue;
                for (i = 0; i < known; i++)
                    if (saved[i].letter == letters[j])
                        seen = 1;
                if (seen)
                    continue;
                memset(&saved[known], 0, sizeof saved[known]);
                saved[known].letter = letters[j];
                gs_str_copy(saved[known].point, sizeof saved[known].point,
                            point);
                known++;
            }
    }

    for (i = 0; i < known; i++) {
        gs_mount_outcome_t outcome;

        outcome = gs_mount_restore(saved[i].letter, why, sizeof why);
        if (outcome == GS_MOUNT_OK_REMOUNTED) {
            put_back++;
            /* Written down now rather than at the end, since this drive
             * may have come from Windows rather than from the stored
             * list and the next run has to know about it. */
            saved[i].seen_at = gs_mount_now();
            (void)gs_mount_remember(&saved[i]);
        }
        if (gs_mount_outcome_is_warning(outcome) && !warned) {
            warned = 1;
            warn_letter = saved[i].letter;
            warn_outcome = outcome;
        }
        if (why[0] != '\0' && gs_mount_outcome_is_warning(outcome))
            gs_log_warn("mount: %s", why);
        else if (why[0] != '\0')
            gs_log_info("mount: %s", why);
    }

    gs_mount_unlock();

    if (note != NULL && cap > 0) {
        if (put_back > 0)
            snprintf(note, cap, "%d DRIVE%s PUT BACK", put_back,
                     put_back == 1 ? "" : "S");
        else if (warned)
            snprintf(note, cap, "DRIVE %c: %s", warn_letter,
                     gs_mount_outcome_name(warn_outcome));
    }
    return put_back;
}

gs_mount_outcome_t gs_mount_restore(char letter, char *why, size_t cap)
{
    char point[16];
    gs_mount_state_t state;

    if (why != NULL && cap > 0)
        why[0] = '\0';
    if (!gs_mount_letter_ok(letter))
        return GS_MOUNT_SKIPPED;
    if (!gs_mount_under_wsl())
        return GS_MOUNT_SKIPPED;
    if (gs_mount_point_for(letter, point, sizeof point) != GS_OK)
        return GS_MOUNT_SKIPPED;
    if (gs_mount_point_refused(point)) {
        if (why != NULL)
            snprintf(why, cap, "%s belongs to the system and is left alone",
                     point);
        return GS_MOUNT_SKIPPED;
    }

    state = gs_mount_state(letter, NULL);
    if (state == GS_MOUNT_HEALTHY) {
        if (why != NULL)
            snprintf(why, cap, "%c: is already mounted at %s", letter, point);
        return GS_MOUNT_OK_ALREADY;
    }
    if (state == GS_MOUNT_FOREIGN) {
        if (why != NULL)
            snprintf(why, cap,
                     "%s holds something that is not drive %c:, so it is "
                     "left alone", point, letter);
        return GS_MOUNT_SKIPPED;
    }

    return gs_mount_repair(letter, point, state, why, cap);
}

static int mount_init(void)
{
    return gs_mount_open();
}

static int mount_run(int argc, char **argv)
{
    gs_mount_drive_t saved[GS_MOUNT_MAX_DRIVES];
    char note[64];
    char why[256];
    int known;
    int i;

    (void)argc;
    (void)argv;

    if (!gs_mount_under_wsl()) {
        printf("this machine has no Windows drives to put back\n");
        return GS_OK;
    }

    gs_mount_keep_reading();
    known = gs_mount_recall(saved, GS_MOUNT_MAX_DRIVES);
    printf("%d drive%s written down\n", known, known == 1 ? "" : "s");
    for (i = 0; i < known; i++) {
        gs_mount_state_t state = gs_mount_state(saved[i].letter, NULL);

        printf("  %c: at %-8s %-8s missed %d run%s\n", saved[i].letter,
               saved[i].point, gs_mount_state_name(state), saved[i].misses,
               saved[i].misses == 1 ? "" : "s");
    }

    (void)gs_mount_restore_all(note, sizeof note);
    for (i = 0; i < known; i++) {
        gs_mount_outcome_t outcome;

        outcome = gs_mount_restore(saved[i].letter, why, sizeof why);
        printf("  %c: %s\n", saved[i].letter,
               why[0] != '\0' ? why : gs_mount_outcome_name(outcome));
    }
    return GS_OK;
}

static void mount_shutdown(void)
{
    gs_mount_close();
}

const gs_module gs_mount_module = {
    "mount",
    "put back the Windows drives this machine had attached",
    mount_init,
    mount_run,
    mount_shutdown
};
