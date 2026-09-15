/* mount_linux.c
 *
 * The half that touches the machine. Reading the kernel mount table,
 * asking a filesystem whether it still answers, finding out who is
 * holding one open, and running the four commands that put a drive back.
 *
 * Every command runs through gs_proc_run, which starts a program
 * directly with its arguments kept apart. The other two ways this
 * program runs things hand their text to /bin/sh, and a shell reads a
 * semicolon, a backtick and an ampersand as instructions, so a value
 * that came out of a database would be able to carry a second command
 * inside it. A command running as the administrator is the one place
 * where that must be impossible rather than unlikely.
 *
 * No unmount here carries -l, --lazy, -f or --force. Those flags detach
 * a filesystem while programs are still writing into it, and both kinds
 * of Windows drive carry the loose 9p cache, so a forced detach throws
 * away everything written since the cache was last emptied. A drive
 * somebody is holding open is reported as busy and left where it is.
 */
#include "mount.h"
#include "mount_internal.h"

#include "gravestone.h"
#include "log.h"
#include "paths.h"
#include "proc.h"
#include "str.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

/* How long any one of the four commands may take. A Windows drive that
 * has gone to sleep answers slowly, and a drive that has gone away does
 * not answer at all. */
#define MOUNT_SECONDS 20

/* Absolute paths, because a name looked up on the search path is looked
 * up in whatever directories that path names. Under WSL most of those
 * sit on the Windows drive, where another program can write, so a name
 * would be an invitation. The tools move about between distributions, so
 * each is tried in the usual places and the first one that is really
 * there is used. */
#define FIRST_REAL(a) \
    gs_mount_first_real((a), (int)(sizeof (a) / sizeof (a)[0]))

const char *gs_mount_first_real(const char *const *paths, int count)
{
    struct stat where;
    int i;

    if (paths == NULL)
        return NULL;
    for (i = 0; i < count; i++)
        if (stat(paths[i], &where) == 0 && S_ISREG(where.st_mode))
            return paths[i];
    return NULL;
}

int gs_mount_kernel_is_windows(void)
{
    FILE *f = fopen("/proc/version", "r");
    char line[512];
    int found = 0;

    if (f == NULL)
        return 0;
    if (fgets(line, sizeof line, f) != NULL) {
        int i;

        for (i = 0; line[i] != '\0'; i++)
            line[i] = (char)(line[i] >= 'A' && line[i] <= 'Z'
                             ? line[i] - 'A' + 'a' : line[i]);
        found = strstr(line, "microsoft") != NULL;
    }
    fclose(f);
    return found;
}

int gs_mount_under_wsl(void)
{
    gs_mount_drive_t drives[GS_MOUNT_MAX_DRIVES];

    /* Two signals have to agree. A kernel naming Microsoft could be a
     * copied string, and a Windows drive in the table could be a share
     * named to look like one, so neither alone points the unmount
     * command at anything. */
    if (!gs_mount_kernel_is_windows())
        return 0;
    return gs_mount_table_read(drives, GS_MOUNT_MAX_DRIVES) > 0;
}

int gs_mount_table_read(gs_mount_drive_t *out, int max)
{
    FILE *f;
    char line[2048];
    int count = 0;

    if (out == NULL || max <= 0)
        return GS_ERR_ARG;
    memset(out, 0, sizeof *out * (size_t)max);

    f = fopen("/proc/mounts", "r");
    if (f == NULL)
        return 0;
    while (count < max && fgets(line, sizeof line, f) != NULL)
        if (gs_mount_parse_line(line, &out[count]))
            count++;
    fclose(f);
    return count;
}

int gs_mount_is_mounted(const char *point)
{
    struct stat here;
    struct stat above;
    char parent[GS_MOUNT_POINT_MAX];
    size_t len;

    if (point == NULL || point[0] == '\0')
        return 0;
    len = strlen(point);
    if (len == 0 || len + 1 > sizeof parent)
        return 0;
    memcpy(parent, point, len + 1);
    while (len > 1 && parent[len - 1] != '/')
        len--;
    if (len > 1)
        len--;                       /* drop the oblique, unless it is the root */
    parent[len] = '\0';
    if (parent[0] == '\0')
        parent[0] = '/', parent[1] = '\0';

    if (stat(point, &here) != 0)
        return 0;
    if (stat(parent, &above) != 0)
        return 0;
    /* Every filesystem the kernel has mounted carries its own device
     * number, so a path whose number differs from the directory above it
     * is a mount rather than an ordinary directory sitting on the same
     * filesystem. */
    return here.st_dev != above.st_dev;
}

