/* mount_repair.c
 *
 * The four commands that put a drive back, and the one question of
 * whether they can be run at all.
 *
 * Every command here runs through gs_proc_run, which starts a program
 * directly with its arguments kept apart from one another. The other two
 * ways this program runs things hand their text to /bin/sh, and a shell
 * reads a semicolon, a backtick and an ampersand as instructions, so a
 * value that came out of a database would be able to carry a second
 * command inside it. A command running as the administrator is the one
 * place where that has to be impossible rather than unlikely.
 *
 * Nothing here carries -l, --lazy, -f or --force, and nothing here ever
 * will. Those flags detach a filesystem while programs are still writing
 * into it, and a Windows drive carries the loose 9p cache, so a forced
 * detach throws away everything written since the cache was last
 * emptied. A drive somebody is holding open is reported as busy and left
 * where it is.
 *
 * Kept apart from mount_linux.c so neither file grows past what fits in
 * a head.
 */
#include "mount.h"
#include "mount_internal.h"

#include "gravestone.h"
#include "log.h"
#include "paths.h"
#include "proc.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* Absolute paths, because a name looked up on the search path is looked
 * up in whatever directories that path names. Under WSL most of those
 * sit on the Windows drive, where another program can write, so a name
 * would be an invitation. The tools move about between distributions, so
 * each is tried in the usual places and the first one really there is
 * the one that runs. */
static const char *const mount_paths[] = { "/usr/bin/mount", "/bin/mount",
                                           "/sbin/mount", "/usr/sbin/mount" };
static const char *const umount_paths[] = { "/usr/bin/umount", "/bin/umount",
                                            "/sbin/umount",
                                            "/usr/sbin/umount" };
static const char *const mkdir_paths[] = { "/bin/mkdir", "/usr/bin/mkdir" };
static const char *const cp_paths[]    = { "/bin/cp", "/usr/bin/cp" };
static const char *const sudo_paths[]  = { "/usr/bin/sudo", "/bin/sudo" };

#define FIRST_REAL(a) \
    gs_mount_first_real((a), (int)(sizeof (a) / sizeof (a)[0]))

