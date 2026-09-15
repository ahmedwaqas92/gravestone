/* ghost.c
 *
 * The sweep starts a Windows program, so it costs about half a second.
 * That is why the shutdown path uses the detached form, where a child
 * carries the work and the program exits without waiting.
 */
#include "ghost.h"
#include "ghost_internal.h"
#include "gravestone.h"
#include "log.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* WSL mounts its own directory here whenever it is carrying windows
 * across, and a machine with no Windows side does not have it. The check
 * costs a fraction of a microsecond, so it goes first. */
#define WSLG_MARK "/mnt/wslg"

int gs_ghost_available(void)
{
    struct stat where;

    return stat(WSLG_MARK, &where) == 0 && S_ISDIR(where.st_mode);
}

int gs_ghost_pattern(const char *title, char *out, size_t cap)
{
    char distro[80];
    int written;

    if (title == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (strchr(title, '\'') != NULL || strchr(title, '*') != NULL)
        return GS_ERR_ARG;

    if (gs_str_wsl_distro(distro, sizeof distro) != GS_OK)
        written = snprintf(out, cap, "%s*", title);
    else
        written = snprintf(out, cap, "%s*(%s)*", title, distro);

    if (written < 0 || (size_t)written >= cap) {
        out[0] = '\0';
        return GS_ERR_ARG;
    }
    return GS_OK;
}

/* The name this program was started as, which is what another copy of it
 * would be running too. Reading the link rather than argv means a copy
 * started from a different directory is still recognised. */
void gs_ghost_drop_deleted_note(char *path)
{
    static const char note[] = " (deleted)";
    size_t len;
    size_t tail = sizeof note - 1;

    if (path == NULL)
        return;
    len = strlen(path);
    if (len >= tail && strcmp(path + len - tail, note) == 0)
        path[len - tail] = '\0';
}

static int read_exe(const char *link, char *out, size_t cap)
{
    ssize_t got = readlink(link, out, cap - 1);

    if (got <= 0)
        return GS_ERR;
    out[got] = '\0';
    gs_ghost_drop_deleted_note(out);
    return GS_OK;
}

static int own_path(char *out, size_t cap)
{
    return read_exe("/proc/self/exe", out, cap);
}

int gs_ghost_others_running(void)
{
    char mine[512];
    char theirs[512];
    DIR *proc;
    struct dirent *entry;
    pid_t self = getpid();
    int found = 0;

    if (own_path(mine, sizeof mine) != GS_OK)
        return 0;
    proc = opendir("/proc");
    if (proc == NULL)
        return 0;

    while ((entry = readdir(proc)) != NULL) {
        char link[64];
        long pid;
        char *end;

        pid = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || pid <= 0)
            continue;
        if ((pid_t)pid == self)
            continue;

        snprintf(link, sizeof link, "/proc/%ld/exe", pid);
        if (read_exe(link, theirs, sizeof theirs) != GS_OK)
            continue;               /* a process we may not look inside */
        if (strcmp(theirs, mine) == 0) {
            found = 1;
            break;
        }
    }
    closedir(proc);
    return found;
}

/* Builds the whole command line. The script travels as one base64 word,
 * so nothing inside it is ever seen by a shell. */
static int build_command(const char *title, char *out, size_t cap)
{
    char pattern[160];
    char script[2048];
    char encoded[4096];
    int written;

    if (gs_ghost_pattern(title, pattern, sizeof pattern) != GS_OK)
        return GS_ERR_ARG;
    if (gs_ghost_script(pattern, script, sizeof script) != GS_OK)
        return GS_ERR_ARG;
    if (gs_str_windows_command(script, encoded, sizeof encoded) < 0)
        return GS_ERR_ARG;

    written = snprintf(out, cap,
                       "powershell.exe -NoProfile -NonInteractive "
                       "-EncodedCommand %s 2>/dev/null", encoded);
    if (written < 0 || (size_t)written >= cap)
        return GS_ERR_ARG;
    return GS_OK;
}