int gs_mount_answers(const char *point)
{
    struct statvfs space;

    if (point == NULL || point[0] == '\0')
        return 0;
    /* An empty directory nobody has mounted anything on answers this
     * call perfectly well, since the answer describes whatever
     * filesystem the path happens to sit on. Asking whether anything is
     * mounted there at all has to come first, or /mnt/d with nothing
     * behind it reports the size of the system disk and reads as a drive
     * that is working. */
    if (!gs_mount_is_mounted(point))
        return 0;
    /* A mount left behind by a drive that went away stays in the table
     * and fails here with ENODEV, which is the difference between a
     * drive that needs putting back and one that is working. */
    return statvfs(point, &space) == 0;
}

int gs_mount_windows_letters(char *out, size_t cap)
{
    static const char *const fsutil_paths[] = {
        "/mnt/c/Windows/System32/fsutil.exe",
        "/mnt/c/windows/System32/fsutil.exe",
        "/mnt/c/Windows/system32/fsutil.exe"
    };
    const char *fsutil = FIRST_REAL(fsutil_paths);
    const char *argv[4];
    char said[512];
    int code = -1;

    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (fsutil == NULL)
        return GS_ERR;

    /* Windows names the drives it has attached, which is the only way to
     * learn about one that was plugged in after this Linux started. The
     * mount table cannot say, because a drive nobody has mounted is not
     * in it. */
    argv[0] = fsutil;
    argv[1] = "fsinfo";
    argv[2] = "drives";
    argv[3] = NULL;
    if (gs_proc_run(fsutil, argv, said, sizeof said, &code,
                    MOUNT_SECONDS) != GS_OK || code != 0)
        return GS_ERR;

    return gs_mount_read_drive_list(said, out, cap);
}

int gs_mount_read_drive_list(const char *text, char *out, size_t cap)
{
    size_t filled = 0;
    int i;

    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (text == NULL)
        return GS_ERR_ARG;

    /* The answer reads "Drives: C:\ D:\", so a drive is a letter, a colon
     * and a backslash, standing on its own. Reading any letter in front
     * of a colon would take the s out of the word Drives and report a
     * drive S that this machine has never had. */
    for (i = 0; text[i] != '\0' && filled + 1 < cap; i++) {
        char c = text[i];

        if (i > 0 && text[i - 1] != ' ' && text[i - 1] != '\t' &&
            text[i - 1] != '\n' && text[i - 1] != '\r')
            continue;
        if (text[i + 1] != ':' || text[i + 2] != '\\')
            continue;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        if (!gs_mount_letter_ok(c))
            continue;
        out[filled++] = c;
        out[filled] = '\0';
    }
    return GS_OK;
}


int gs_mount_dir_at(const char *point, int *exists_out)
{
    struct stat where;

    if (exists_out != NULL)
        *exists_out = 0;
    if (point == NULL || point[0] == '\0')
        return 0;
    /* lstat rather than stat, so a link pointing somewhere else is seen
     * as a link rather than followed to whatever it names. */
    if (lstat(point, &where) != 0)
        return 0;
    if (exists_out != NULL)
        *exists_out = 1;
    return S_ISDIR(where.st_mode);
}

int gs_mount_dir_has_content(const char *point)
{
    DIR *dir;
    struct dirent *entry;
    int found = 0;

    if (point == NULL || point[0] == '\0')
        return 0;
    /* A directory that refuses to be read is reported as full rather
     * than empty, since a mount over it would hide whatever is there. A
     * directory that is not there at all holds nothing. */
    dir = opendir(point);
    if (dir == NULL)
        return errno == EACCES || errno == EPERM;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        found = 1;
        break;
    }
    closedir(dir);
    return found;
}

