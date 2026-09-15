/* sig_test.c
 *
 * Adversarial. Every check tries to make the shutdown path miss a signal
 * rather than confirm that it catches an easy one. A watchdog kills the
 * whole test after thirty seconds, so a wait that never returns fails loudly
 * instead of holding the suite still for ever.
 */
#include "sig.h"
#include "gravestone.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;
static int checks;

static void check(int condition, const char *what)
{
    checks++;
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

/* Puts the module back to how it started, so one test never decides the
 * result of the next. */
static void reset(void)
{
    gs_sig_release();
}

static void test_bad_state(void)
{
    printf("before anything is installed\n");
    reset();
    check(gs_sig_wake_fd() == -1, "there is no descriptor to watch");
    check(gs_sig_quit_requested() == 0, "and nothing has been requested");
    check(gs_sig_quit_number() == 0, "and no signal is on record");
    gs_sig_drain();
    check(1, "draining nothing does not crash");
    gs_sig_release();
    check(1, "releasing nothing does not crash");
    gs_sig_reraise();
    check(1, "raising nothing does not kill the test");
}

static void test_install(void)
{
    int fd;
    int flags;

    printf("installing arms a pipe that cannot block or leak\n");
    reset();
    check(gs_sig_install() == GS_OK, "install succeeds");
    fd = gs_sig_wake_fd();
    check(fd >= 0, "and hands back a descriptor");

    flags = fcntl(fd, F_GETFL, 0);
    check(flags >= 0 && (flags & O_NONBLOCK) != 0,
          "the reading end never blocks");
    flags = fcntl(fd, F_GETFD, 0);
    check(flags >= 0 && (flags & FD_CLOEXEC) != 0,
          "and closes itself when a program is replaced");

    check(gs_sig_install() == GS_OK, "installing twice is allowed");
    check(gs_sig_wake_fd() == fd, "and the descriptor does not change");

    gs_sig_release();
    check(gs_sig_wake_fd() == -1, "releasing takes the descriptor back");
    check(fcntl(fd, F_GETFL, 0) < 0 && errno == EBADF,
          "and closes it rather than leaking it");
}

/* The whole point of the module. A signal has to make a wait that would
 * otherwise sit for ever come back at once. */
static void test_wakes_a_blocked_wait(void)
{
    struct pollfd pfd;
    int ready;

    printf("a signal cuts short a wait that has no timeout\n");
    reset();
    gs_sig_install();

    raise(SIGINT);

    pfd.fd = gs_sig_wake_fd();
    pfd.events = POLLIN;
    pfd.revents = 0;
    ready = poll(&pfd, 1, 3000);
    check(ready == 1, "the descriptor came back ready");
    check((pfd.revents & POLLIN) != 0, "with something to read on it");
    check(gs_sig_quit_requested() == 1, "and the quit flag is set");
    check(gs_sig_quit_number() == SIGINT, "carrying the signal that arrived");
}

static void test_drain(void)
{
    struct pollfd pfd;

    printf("draining stops the descriptor reporting itself for ever\n");
    reset();
    gs_sig_install();
    /* One only. A second terminate would end the test, which is the point
     * of the section below. */
    raise(SIGTERM);

    gs_sig_drain();
    pfd.fd = gs_sig_wake_fd();
    pfd.events = POLLIN;
    pfd.revents = 0;
    check(poll(&pfd, 1, 50) == 0, "nothing is left on it");
    check(gs_sig_quit_requested() == 1,
          "but the request itself survives the drain");
    check(gs_sig_quit_number() == SIGTERM, "and so does which signal it was");

    gs_sig_drain();
    check(1, "draining an empty pipe does not block");
}

/* A second signal while the program is already shutting down must not
 * change the record, because the shutdown is already under way for the
 * first one. */
static void test_second_signal(void)
{
    printf("a second signal does not rewrite the first\n");
    reset();
    gs_sig_install();
    raise(SIGINT);
    check(gs_sig_quit_number() == SIGINT, "the first signal is on record");
    raise(SIGTERM);
    check(gs_sig_quit_number() == SIGINT, "and the second leaves it alone");
    check(gs_sig_quit_requested() == 1, "the request is still standing");
}

/* The tidying after a stop signal writes files and waits on the display,
 * either of which can stop for a long time. A second press of the same
 * key has to end the program there and then rather than queue behind it.
 * That only works if the signal goes back to its ordinary behaviour the
 * moment the first one is recorded. */
static void test_second_of_the_same_kills(void)
{
    pid_t child;
    int status = 0;

    printf("a second press of the same key ends it on the spot\n");
    reset();
    child = fork();
    if (child < 0) {
        check(0, "the child started");
        return;
    }
    if (child == 0) {
        gs_sig_install();
        raise(SIGINT);           /* recorded, and the loop would now stop */
        raise(SIGINT);           /* nothing catches this one */
        _exit(9);                /* never reached */
    }
    check(waitpid(child, &status, 0) == child, "the child came back");
    check(WIFSIGNALED(status), "and it was killed rather than exiting");
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGINT,
          "by the interrupt it was sent the second time");
}