gs_mount_outcome_t gs_mount_repair(char letter, const char *point,
                                   gs_mount_state_t state,
                                   char *why, size_t cap)
{
    const char *umount_bin = FIRST_REAL(umount_paths);
    const char *mount_bin = FIRST_REAL(mount_paths);
    const char *mkdir_bin = FIRST_REAL(mkdir_paths);
    char device[4];
    char said[512];
    char clean[512];
    char hand[96];
    const char *tail[6];
    int code = -1;
    int exists = 0;
    int was_stale = 0;

    if (why != NULL && cap > 0)
        why[0] = '\0';
    /* The letter has already been checked by the caller, and the point
     * was worked out from it rather than read back from anywhere. Both
     * are checked again here because this is the last place before a
     * command runs. */
    if (!gs_mount_letter_ok(letter) || !gs_mount_pair_ok(letter, point))
        return GS_MOUNT_SKIPPED;
    if (gs_mount_point_refused(point))
        return GS_MOUNT_SKIPPED;
    if (mount_bin == NULL) {
        if (why != NULL)
            snprintf(why, cap, "this machine has no mount command");
        return GS_MOUNT_FAILED;
    }
    gs_mount_hand_command(letter, hand, sizeof hand);

    device[0] = letter;
    device[1] = ':';
    device[2] = '\0';

    /* A dead mount somebody is still reading is left where it is,
     * whatever rights this program has. A model file is mapped into
     * memory rather than read, so taking the drive out from under the
     * program holding it kills that program outright. */
    if (state == GS_MOUNT_STALE) {
        char holder[160];
        int holders = gs_mount_holders(point, holder, sizeof holder);

        if (holders > 0) {
            if (why != NULL)
                snprintf(why, cap,
                         "drive %c: is dead and %d program%s still holding "
                         "%s open, starting with %s, so it is left alone. "
                         "Close it and run: %s",
                         letter, holders, holders == 1 ? " is" : "s are",
                         point, holder[0] != '\0' ? holder : "one of them",
                         hand);
            return GS_MOUNT_BUSY;
        }
    }

    /* An entry in /etc/fstab carrying the word user lets an ordinary
     * person mount that one line, with the drive, the type and the
     * options all fixed by whoever wrote the file. Trying that first
     * costs one program start and is the only route that needs no
     * administrator rights at all, so a machine set up that way puts its
     * drive back with nothing asked of anybody. A machine with no such
     * line refuses in a few milliseconds and the attempt below runs. */
    if (state == GS_MOUNT_ABSENT && gs_mount_fstab_grants(letter) &&
        gs_mount_dir_at(point, &exists)) {
        const char *argv[3];

        argv[0] = mount_bin;
        argv[1] = point;
        argv[2] = NULL;
        if (gs_proc_run(mount_bin, argv, said, sizeof said, &code,
                        GS_MOUNT_SECONDS) == GS_OK && code == 0 &&
            gs_mount_state(letter, NULL) == GS_MOUNT_HEALTHY) {
            if (why != NULL)
                snprintf(why, cap,
                         "drive %c: is back at %s, mounted from the line in "
                         "%s with nothing asked", letter, point,
                         GS_MOUNT_FSTAB);
            return GS_MOUNT_OK_REMOUNTED;
        }
        gs_mount_plain(said, clean, sizeof clean);
        gs_log_debug("mount: the %s line for %c: would not mount: %s",
                     GS_MOUNT_FSTAB, letter,
                     clean[0] != '\0' ? clean : "no reason given");
    }

    if (!gs_mount_root_free()) {
        if (why != NULL) {
            /* A machine whose table already grants the right and still
             * would not mount has a different problem from one that was
             * never set up, and the person needs to be told which. */
            if (gs_mount_fstab_grants(letter))
                snprintf(why, cap,
                         "drive %c: has its line in %s and would still not "
                         "mount. Run: %s", letter, GS_MOUNT_FSTAB, hand);
            else
                snprintf(why, cap,
                         "drive %c: needs a line in %s before it can be "
                         "mounted without a password. Setting that up asks "
                         "for the machine password once", letter,
                         GS_MOUNT_FSTAB);
        }
        return GS_MOUNT_NO_RIGHTS;
    }

    if (state == GS_MOUNT_STALE) {
        tail[0] = point;
        if (gs_mount_as_root(umount_bin != NULL ? umount_bin : "/bin/umount", tail, 1,
                    said, sizeof said, &code) != GS_OK) {
            gs_mount_plain(said, clean, sizeof clean);
            if (why != NULL)
                snprintf(why, cap,
                         "taking drive %c: down did not finish. Run: %s",
                         letter, hand);
            return GS_MOUNT_TIMED_OUT;
        }
        was_stale = 1;
        /* What the command returned decides nothing. The mount table is
         * read again instead, since a command that worked in silence and
         * a command that failed look the same from the outside. */
        if (gs_mount_state(letter, NULL) == GS_MOUNT_STALE) {
            gs_mount_plain(said, clean, sizeof clean);
            if (why != NULL)
                snprintf(why, cap,
                         "drive %c: is dead and would not come down: %s",
                         letter, clean[0] != '\0' ? clean : "no reason given");
            return GS_MOUNT_BUSY;
        }
    }

    /* The mount point is made only when nothing is there. Nothing here
     * ever removes a directory, because removing one buys nothing that
     * mounting does not already do, and a program stopped between the
     * removal and the remake would leave the path gone. */
    if (!gs_mount_dir_at(point, &exists)) {
        if (exists) {
            if (why != NULL)
                snprintf(why, cap,
                         "%s is not a directory, so drive %c: has nowhere "
                         "to go", point, letter);
            return was_stale ? GS_MOUNT_DETACHED : GS_MOUNT_FAILED;
        }
        tail[0] = "-p";
        tail[1] = point;
        if (gs_mount_as_root(mkdir_bin != NULL ? mkdir_bin : "/bin/mkdir", tail, 2,
                    said, sizeof said, &code) != GS_OK) {
            if (why != NULL)
                snprintf(why, cap, "%s could not be made. Run: %s", point,
                         hand);
            return was_stale ? GS_MOUNT_DETACHED : GS_MOUNT_FAILED;
        }
    } else if (gs_mount_dir_has_content(point)) {
        /* Linux mounts over a full directory without complaint, and the
         * files underneath are hidden rather than removed. Their bytes go
         * on filling the disk while the interface reports the drive's own
         * free space, so a download sized against the drive fills the
         * system disk instead. */
        if (why != NULL)
            snprintf(why, cap,
                     "%s already holds files, so mounting drive %c: over it "
                     "would hide them. Empty it first, then run: %s",
                     point, letter, hand);
        return was_stale ? GS_MOUNT_DETACHED : GS_MOUNT_FAILED;
    }

    tail[0] = "-t";
    tail[1] = "drvfs";
    tail[2] = device;
    tail[3] = point;
    if (gs_mount_as_root(mount_bin, tail, 4, said, sizeof said, &code) != GS_OK) {
        if (why != NULL)
            snprintf(why, cap, "mounting drive %c: did not finish. Run: %s",
                     letter, hand);
        return GS_MOUNT_TIMED_OUT;
    }

    if (gs_mount_state(letter, NULL) == GS_MOUNT_HEALTHY) {
        if (why != NULL)
            snprintf(why, cap, "drive %c: is back at %s", letter, point);
        return GS_MOUNT_OK_REMOUNTED;
    }

    gs_mount_plain(said, clean, sizeof clean);
    if (why != NULL)
        snprintf(why, cap,
                 "drive %c: is not in the machine: %s. Plug it in and run: %s",
                 letter, clean[0] != '\0' ? clean : "the mount was refused",
                 hand);
    return was_stale ? GS_MOUNT_DETACHED : GS_MOUNT_GONE;
}

int gs_mount_fstab_grants(char letter)
{
    gs_mount_fstab_t row;

    if (!gs_mount_fstab_find(letter, &row))
        return 0;
    return row.mountable_by_user && gs_mount_pair_ok(letter, row.point);
}