/* True when the link at path points at something under the mount point.
 * Reading the link rather than following it matters, since following one
 * into a dead mount blocks. */
static int link_under(const char *path, const char *point, size_t point_len)
{
    char target[512];
    ssize_t len = readlink(path, target, sizeof target - 1);

    if (len <= 0)
        return 0;
    target[len] = '\0';
    if (strncmp(target, point, point_len) != 0)
        return 0;
    return target[point_len] == '/' || target[point_len] == '\0';
}

int gs_mount_holders(const char *point, char *first, size_t cap)
{
    DIR *procs;
    struct dirent *entry;
    size_t point_len;
    int holders = 0;

    if (first != NULL && cap > 0)
        first[0] = '\0';
    if (point == NULL || point[0] == '\0')
        return 0;
    point_len = strlen(point);

    procs = opendir("/proc");
    if (procs == NULL)
        return 0;

    while ((entry = readdir(procs)) != NULL) {
        char base[24];
        char path[40];
        DIR *handles;
        struct dirent *handle;
        int hit = 0;

        /* Only the numbered directories are processes, and a process
         * number never runs past ten digits, so anything longer is one
         * of the named entries beside them. */
        {
            size_t n = strlen(entry->d_name);
            size_t k;

            if (n == 0 || n > 10)
                continue;
            for (k = 0; k < n; k++)
                if (entry->d_name[k] < '0' || entry->d_name[k] > '9')
                    break;
            if (k < n)
                continue;
            snprintf(base, sizeof base, "/proc/%.10s", entry->d_name);
        }

        /* Where the program is working, and the program file itself. */
        snprintf(path, sizeof path, "%s/cwd", base);
        if (link_under(path, point, point_len))
            hit = 1;
        if (!hit) {
            snprintf(path, sizeof path, "%s/exe", base);
            if (link_under(path, point, point_len))
                hit = 1;
        }

        /* Every file it has open. A model file is mapped into memory
         * rather than read, and a mapping shows up here as an open
         * handle, so this is what catches an inference server holding a
         * model on the drive. */
        if (!hit) {
            snprintf(path, sizeof path, "%s/fd", base);
            handles = opendir(path);
            if (handles != NULL) {
                while ((handle = readdir(handles)) != NULL) {
                    char one[64];

                    if (handle->d_name[0] == '.')
                        continue;
                    snprintf(one, sizeof one, "%s/%.10s", path, handle->d_name);
                    if (link_under(one, point, point_len)) {
                        hit = 1;
                        break;
                    }
                }
                closedir(handles);
            }
        }

        if (!hit)
            continue;
        holders++;
        if (first != NULL && cap > 0 && first[0] == '\0') {
            char name[96];
            char comm[160];
            FILE *f;

            snprintf(comm, sizeof comm, "%s/comm", base);
            f = fopen(comm, "r");
            name[0] = '\0';
            if (f != NULL) {
                if (fgets(name, sizeof name, f) == NULL)
                    name[0] = '\0';
                fclose(f);
            }
            {
                size_t n = strlen(name);

                while (n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r'))
                    name[--n] = '\0';
            }
            snprintf(comm, sizeof comm, "%s (pid %.10s)",
                     name[0] != '\0' ? name : "a program", entry->d_name);
            gs_mount_plain(comm, first, cap);
        }
    }
    closedir(procs);
    return holders;
}

/* Nothing in this program locks anything else, so the lock lives beside
 * the database rather than in a place another program might claim. */
static int lock_fd = -1;

int gs_mount_lock(void)
{
    char dir[512];
    char path[600];

    if (lock_fd >= 0)
        return GS_OK;
    if (gs_paths_data_dir(dir, sizeof dir) != GS_OK)
        return GS_ERR;
    snprintf(path, sizeof path, "%s/mount.lock", dir);

    lock_fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd < 0)
        return GS_ERR;
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        close(lock_fd);
        lock_fd = -1;
        return GS_ERR;
    }
    return GS_OK;
}

void gs_mount_unlock(void)
{
    if (lock_fd < 0)
        return;
    (void)flock(lock_fd, LOCK_UN);
    close(lock_fd);
    lock_fd = -1;
}