int gs_ghost_sweep(const char *title, int *hidden, int *already)
{
    char command[8192];
    char line[256];
    FILE *pipe_in;
    int got_hidden = 0;
    int got_already = 0;

    if (hidden != NULL)
        *hidden = 0;
    if (already != NULL)
        *already = 0;
    if (title == NULL)
        return GS_ERR_ARG;
    if (!gs_ghost_available())
        return GS_OK;               /* nothing mirrors windows here */
    if (gs_ghost_others_running()) {
        gs_log_debug("ghost: another copy is running, leaving windows alone");
        return GS_OK;
    }
    if (build_command(title, command, sizeof command) != GS_OK)
        return GS_ERR_ARG;

    pipe_in = popen(command, "r");
    if (pipe_in == NULL)
        return GS_ERR;

    while (fgets(line, sizeof line, pipe_in) != NULL) {
        int a = 0, b = 0;

        if (sscanf(line, "hidden %d already %d", &a, &b) == 2) {
            got_hidden = a;
            got_already = b;
        }
    }
    if (pclose(pipe_in) != 0)
        return GS_ERR;

    if (hidden != NULL)
        *hidden = got_hidden;
    if (already != NULL)
        *already = got_already;
    if (got_hidden > 0)
        gs_log_info("ghost: hid %d leftover window%s", got_hidden,
                    got_hidden == 1 ? "" : "s");
    return GS_OK;
}

void gs_ghost_detach(void)
{
    static const int stop[] = { SIGINT, SIGTERM, SIGHUP };
    struct sigaction plain;
    size_t i;

    setsid();
    memset(&plain, 0, sizeof plain);
    plain.sa_handler = SIG_DFL;
    sigemptyset(&plain.sa_mask);
    for (i = 0; i < sizeof stop / sizeof stop[0]; i++)
        sigaction(stop[i], &plain, NULL);
}

/* Sleeps the whole time asked for. A plain sleep gives up the moment any
 * signal arrives and hands back the time it did not use, which would cut
 * the wait to nothing. */
static void wait_out(int seconds)
{
    struct timespec left;

    left.tv_sec = seconds;
    left.tv_nsec = 0;
    while (nanosleep(&left, &left) != 0 && errno == EINTR)
        ;
}

int gs_ghost_sweep_detached(const char *title)
{
    pid_t first;

    if (title == NULL)
        return GS_ERR_ARG;
    if (!gs_ghost_available())
        return GS_OK;

    /* Two forks, so the working child is handed to the init process and
     * nothing is left for this program to collect. The middle child exits
     * at once and is collected here, which keeps the process table
     * tidy. */
    first = fork();
    if (first < 0)
        return GS_ERR;
    if (first == 0) {
        pid_t second = fork();

        if (second < 0)
            _exit(1);
        if (second == 0) {
            char command[8192];

            gs_ghost_detach();

            /* The parent is on its way out and its window is already
             * gone, so waiting a moment lets the compositor take down the
             * mirror by itself before anything is hidden by hand. The
             * wait also outlasts the parent, so the check below sees a
             * program that has really gone. */
            wait_out(2);
            if (gs_ghost_others_running())
                _exit(0);           /* someone else still owns a window */

            if (build_command(title, command, sizeof command) == GS_OK) {
                int quiet = open("/dev/null", O_WRONLY);

                if (quiet >= 0) {
                    dup2(quiet, STDOUT_FILENO);
                    dup2(quiet, STDERR_FILENO);
                    if (quiet > STDERR_FILENO)
                        close(quiet);
                }
                execl("/bin/sh", "sh", "-c", command, (char *)NULL);
            }
            _exit(0);
        }
        _exit(0);
    }
    while (waitpid(first, NULL, 0) < 0 && errno == EINTR)
        ;
    return GS_OK;
}

#else /* _WIN32 */

/* A program running on Windows draws on the desktop directly, so no
 * mirror of it exists and there is nothing to sweep. */

int gs_ghost_available(void)
{
    return 0;
}

int gs_ghost_pattern(const char *title, char *out, size_t cap)
{
    if (title == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    return GS_ERR_ARG;
}

int gs_ghost_sweep(const char *title, int *hidden, int *already)
{
    (void)title;
    if (hidden != NULL)
        *hidden = 0;
    if (already != NULL)
        *already = 0;
    return GS_OK;
}

int gs_ghost_sweep_detached(const char *title)
{
    (void)title;
    return GS_OK;
}

int gs_ghost_others_running(void)
{
    return 0;
}

#endif