/* The signal that arrived hands itself back. The other two stay caught,
 * so a stop request by any other route is still handled properly. */
static void test_others_stay_caught(void)
{
    struct sigaction found;

    printf("only the signal that arrived hands itself back\n");
    reset();
    gs_sig_install();
    raise(SIGINT);

    memset(&found, 0, sizeof found);
    sigaction(SIGINT, NULL, &found);
    check(found.sa_handler == SIG_DFL, "the interrupt is back to normal");

    memset(&found, 0, sizeof found);
    sigaction(SIGTERM, NULL, &found);
    check(found.sa_handler != SIG_DFL && found.sa_handler != SIG_IGN,
          "and terminate is still caught");

    /* Which also bounds what the pipe can ever hold, since each signal
     * writes at most once and there are three of them. */
    raise(SIGTERM);
    check(gs_sig_quit_requested() == 1, "a later terminate is still handled");
    check(gs_sig_quit_number() == SIGINT,
          "and the first signal is still the one on record");
}

/* A signal the parent chose to ignore has to stay ignored, or a program
 * started in the background dies when the terminal in front of it is
 * interrupted. */
static void test_inherited_ignore(void)
{
    struct sigaction want;
    struct sigaction found;

    printf("a signal already being ignored stays ignored\n");
    reset();
    memset(&want, 0, sizeof want);
    want.sa_handler = SIG_IGN;
    sigemptyset(&want.sa_mask);
    sigaction(SIGHUP, &want, NULL);

    gs_sig_install();
    memset(&found, 0, sizeof found);
    sigaction(SIGHUP, NULL, &found);
    check(found.sa_handler == SIG_IGN, "hangup is left alone");

    raise(SIGHUP);
    check(gs_sig_quit_requested() == 0, "so raising it changes nothing");

    check(gs_sig_quit_number() != SIGHUP, "and it never reaches the record");
    gs_sig_release();

    memset(&want, 0, sizeof want);
    want.sa_handler = SIG_DFL;
    sigemptyset(&want.sa_mask);
    sigaction(SIGHUP, &want, NULL);
}

/* Releasing has to put back exactly what was there, or a program that
 * stops catching signals halfway through is left with ours. */
static void test_release_restores(void)
{
    struct sigaction found;

    printf("releasing puts the old behaviour back\n");
    reset();
    memset(&found, 0, sizeof found);
    sigaction(SIGINT, NULL, &found);
    check(found.sa_handler == SIG_DFL, "interrupt starts at the default");

    gs_sig_install();
    memset(&found, 0, sizeof found);
    sigaction(SIGINT, NULL, &found);
    check(found.sa_handler != SIG_DFL && found.sa_handler != SIG_IGN,
          "installing takes it over");
    check((found.sa_flags & SA_RESTART) == 0,
          "and asks for waits to be cut short rather than restarted");

    gs_sig_release();
    memset(&found, 0, sizeof found);
    sigaction(SIGINT, NULL, &found);
    check(found.sa_handler == SIG_DFL, "releasing gives it back");
}

/* The shape the program actually uses. A child installs the module, waits
 * with no timeout, is interrupted from outside, and has to come back and
 * exit by itself rather than being killed where it stood. */
