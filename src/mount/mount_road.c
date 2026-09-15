/* mount_road.c
 *
 * The road to root, and the argument list that carries a command down it.
 *
 * Putting a Windows drive back needs the administrator, and nothing here
 * ever asks a person for a password. Windows grants whoever owns a
 * distribution the right to start a command inside it as root with
 * nothing asked, because the owner of the machine already holds every
 * file of it, so a Linux inside Windows reaches root through wsl.exe.
 * Elsewhere sudo is tried in the one form that refuses rather than asks.
 * A machine with neither road gets warnings and carries on.
 *
 * Every command goes through gs_proc_run with its arguments kept apart
 * from one another, and no shell reads any of them on the way.
 *
 * Kept apart from mount_repair.c so neither file grows past what fits in
 * a head.
 */
#include "mount.h"
#include "mount_internal.h"

#include "gravestone.h"
#include "log.h"
#include "proc.h"

#include <string.h>
#include <unistd.h>

/* Absolute paths, because a name looked up on the search path is looked
 * up in whatever directories that path names. Under WSL most of those
 * sit on the Windows drive, where another program can write, so a name
 * would be an invitation. */
static const char *const sudo_paths[] = { "/usr/bin/sudo", "/bin/sudo" };
static const char *const id_paths[]   = { "/usr/bin/id", "/bin/id" };

/* Where Windows keeps the program that starts a command inside this
 * Linux. The one in System32 is a small stub that finds whichever real
 * copy is installed. Both sit on the Windows drive, which is read only
 * to this user, and that is checked before either is trusted. */
static const char *const wsl_paths[] = {
    "/mnt/c/Windows/System32/wsl.exe",
    "/mnt/c/WINDOWS/system32/wsl.exe",
    "/mnt/c/Program Files/WSL/wsl.exe"
};

#define FIRST_REAL(a) \
    gs_mount_first_real((a), (int)(sizeof (a) / sizeof (a)[0]))

int gs_mount_root_argv(gs_mount_road_t road, const char *helper,
                       const char *distro, const char *program,
                       const char *const *tail, int tail_len,
                       const char **argv, int cap)
{
    int n = 0;
    int i;

    if (argv == NULL || cap < 2 || program == NULL || program[0] != '/')
        return GS_ERR_ARG;
    if (tail_len < 0 || (tail_len > 0 && tail == NULL))
        return GS_ERR_ARG;

    switch (road) {
    case GS_MOUNT_ROAD_SELF:
        break;
    case GS_MOUNT_ROAD_SUDO:
        if (helper == NULL || helper[0] != '/')
            return GS_ERR_ARG;
        if (n + 2 >= cap)
            return GS_ERR_ARG;
        argv[n++] = helper;
        argv[n++] = "-n";
        break;
    case GS_MOUNT_ROAD_WSL:
        /* The distribution name reaches wsl.exe as an argument of its
         * own, and it has already been refused unless it holds only
         * letters, digits, dot, dash and underscore. -e runs the program
         * straight, with no shell reading the arguments on the way. */
        if (helper == NULL || helper[0] != '/' || distro == NULL ||
            distro[0] == '\0')
            return GS_ERR_ARG;
        if (n + 6 >= cap)
            return GS_ERR_ARG;
        argv[n++] = helper;
        argv[n++] = "-d";
        argv[n++] = distro;
        argv[n++] = "-u";
        argv[n++] = "root";
        argv[n++] = "-e";
        break;
    default:
        return GS_ERR_ARG;
    }

    if (n + 1 + tail_len >= cap)
        return GS_ERR_ARG;
    argv[n++] = program;
    for (i = 0; i < tail_len; i++)
        argv[n++] = tail[i];
    argv[n] = NULL;
    return n;
}

/* The road found on this machine, looked for until an answer is definite
 * and remembered for the rest of the run. A probe that timed out or
 * could not start is no answer at all, so it is looked for again on the
 * next pass rather than written down as no road. */
static gs_mount_road_t road_found = GS_MOUNT_ROAD_NONE;
static int road_looked;
static char road_distro[72];

