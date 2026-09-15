/* mount_fstab.c
 *
 * The table of filesystems the system keeps in /etc/fstab, read and
 * rewritten.
 *
 * A line there carrying the word user lets an ordinary person mount that
 * one entry. The drive, the filesystem type and every option are fixed by
 * whoever wrote the file, so the person mounting chooses nothing, which
 * is what makes the right safe to hand out. The word user also turns on
 * noexec, nosuid and nodev by itself, so no program can be run from the
 * drive and no file on it can gain rights.
 *
 * Writing this file needs administrator rights once. After that the line
 * is what carries the right, and nothing is ever asked again.
 *
 * Kept apart from the rest so neither file grows past what fits in a head.
 */
#include "mount.h"
#include "mount_internal.h"

#include "gravestone.h"
#include "log.h"
#include "str.h"

#include <stdio.h>
#include <string.h>

/* The options written against a drive. noauto keeps the system from
 * trying at boot, since this program does the mounting. user hands the
 * right to mount that one line to an ordinary person. The two numbers
 * name who owns the files once it is up, and they are worked out from
 * whoever is running rather than written in. */
#define FSTAB_TYPE "drvfs"

int gs_mount_fstab_parse(const char *line, gs_mount_fstab_t *out)
{
    char device[256];
    char point[GS_MOUNT_POINT_MAX];
    char type[64];
    char options[512];
    int fields;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    if (line == NULL)
        return 0;

    /* A comment describes nothing the system mounts. */
    while (*line == ' ' || *line == '\t')
        line++;
    if (*line == '#' || *line == '\0' || *line == '\n')
        return 0;

    fields = sscanf(line, "%255s %127s %63s %511s", device, point, type,
                    options);
    if (fields < 3)
        return 0;
    if (fields < 4)
        gs_str_copy(options, sizeof options, "defaults");

    if (strcmp(type, FSTAB_TYPE) != 0)
        return 0;

    /* A whole drive is one letter and a colon. A shared folder carries a
     * path after it, and putting one of those back needs a path this
     * module has no safe way to carry. */
    if (device[0] == '\0' || device[1] != ':' || device[2] != '\0')
        return 0;
    if (device[0] >= 'a' && device[0] <= 'z')
        device[0] = (char)(device[0] - 'a' + 'A');
    if (!gs_mount_letter_ok(device[0]))
        return 0;

    out->letter = device[0];
    gs_str_copy(out->point, sizeof out->point, point);
    gs_str_copy(out->options, sizeof out->options, options);
    out->mountable_by_user = gs_mount_option_present(options, "user") ||
                             gs_mount_option_present(options, "users");
    return 1;
}

int gs_mount_option_present(const char *options, const char *want)
{
    size_t len;
    const char *at;

    if (options == NULL || want == NULL || want[0] == '\0')
        return 0;
    len = strlen(want);
    at = options;

    /* The options are joined by commas, so the word has to fill a whole
     * gap between two of them. Looking for the letters anywhere would
     * find user inside nouser, which means the opposite. */
    for (;;) {
        const char *found = strstr(at, want);

        if (found == NULL)
            return 0;
        if ((found == options || found[-1] == ',') &&
            (found[len] == '\0' || found[len] == ','))
            return 1;
        at = found + 1;
    }
}

int gs_mount_fstab_read(gs_mount_fstab_t *out, int max)
{
    FILE *f;
    char line[1024];
    int count = 0;

    if (out == NULL || max <= 0)
        return GS_ERR_ARG;
    memset(out, 0, sizeof *out * (size_t)max);

    f = fopen(GS_MOUNT_FSTAB, "r");
    if (f == NULL)
        return 0;
    while (count < max && fgets(line, sizeof line, f) != NULL)
        if (gs_mount_fstab_parse(line, &out[count]))
            count++;
    fclose(f);
    return count;
}

