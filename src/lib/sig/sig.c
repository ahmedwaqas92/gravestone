/* sig.c
 *
 * The pipe is made before any handler is armed, so a signal arriving at
 * the earliest possible moment still has somewhere to write. Both ends
 * are closed on exec and never block, which keeps a full pipe from
 * holding a handler still.
 */
#include "sig.h"
#include "gravestone.h"

#ifndef _WIN32

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

/* Written by the handler and read by everything else, so both are the
 * one type the standard promises can be read and written in a single
 * step. Nothing else may be touched from inside a handler. */
static volatile sig_atomic_t quit_flag;
static volatile sig_atomic_t quit_number;

static int wake_read = -1;
static int wake_write = -1;
static int armed;

/* The signals a stop request arrives as. Hangup is included because a
 * terminal closing sends it, and an editor stopping a task often closes
 * the terminal rather than sending anything else. */
static const int caught[] = { SIGINT, SIGTERM, SIGHUP };
#define CAUGHT_COUNT ((int)(sizeof caught / sizeof caught[0]))

static struct sigaction previous[CAUGHT_COUNT];

/* Which of them we actually took over. The old behaviour cannot be told
 * from the record alone, because the default behaviour is written as a
 * null pointer and that is also what an empty record holds. */
static int taken[CAUGHT_COUNT];

/* Everything this handler calls is on the short list a handler may use.
 * write is on it. Assignment to sig_atomic_t is a plain store. errno is
 * saved and put back because write may change it, and the interrupted
 * code is entitled to find errno as it left it. */
static void on_signal(int number)
{
    int saved = errno;
    const char byte = 'q';

    if (quit_flag == 0) {
        quit_flag = 1;
        quit_number = number;

        /* The tidying that follows reads and writes files and waits on
         * the display, any of which can stop for a long time. A second
         * press of the same key has to end the program there and then, so
         * this signal goes back to its ordinary behaviour now that the
         * first one has been recorded. sigaction is one of the calls a
         * handler is allowed to make. */
        {
            struct sigaction plain;

            memset(&plain, 0, sizeof plain);
            plain.sa_handler = SIG_DFL;
            sigemptyset(&plain.sa_mask);
            sigaction(number, &plain, NULL);
        }
    }
    if (wake_write >= 0) {
        ssize_t written = write(wake_write, &byte, 1);

        (void)written;   /* a full pipe already carries the message */
    }
    errno = saved;
}

static int set_flags(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return GS_ERR;
    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return GS_ERR;
    return GS_OK;
}

int gs_sig_install(void)
{
    int fds[2];
    int i;

    if (armed)
        return GS_OK;

    /* pipe2 would set both flags in one call, and it is not declared
     * under the standard this project compiles against, so the flags go
     * on afterwards. */
    if (pipe(fds) != 0)
        return GS_ERR;
    if (set_flags(fds[0]) != GS_OK || set_flags(fds[1]) != GS_OK) {
        close(fds[0]);
        close(fds[1]);
        return GS_ERR;
    }
    wake_read = fds[0];
    wake_write = fds[1];

    for (i = 0; i < CAUGHT_COUNT; i++) {
        struct sigaction want;

        memset(&want, 0, sizeof want);
        want.sa_handler = on_signal;
        sigemptyset(&want.sa_mask);
        /* No SA_RESTART, because a wait that restarts by itself never
         * hands control back to the loop that has to act on this. */
        want.sa_flags = 0;

        taken[i] = 0;
        if (sigaction(caught[i], NULL, &previous[i]) != 0) {
            memset(&previous[i], 0, sizeof previous[i]);
            continue;
        }
        /* A signal the parent chose to ignore stays ignored, since a
         * program started in the background is meant to survive the
         * interrupt that stops the one in front. */
        if (previous[i].sa_handler == SIG_IGN)
            continue;
        if (sigaction(caught[i], &want, NULL) == 0)
            taken[i] = 1;
    }

    armed = 1;
    return GS_OK;
}

int gs_sig_wake_fd(void)
{
    return wake_read;
}

int gs_sig_quit_requested(void)
{
    return quit_flag != 0;
}

int gs_sig_quit_number(void)
{
    return (int)quit_number;
}

void gs_sig_drain(void)
{
    char scratch[64];

    if (wake_read < 0)
        return;
    while (read(wake_read, scratch, sizeof scratch) > 0)
        ;
}

void gs_sig_release(void)
{
    int i;

    if (!armed)
        return;
    for (i = 0; i < CAUGHT_COUNT; i++) {
        if (taken[i])
            sigaction(caught[i], &previous[i], NULL);
        taken[i] = 0;
    }

    if (wake_read >= 0)
        close(wake_read);
    if (wake_write >= 0)
        close(wake_write);
    wake_read = -1;
    wake_write = -1;
    armed = 0;

    /* The record goes with the pipe. A later install starts clean rather
     * than finding a signal that arrived before it was watching. */
    quit_flag = 0;
    quit_number = 0;
}

void gs_sig_reraise(void)
{
    int number = (int)quit_number;
    struct sigaction plain;

    if (number == 0)
        return;
    gs_sig_release();

    memset(&plain, 0, sizeof plain);
    plain.sa_handler = SIG_DFL;
    sigemptyset(&plain.sa_mask);
    sigaction(number, &plain, NULL);
    raise(number);
}

#else /* _WIN32 */

/* Windows ends a console program through its own handler and a desktop
 * program through a close message, and gravestone already answers the
 * close message, so there is nothing here to catch. */

int gs_sig_install(void)      { return GS_OK; }
int gs_sig_wake_fd(void)      { return -1; }
int gs_sig_quit_requested(void) { return 0; }
int gs_sig_quit_number(void)  { return 0; }
void gs_sig_drain(void)       { }
void gs_sig_release(void)     { }
void gs_sig_reraise(void)     { }

#endif