int gs_mount_line_is_nought(const char *text, size_t cap)
{
    /* Both channels of the program arrive down one pipe, and wsl.exe
     * writes warnings of its own on the error channel before the program
     * it started says anything, so the nought is looked for among the
     * lines rather than expected alone. Those warnings arrive as two
     * bytes a character with a nought byte after each one, since wsl.exe
     * writes the wide form of its text when its output is a pipe, so
     * nought bytes are stepped over rather than read as the end. */
    size_t i;
    size_t len = 0;
    char line[8];

    if (text == NULL)
        return 0;
    for (i = 0; i < cap; i++) {
        char c = text[i];

        if (c == '\0')
            continue;
        if (c == '\n') {
            if (len == 1 && line[0] == '0')
                return 1;
            len = 0;
            continue;
        }
        if (c == '\r')
            continue;
        if (len < sizeof line)
            line[len] = c;
        len++;
    }
    return len == 1 && line[0] == '0';
}

/* Runs id as the administrator down one road and reads back a nought.
 * Returns 1 for root, 0 for a definite refusal, and minus one when the
 * program never finished, which decides nothing. What the road printed
 * decides nothing else, since sudo is translated and a machine in
 * another language says the same thing differently. */
static int road_answers(gs_mount_road_t road, const char *helper)
{
    const char *id = FIRST_REAL(id_paths);
    const char *tail[1];
    const char *argv[16];
    char answer[1024];
    int code = -1;

    if (id == NULL)
        return 0;
    tail[0] = "-u";
    if (gs_mount_root_argv(road, helper, road_distro, id, tail, 1, argv,
                           16) < 0)
        return 0;
    memset(answer, 0, sizeof answer);
    if (gs_proc_run(argv[0], argv, answer, sizeof answer, &code,
                    GS_MOUNT_SECONDS) != GS_OK) {
        gs_log_debug("mount: the road probe through %s never finished",
                     helper);
        return -1;
    }
    return code == 0 && gs_mount_line_is_nought(answer, sizeof answer);
}

gs_mount_road_t gs_mount_root_road(const char **helper_out)
{
    static const char *helper;
    int undecided = 0;

    if (helper_out != NULL)
        *helper_out = helper;
    if (road_looked)
        return road_found;

    if (geteuid() == 0) {
        road_found = GS_MOUNT_ROAD_SELF;
        road_looked = 1;
        return road_found;
    }

    /* The program is trusted only from a place this user cannot write
     * to, since a program anybody could replace is not the one wanted. */
    if (gs_mount_under_wsl() &&
        gs_mount_distro(road_distro, sizeof road_distro) == GS_OK) {
        const char *wsl = FIRST_REAL(wsl_paths);

        if (wsl != NULL && access(wsl, W_OK) != 0) {
            int said = road_answers(GS_MOUNT_ROAD_WSL, wsl);

            if (said > 0) {
                helper = wsl;
                road_found = GS_MOUNT_ROAD_WSL;
                road_looked = 1;
                if (helper_out != NULL)
                    *helper_out = helper;
                gs_log_info("mount: root reached through %s with nothing "
                            "asked", wsl);
                return road_found;
            }
            if (said < 0)
                undecided = 1;
        }
    }

    /* Otherwise sudo, and only the form that refuses rather than asks. */
    {
        const char *sudo = FIRST_REAL(sudo_paths);

        if (sudo != NULL) {
            int said = road_answers(GS_MOUNT_ROAD_SUDO, sudo);

            if (said > 0) {
                helper = sudo;
                road_found = GS_MOUNT_ROAD_SUDO;
                road_looked = 1;
                if (helper_out != NULL)
                    *helper_out = helper;
                return road_found;
            }
            if (said < 0)
                undecided = 1;
        }
    }
    road_found = GS_MOUNT_ROAD_NONE;
    road_looked = !undecided;
    return road_found;
}

int gs_mount_root_free(void)
{
    return gs_mount_root_road(NULL) != GS_MOUNT_ROAD_NONE;
}

int gs_mount_root_settled(void)
{
    return road_looked;
}

int gs_mount_as_root(const char *program, const char *const *tail,
                     int tail_len, char *out, size_t cap, int *code)
{
    const char *helper = NULL;
    gs_mount_road_t road = gs_mount_root_road(&helper);
    const char *argv[16];

    if (program == NULL || tail_len < 0 || tail_len > 6)
        return GS_ERR_ARG;
    if (road == GS_MOUNT_ROAD_NONE)
        return GS_ERR;
    if (gs_mount_root_argv(road, helper, road_distro, program, tail,
                           tail_len, argv, 16) < 0)
        return GS_ERR_ARG;
    return gs_proc_run(argv[0], argv, out, cap, code, GS_MOUNT_SECONDS);
}