static void test_real_interrupt(void)
{
    pid_t child;
    int status = 0;
    int fds[2];
    char ready = 0;

    printf("a real interrupt from outside ends a real wait\n");
    reset();
    if (pipe(fds) != 0) {
        check(0, "a pipe was made");
        return;
    }

    child = fork();
    if (child < 0) {
        check(0, "the child started");
        close(fds[0]);
        close(fds[1]);
        return;
    }
    if (child == 0) {
        struct pollfd pfd;
        char note = 'r';
        int got;
        int interrupted;

        close(fds[0]);
        if (gs_sig_install() != GS_OK)
            _exit(70);
        if (write(fds[1], &note, 1) != 1)
            _exit(71);

        pfd.fd = gs_sig_wake_fd();
        pfd.events = POLLIN;
        pfd.revents = 0;
        /* No timeout at all, so only the signal can end this. It ends one
         * of two ways. A signal landing while the wait is already running
         * cuts the wait short and reports an interruption. A signal
         * landing in the moment before the wait starts leaves its byte in
         * the pipe, and the wait then finds the pipe ready. Both are the
         * wait coming back, which is all the loop needs. */
        got = poll(&pfd, 1, -1);
        if (got == 1)
            interrupted = 0;
        else if (got < 0 && errno == EINTR)
            interrupted = 1;
        else
            _exit(72);
        (void)interrupted;
        if (!gs_sig_quit_requested())
            _exit(73);
        _exit(gs_sig_quit_number() == SIGTERM ? 0 : 74);
    }

    close(fds[1]);
    check(read(fds[0], &ready, 1) == 1 && ready == 'r',
          "the child is installed and waiting");
    close(fds[0]);

    kill(child, SIGTERM);
    check(waitpid(child, &status, 0) == child, "the child came back");
    check(WIFEXITED(status), "by exiting rather than being killed");
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "and it saw the terminate it was sent");
}

/* The half the self pipe exists for. A signal that lands in the moment
 * before a wait begins leaves nothing to interrupt, so the wait would sit
 * for ever with nothing but a flag set behind it. The byte in the pipe is
 * what stops that. */
static void test_signal_just_before_the_wait(void)
{
    struct pollfd pfd;
    int ready;

    printf("a signal that lands before the wait still ends it\n");
    reset();
    gs_sig_install();

    /* Raised, handled and returned from, all before poll is entered. */
    raise(SIGINT);
    check(gs_sig_quit_requested() == 1, "the signal has already been dealt with");

    pfd.fd = gs_sig_wake_fd();
    pfd.events = POLLIN;
    pfd.revents = 0;
    ready = poll(&pfd, 1, -1);
    check(ready == 1, "and a wait with no timeout still comes straight back");
    check((pfd.revents & POLLIN) != 0, "because the byte is waiting in the pipe");
}

/* Raising the signal again at the end has to look, from outside, exactly
 * like the program was killed by it. */
static void test_reraise_looks_like_a_kill(void)
{
    pid_t child;
    int status = 0;

    printf("raising the signal again reads as a kill from outside\n");
    reset();
    child = fork();
    if (child < 0) {
        check(0, "the child started");
        return;
    }
    if (child == 0) {
        gs_sig_install();
        raise(SIGINT);
        gs_sig_reraise();
        _exit(9);            /* never reached when reraise works */
    }
    check(waitpid(child, &status, 0) == child, "the child came back");
    check(WIFSIGNALED(status), "and it was killed rather than exiting");
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGINT,
          "by the same signal it caught");
}

int main(void)
{
    /* A regression here is a program that never returns, so the test kills
     * itself rather than hanging the suite. */
    alarm(30);

    printf("sig\n\n");

    test_bad_state();
    test_install();
    test_wakes_a_blocked_wait();
    test_drain();
    test_second_signal();
    test_second_of_the_same_kills();
    test_others_stay_caught();
    test_inherited_ignore();
    test_release_restores();
    test_real_interrupt();
    test_signal_just_before_the_wait();
    test_reraise_looks_like_a_kill();

    reset();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