/* Where the new table is written before it is put in place. It sits in
 * this program's own directory, which only the person running can write,
 * so nothing else can swap it between the writing and the copying. */
static int spill(const char *text, size_t len, char *path, size_t cap)
{
    char dir[512];
    int fd;
    ssize_t put;

    if (gs_paths_data_dir(dir, sizeof dir) != GS_OK)
        return GS_ERR;
    if (snprintf(path, cap, "%s/fstab.new", dir) < 0)
        return GS_ERR;

    /* Made fresh every time rather than opened, so a file somebody left
     * there is never written into and never copied over the table. */
    (void)unlink(path);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0)
        return GS_ERR;
    put = write(fd, text, len);
    close(fd);
    if (put < 0 || (size_t)put != len) {
        (void)unlink(path);
        return GS_ERR;
    }
    return GS_OK;
}

int gs_mount_fstab_grant(char letter, const char *password,
                         size_t password_len, char *why, size_t cap)
{
    const char *cp_bin = FIRST_REAL(cp_paths);
    const char *sudo = FIRST_REAL(sudo_paths);
    char point[16];
    char table[16384];
    char path[600];
    char said[512];
    char clean[512];
    const char *argv[8];
    size_t len = 0;
    int code = -1;
    int n = 0;
    int rc;

    if (why != NULL && cap > 0)
        why[0] = '\0';
    if (!gs_mount_letter_ok(letter))
        return GS_ERR_ARG;
    if (gs_mount_point_for(letter, point, sizeof point) != GS_OK)
        return GS_ERR_ARG;
    if (gs_mount_point_refused(point)) {
        if (why != NULL)
            snprintf(why, cap, "%s belongs to the system and is left alone",
                     point);
        return GS_ERR_ARG;
    }
    if (cp_bin == NULL || (sudo == NULL && geteuid() != 0)) {
        if (why != NULL)
            snprintf(why, cap, "this machine has no way to write %s",
                     GS_MOUNT_FSTAB);
        return GS_ERR;
    }

    if (gs_mount_fstab_grants(letter)) {
        if (why != NULL)
            snprintf(why, cap, "%s already lets you mount drive %c:",
                     GS_MOUNT_FSTAB, letter);
        return GS_OK;
    }

    if (gs_mount_fstab_rewrite(letter, point, (long)getuid(), (long)getgid(),
                               table, sizeof table, &len) != GS_OK) {
        if (why != NULL)
            snprintf(why, cap, "%s could not be worked out", GS_MOUNT_FSTAB);
        return GS_ERR;
    }
    if (spill(table, len, path, sizeof path) != GS_OK) {
        if (why != NULL)
            snprintf(why, cap, "the new table could not be written down");
        return GS_ERR;
    }

    /* The old table is kept under a name of its own before the new one
     * goes in, so a line that turns out to be wrong can be put back by
     * hand. */
    if (geteuid() != 0) {
        argv[n++] = sudo;
        argv[n++] = "-S";           /* the password arrives on the input */
        argv[n++] = "-p";
        argv[n++] = "";             /* and sudo asks for nothing on screen */
    }
    argv[n++] = cp_bin;
    argv[n++] = "--backup=simple";
    argv[n++] = "--suffix=" GS_MOUNT_FSTAB_BACKUP;
    argv[n] = NULL;

    /* Two more arguments than the array above names would overrun it, so
     * the source and the target go in a second pass with the room
     * checked. */
    {
        const char *full[10];
        int i;

        for (i = 0; i < n; i++)
            full[i] = argv[i];
        full[n] = path;
        full[n + 1] = GS_MOUNT_FSTAB;
        full[n + 2] = NULL;

        /* The password goes straight down the pipe into sudo. It is
         * never an argument, because every running program's arguments
         * are readable through /proc, and it is never in the
         * surroundings for the same reason. Nothing here copies it. */
        rc = gs_proc_run_input(full[0], full, password, password_len,
                               said, sizeof said, &code, GS_MOUNT_SECONDS);
    }
    (void)unlink(path);

    if (rc != GS_OK) {
        if (why != NULL)
            snprintf(why, cap, "writing %s did not finish", GS_MOUNT_FSTAB);
        return GS_ERR;
    }

    /* What the tool printed decides nothing. The table is read again,
     * and whether it now grants the right is the whole answer. */
    if (gs_mount_fstab_grants(letter)) {
        if (why != NULL)
            snprintf(why, cap,
                     "%s now lets you mount drive %c: with no password. The "
                     "table it replaced is at %s%s", GS_MOUNT_FSTAB, letter,
                     GS_MOUNT_FSTAB, GS_MOUNT_FSTAB_BACKUP);
        return GS_OK;
    }

    gs_mount_plain(said, clean, sizeof clean);
    if (why != NULL) {
        if (code != 0 && strstr(clean, "password") != NULL)
            snprintf(why, cap, "that password was not accepted");
        else
            snprintf(why, cap, "%s was not changed: %s", GS_MOUNT_FSTAB,
                     clean[0] != '\0' ? clean : "no reason given");
    }
    return GS_ERR;
}
