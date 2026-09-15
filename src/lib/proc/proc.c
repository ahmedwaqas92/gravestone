#include "proc.h"
#include "gravestone.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

/* Windows names its bin for discarded output differently, spells the
 * pipe calls with a leading underscore, and has no `command -v`. Naming
 * the differences once here keeps every caller writing one command.
 *
 * The shell each platform hands popen is the reason. A Windows shell
 * reads `2>/dev/null` as a path and fails on the missing directory, so
 * every command carrying it comes back empty. */
#ifdef _WIN32
#define GS_PROC_QUIET  "2>NUL"
#define GS_PROC_LOUD   "2>&1"
#define GS_PROC_WHICH  "where"
#define gs_popen  _popen
#define gs_pclose _pclose
#else
#define GS_PROC_QUIET  "2>/dev/null"
#define GS_PROC_LOUD   "2>&1"
#define GS_PROC_WHICH  "command -v"
#define gs_popen  popen
#define gs_pclose pclose
#endif

/* Runs the command and keeps what it printed. Complaints go to the bin
 * or into the answer, depending on whether the caller has to explain a
 * failure to somebody. */
static int capture(const char *command, char *out, size_t cap, int keep)
{
    FILE *pipe;
    size_t filled = 0;
    int status;

    if (command == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';

    {
        char line[512];
        char full[600];

        snprintf(full, sizeof full, "%s %s", command,
                 keep ? GS_PROC_LOUD : GS_PROC_QUIET);
        pipe = gs_popen(full, "r");
        if (pipe == NULL) {
            gs_log_debug("proc: could not start %s", command);
            return GS_ERR_IO;
        }

        while (fgets(line, sizeof line, pipe) != NULL) {
            size_t len = strlen(line);
            size_t room = cap - 1 - filled;

            if (room == 0)
                break;
            if (len > room)
                len = room;
            memcpy(out + filled, line, len);
            filled += len;
            out[filled] = '\0';
        }
    }

    status = gs_pclose(pipe);
    if (status != 0) {
        gs_log_debug("proc: %s exited with %d", command, status);
        /* A caller asking for the reason keeps what the tool said, since
         * that text is the whole point of asking. */
        if (!keep)
            out[0] = '\0';
        return GS_ERR;
    }

    /* A trailing newline is never wanted by a caller storing one value. */
    while (filled > 0 && (out[filled - 1] == '\n' || out[filled - 1] == '\r'))
        out[--filled] = '\0';

    return filled > 0 ? GS_OK : GS_ERR;
}

int gs_proc_capture(const char *command, char *out, size_t cap)
{
    /* Standard error is thrown away so a tool complaining about a missing
     * driver does not end up presented as a graphics card. */
    return capture(command, out, cap, 0);
}

int gs_proc_reason(const char *command, char *out, size_t cap)
{
    /* A tool that refuses says why on the error channel, and a caller
     * about to tell a person that something would not work needs those
     * words rather than a bare failure. */
    return capture(command, out, cap, 1);
}

int gs_proc_exists(const char *name)
{
    char command[256];
    char answer[256];

    if (name == NULL || name[0] == '\0')
        return 0;
    /* Anything other than a plain name could carry shell syntax. */
    if (strpbrk(name, " \t;&|<>$`\\\"'\n") != NULL)
        return 0;

    snprintf(command, sizeof command, "%s %s", GS_PROC_WHICH, name);
    return gs_proc_capture(command, answer, sizeof answer) == GS_OK;
}

#ifdef _WIN32

int gs_proc_run(const char *program, const char *const *argv,
                char *out, size_t cap, int *exit_code, int seconds)
{
    /* Windows starts a process through a different call altogether, and
     * nothing on that side needs this yet. Writing a wrong version now
     * would be worse than refusing. */
    (void)program;
    (void)argv;
    (void)seconds;
    if (out != NULL && cap > 0)
        out[0] = '\0';
    if (exit_code != NULL)
        *exit_code = -1;
    return GS_ERR;
}

int gs_proc_run_input(const char *program, const char *const *argv,
                      const char *input, size_t input_len,
                      char *out, size_t cap, int *exit_code, int seconds)
{
    (void)input;
    (void)input_len;
    return gs_proc_run(program, argv, out, cap, exit_code, seconds);
}

int gs_proc_spawn(const char *program, const char *const *argv,
                  const char *log_path)
{
    (void)program;
    (void)argv;
    (void)log_path;
    return GS_ERR;
}


#else

/* The search path the child is given. A child inheriting the one the
 * caller has would look up its program in whatever directories that path
 * names, and under WSL most of those sit on the Windows drive where
 * another program can write. The absolute path handed in is what runs, so
 * this only covers anything the program itself starts. */
#define GS_PROC_SAFE_PATH "PATH=/usr/sbin:/usr/bin:/sbin:/bin"

int gs_proc_run(const char *program, const char *const *argv,
                char *out, size_t cap, int *exit_code, int seconds)
{
    return gs_proc_run_input(program, argv, NULL, 0, out, cap, exit_code,
                             seconds);
}

int gs_proc_run_input(const char *program, const char *const *argv,
                      const char *input, size_t input_len,
                      char *out, size_t cap, int *exit_code, int seconds)
{
    char *const envp[] = { (char *)GS_PROC_SAFE_PATH, NULL };
    int channel[2];
    int feed[2];
    size_t written = 0;
    pid_t child;
    size_t filled = 0;
    long long deadline;
    int status = 0;
    int timed_out = 0;
    int rc = GS_OK;

    if (out != NULL && cap > 0)
        out[0] = '\0';
    if (exit_code != NULL)
        *exit_code = -1;

    if (program == NULL || argv == NULL || argv[0] == NULL || seconds <= 0)
        return GS_ERR_ARG;
    /* A relative name would be looked up, and looking anything up is what
     * this call exists to avoid. */
    if (program[0] != '/')
        return GS_ERR_ARG;
    if (out == NULL || cap == 0)
        return GS_ERR_ARG;

    if (input != NULL && input_len == 0)
        input = NULL;

    if (pipe(channel) != 0) {
        gs_log_debug("proc: no pipe for %s: %s", program, strerror(errno));
        return GS_ERR_IO;
    }
    feed[0] = -1;
    feed[1] = -1;
    if (input != NULL && pipe(feed) != 0) {
        close(channel[0]);
        close(channel[1]);
        gs_log_debug("proc: no input pipe for %s: %s", program,
                     strerror(errno));
        return GS_ERR_IO;
    }

    child = fork();
    if (child < 0) {
        close(channel[0]);
        close(channel[1]);
        if (feed[0] >= 0) {
            close(feed[0]);
            close(feed[1]);
        }
        gs_log_debug("proc: could not fork for %s: %s", program,
                     strerror(errno));
        return GS_ERR_IO;
    }

    if (child == 0) {
        /* Both channels go down the pipe, since a tool explaining a
         * refusal writes on the error one. Reading is the parent's job,
         * so the child lets its copy go. */
        close(channel[0]);
        if (dup2(channel[1], 1) < 0 || dup2(channel[1], 2) < 0)
            _exit(127);
        close(channel[1]);
        /* Whatever the caller wants typed into this child arrives down a
         * pipe. With nothing to type the child reads end of file at
         * once, which is what stops a program that wants a password from
         * sitting on the terminal waiting and putting a prompt in front
         * of somebody. */
        if (feed[0] >= 0) {
            close(feed[1]);
            if (dup2(feed[0], 0) < 0)
                _exit(127);
            close(feed[0]);
        } else {
            int quiet = open("/dev/null", O_RDONLY);

            if (quiet >= 0) {
                (void)dup2(quiet, 0);
                close(quiet);
            }
        }
        execve(program, (char *const *)argv, envp);
        _exit(127);
    }

    close(channel[1]);
    if (feed[0] >= 0)
        close(feed[0]);
    /* A program whose output nobody drains fills the pipe and stops, so
     * the writing and the reading are watched together rather than one
     * after the other. */
    if (feed[1] >= 0)
        signal(SIGPIPE, SIG_IGN);
    deadline = (long long)time(NULL) + seconds;

    for (;;) {
        struct pollfd pfd[2];
        int watched = 1;
        int left = (int)(deadline - (long long)time(NULL));
        int ready;
        ssize_t got;
        size_t room;

        if (left < 0)
            left = 0;
        pfd[0].fd = channel[0];
        pfd[0].events = POLLIN;
        pfd[0].revents = 0;
        if (feed[1] >= 0) {
            pfd[1].fd = feed[1];
            pfd[1].events = POLLOUT;
            pfd[1].revents = 0;
            watched = 2;
        }
        ready = poll(pfd, (nfds_t)watched, left * 1000);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (ready == 0) {
            timed_out = 1;
            break;
        }

        if (watched == 2 && (pfd[1].revents & (POLLOUT | POLLERR | POLLHUP))) {
            ssize_t put = write(feed[1], input + written, input_len - written);

            if (put > 0)
                written += (size_t)put;
            if (put <= 0 || written >= input_len) {
                /* The program is told the typing has finished, which is
                 * what makes sudo act on the line it has been given. */
                close(feed[1]);
                feed[1] = -1;
            }
            if (!(pfd[0].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
        }

        room = cap - 1 - filled;
        if (room == 0) {
            /* The answer is full, so the rest is read and thrown away
             * rather than left to fill the pipe and stop the child. */
            char bin[256];

            got = read(channel[0], bin, sizeof bin);
            if (got <= 0)
                break;
            continue;
        }
        got = read(channel[0], out + filled, room);
        if (got < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (got == 0)
            break;
        filled += (size_t)got;
        out[filled] = '\0';
    }

    close(channel[0]);
    if (feed[1] >= 0)
        close(feed[1]);

    if (timed_out) {
        gs_log_warn("proc: %s was still running after %d seconds and was "
                    "stopped", program, seconds);
        kill(child, SIGKILL);
        rc = GS_ERR;
    }

    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR)
            break;

    while (filled > 0 && (out[filled - 1] == '\n' || out[filled - 1] == '\r'))
        out[--filled] = '\0';

    if (timed_out)
        return rc;

    if (WIFEXITED(status)) {
        if (exit_code != NULL)
            *exit_code = WEXITSTATUS(status);
        return GS_OK;
    }

    /* Killed by a signal, so there is no exit number to report and the
     * caller cannot tell success from failure. */
    gs_log_debug("proc: %s did not exit normally", program);
    return GS_ERR;
}

#endif

#ifndef _WIN32

int gs_proc_spawn(const char *program, const char *const *argv,
                  const char *log_path)
{
    pid_t middle;
    int status = 0;

    if (program == NULL || argv == NULL || argv[0] == NULL)
        return GS_ERR_ARG;
    if (program[0] != '/')
        return GS_ERR_ARG;

    /* Started through a child of a child. The middle one exits at once
     * and is waited for here, so the program itself is left with no
     * parent and the system adopts it. Without that step it would stay a
     * dead entry in the table until this program waited for it, and this
     * program never will, since it walked away. */
    middle = fork();
    if (middle < 0) {
        gs_log_debug("proc: could not fork for %s: %s", program,
                     strerror(errno));
        return GS_ERR_IO;
    }

    if (middle == 0) {
        pid_t grandchild = fork();

        if (grandchild != 0)
            _exit(grandchild < 0 ? 127 : 0);

        /* A session of its own, so closing the terminal this program was
         * started from does not take the server down with it. */
        if (setsid() < 0)
            _exit(127);

        {
            int quiet = open("/dev/null", O_RDONLY);

            if (quiet >= 0) {
                (void)dup2(quiet, 0);
                close(quiet);
            }
        }
        {
            int out = -1;

            if (log_path != NULL)
                out = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (out < 0)
                out = open("/dev/null", O_WRONLY);
            if (out >= 0) {
                (void)dup2(out, 1);
                (void)dup2(out, 2);
                if (out > 2)
                    close(out);
            }
        }

        /* The surroundings go on unchanged, since a server reads where
         * it keeps its files from them. */
        execv(program, (char *const *)argv);
        _exit(127);
    }

    while (waitpid(middle, &status, 0) < 0)
        if (errno != EINTR)
            break;
    return GS_OK;
}

#endif