int gs_mount_fstab_find(char letter, gs_mount_fstab_t *found)
{
    gs_mount_fstab_t rows[GS_MOUNT_MAX_DRIVES];
    int count;
    int i;

    if (found != NULL)
        memset(found, 0, sizeof *found);
    if (!gs_mount_letter_ok(letter))
        return 0;

    count = gs_mount_fstab_read(rows, GS_MOUNT_MAX_DRIVES);
    for (i = 0; i < count; i++)
        if (rows[i].letter == letter) {
            if (found != NULL)
                *found = rows[i];
            return 1;
        }
    return 0;
}

int gs_mount_fstab_line(char letter, const char *point, long uid, long gid,
                        char *out, size_t cap)
{
    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (!gs_mount_letter_ok(letter) || !gs_mount_pair_ok(letter, point))
        return GS_ERR_ARG;
    if (uid < 0 || gid < 0)
        return GS_ERR_ARG;

    if (snprintf(out, cap, "%c: %s %s %s%s,uid=%ld,gid=%ld 0 0\n",
                 letter, point, FSTAB_TYPE, GS_MOUNT_FSTAB_BASE,
                 GS_MOUNT_FSTAB_RIGHT, uid, gid) < 0)
        return GS_ERR;
    if (strlen(out) + 1 >= cap)
        return GS_ERR;
    return GS_OK;
}

int gs_mount_fstab_rewrite(char letter, const char *point, long uid, long gid,
                           char *out, size_t cap, size_t *len_out)
{
    FILE *f;
    char line[1024];
    char wanted[512];
    size_t filled = 0;
    int replaced = 0;

    if (len_out != NULL)
        *len_out = 0;
    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (gs_mount_fstab_line(letter, point, uid, gid, wanted,
                            sizeof wanted) != GS_OK)
        return GS_ERR_ARG;

    f = fopen(GS_MOUNT_FSTAB, "r");
    if (f != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            gs_mount_fstab_t row;
            const char *put = line;
            size_t len;

            /* The drive already has a line, so that line is replaced
             * rather than a second one added. Two lines naming one point
             * is a table nobody can read, and the system would act on
             * whichever came first. */
            if (gs_mount_fstab_parse(line, &row) && row.letter == letter) {
                put = wanted;
                replaced = 1;
            }
            len = strlen(put);
            if (filled + len + 2 >= cap) {
                fclose(f);
                return GS_ERR;
            }
            memcpy(out + filled, put, len);
            filled += len;
            if (filled > 0 && out[filled - 1] != '\n')
                out[filled++] = '\n';
            out[filled] = '\0';
        }
        fclose(f);
    }

    if (!replaced) {
        size_t len = strlen(wanted);

        if (filled + len + 1 >= cap)
            return GS_ERR;
        memcpy(out + filled, wanted, len);
        filled += len;
        out[filled] = '\0';
    }

    if (len_out != NULL)
        *len_out = filled;
    return GS_OK;
}

char gs_mount_wants_setup(void)
{
    char letters[GS_MOUNT_MAX_DRIVES + 1];
    int i;

    if (!gs_mount_under_wsl())
        return '\0';
    /* A machine that reaches root with nothing asked needs no line in
     * the table and no password from anybody, so the box stays shut.
     * Under WSL that is every machine with wsl.exe in its usual place. */
    if (gs_mount_root_free())
        return '\0';
    /* A road probe that never finished has decided nothing, and a box
     * opened on that would ask for a password the machine never needed.
     * The question waits until the road is known one way or the other. */
    if (!gs_mount_root_settled())
        return '\0';
    if (gs_mount_windows_letters(letters, sizeof letters) != GS_OK)
        return '\0';

    for (i = 0; letters[i] != '\0'; i++) {
        char point[16];

        if (gs_mount_point_for(letters[i], point, sizeof point) != GS_OK)
            continue;
        if (gs_mount_point_refused(point))
            continue;
        /* A drive already up needs nothing, and neither does one whose
         * line is already in the system table. */
        if (gs_mount_state(letters[i], NULL) == GS_MOUNT_HEALTHY)
            continue;
        if (gs_mount_fstab_grants(letters[i]))
            continue;
        /* Asked once and turned down is asked no more. */
        if (gs_mount_declined(letters[i]))
            continue;
        return letters[i];
    }
    return '\0';
}
