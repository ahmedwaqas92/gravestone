/* ghost_test.c
 *
 * Adversarial. The parts that can be checked anywhere are the pattern,
 * the cleaning, the script and the encoding, and those get the hard
 * cases. The sweep itself needs a Windows side, so it is reported as a
 * skip on a machine without one rather than as a failure.
 */
#include "ghost.h"
#include "ghost_internal.h"
#include "gravestone.h"
#include "str.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Never the real title. A sweep matching it would hide the window of a
 * copy of the program that happens to be running, since the guard below
 * only recognises another copy of the same binary and this test is a
 * different one. */
#define SAFE_TITLE "GravestoneTestNoSuchWindow"

static int failures;
static int checks;
static int skipped;

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

static void test_pattern(void)
{
    char out[160];
    char saved[80];
    const char *had = getenv("WSL_DISTRO_NAME");

    printf("the pattern that matches a mirror of our window\n");
    saved[0] = '\0';
    if (had != NULL)
        snprintf(saved, sizeof saved, "%s", had);

    setenv("WSL_DISTRO_NAME", "Debian", 1);
    check(gs_ghost_pattern("Gravestone", out, sizeof out) == GS_OK,
          "a plain name works");
    check(strcmp(out, "Gravestone*(Debian)*") == 0,
          "and carries the distribution in brackets");

    unsetenv("WSL_DISTRO_NAME");
    check(gs_ghost_pattern("Gravestone", out, sizeof out) == GS_OK,
          "an unknown distribution still works");
    check(strcmp(out, "Gravestone*") == 0,
          "and drops the brackets instead of guessing one");

    /* A distribution name is not ours, so a name carrying a quote or a
     * wildcard has to be refused rather than folded into the pattern,
     * where it would match windows that are nothing to do with us. */
    setenv("WSL_DISTRO_NAME", "Deb'ian", 1);
    gs_ghost_pattern("Gravestone", out, sizeof out);
    check(strcmp(out, "Gravestone*") == 0, "a quoted name is dropped");
    setenv("WSL_DISTRO_NAME", "Deb*", 1);
    gs_ghost_pattern("Gravestone", out, sizeof out);
    check(strcmp(out, "Gravestone*") == 0, "a wildcard name is dropped");
    setenv("WSL_DISTRO_NAME", "Deb ian", 1);
    gs_ghost_pattern("Gravestone", out, sizeof out);
    check(strcmp(out, "Gravestone*") == 0, "a name with a space is dropped");
    setenv("WSL_DISTRO_NAME", "", 1);
    gs_ghost_pattern("Gravestone", out, sizeof out);
    check(strcmp(out, "Gravestone*") == 0, "an empty name is dropped");

    setenv("WSL_DISTRO_NAME", "Debian", 1);
    check(gs_ghost_pattern("Grave'stone", out, sizeof out) == GS_ERR_ARG,
          "a quoted title is refused outright");
    check(out[0] == '\0', "and leaves nothing behind");
    check(gs_ghost_pattern("Grave*", out, sizeof out) == GS_ERR_ARG,
          "a title carrying a wildcard is refused");
    check(gs_ghost_pattern(NULL, out, sizeof out) == GS_ERR_ARG,
          "no title is refused");
    check(gs_ghost_pattern("Gravestone", NULL, 10) == GS_ERR_ARG,
          "nowhere to write is refused");
    check(gs_ghost_pattern("Gravestone", out, 0) == GS_ERR_ARG,
          "no room at all is refused");
    check(gs_ghost_pattern("Gravestone", out, 8) == GS_ERR_ARG,
          "too little room is refused");
    check(out[0] == '\0', "and that leaves nothing half written");

    if (saved[0] != '\0')
        setenv("WSL_DISTRO_NAME", saved, 1);
    else
        unsetenv("WSL_DISTRO_NAME");
}

static void test_script(void)
{
    char out[4096];
    char small[64];

    printf("the script the Windows side is asked to run\n");
    check(gs_ghost_script("Gravestone*(Debian)*", out, sizeof out) == GS_OK,
          "the script is built");
    check(strstr(out, "Gravestone*(Debian)*") != NULL,
          "and carries the pattern it was given");
    check(strstr(out, "EnumWindows") != NULL, "it walks the desktop");
    check(strstr(out, "ShowWindowAsync") != NULL, "and hides what it finds");
    check(strstr(out, "IsWindowVisible") != NULL,
          "checking first whether a window is even showing");
    check(strstr(out, "hidden $hid already") != NULL,
          "and reports both counts, so an orphan is never walked past");
    check(strstr(out, "$seen.Add") != NULL,
          "the matches are collected outside the walk, since a value "
          "written inside one is thrown away");

    check(gs_ghost_script("has'quote", out, sizeof out) == GS_ERR_ARG,
          "a pattern with a quote is refused, as it would end the string");
    check(gs_ghost_script(NULL, out, sizeof out) == GS_ERR_ARG,
          "no pattern is refused");
    check(gs_ghost_script("x", NULL, 10) == GS_ERR_ARG,
          "nowhere to write is refused");
    check(gs_ghost_script("x", small, sizeof small) == GS_ERR_ARG,
          "too little room is refused rather than written short");
}

static void test_deleted_note(void)
{
    char path[64];

    printf("a program replaced on disk is still the same program\n");
    gs_str_copy(path, sizeof path, "/home/me/build/gravestone (deleted)");
    gs_ghost_drop_deleted_note(path);
    check(strcmp(path, "/home/me/build/gravestone") == 0,
          "the note comes off the end");

    gs_str_copy(path, sizeof path, "/home/me/build/gravestone");
    gs_ghost_drop_deleted_note(path);
    check(strcmp(path, "/home/me/build/gravestone") == 0,
          "a path without one is untouched");

    /* A path that only looks like the note must survive whole, or a real
     * program named this way loses part of its name. */
    gs_str_copy(path, sizeof path, "/home/me/ (deleted)/gravestone");
    gs_ghost_drop_deleted_note(path);
    check(strcmp(path, "/home/me/ (deleted)/gravestone") == 0,
          "the note is only taken off the end");

    gs_str_copy(path, sizeof path, "(deleted)");
    gs_ghost_drop_deleted_note(path);
    check(strcmp(path, "(deleted)") == 0,
          "a path shorter than the note with its space is untouched");

    gs_str_copy(path, sizeof path, " (deleted)");
    gs_ghost_drop_deleted_note(path);
    check(path[0] == '\0', "a path that is only the note becomes nothing");

    gs_str_copy(path, sizeof path, "");
    gs_ghost_drop_deleted_note(path);
    check(path[0] == '\0', "an empty path stays empty");

    gs_ghost_drop_deleted_note(NULL);
    check(1, "no path at all does not crash");
}

/* The sweeper runs on after the program that started it has gone. It must
 * not still be answering that program's stop signals, or a Ctrl-C aimed at
 * the group would be swallowed and would cut short the wait it needs. */
static void test_detach(void)
{
    pid_t child;
    int status = 0;

    printf("a child cut loose answers a signal the ordinary way again\n");
    child = fork();
    if (child < 0) {
        check(0, "the child started");
        return;
    }
    if (child == 0) {
        struct sigaction want;
        struct sigaction found;

        /* Standing in for the parent that caught the stop signals. */
        memset(&want, 0, sizeof want);
        want.sa_handler = SIG_IGN;
        sigemptyset(&want.sa_mask);
        sigaction(SIGTERM, &want, NULL);
        sigaction(SIGINT, &want, NULL);
        sigaction(SIGHUP, &want, NULL);

        gs_ghost_detach();

        memset(&found, 0, sizeof found);
        sigaction(SIGTERM, NULL, &found);
        if (found.sa_handler != SIG_DFL)
            _exit(60);
        memset(&found, 0, sizeof found);
        sigaction(SIGINT, NULL, &found);
        if (found.sa_handler != SIG_DFL)
            _exit(61);
        memset(&found, 0, sizeof found);
        sigaction(SIGHUP, NULL, &found);
        if (found.sa_handler != SIG_DFL)
            _exit(62);
        /* A group of its own, so a signal sent to the old group misses. */
        if (getsid(0) != getpid())
            _exit(63);
        _exit(0);
    }
    while (waitpid(child, &status, 0) < 0)
        ;
    check(WIFEXITED(status), "the child finished by itself");
    check(WIFEXITED(status) && WEXITSTATUS(status) != 60 &&
          WEXITSTATUS(status) != 61 && WEXITSTATUS(status) != 62,
          "every stop signal is back to its ordinary behaviour");
    check(WIFEXITED(status) && WEXITSTATUS(status) != 63,
          "and it sits in a group of its own");
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "with nothing else wrong");
}

static void test_others_running(void)
{
    pid_t child;
    int fds[2];
    int alone;
    char note = 0;

    printf("holding back while another copy is alive\n");

    /* What this reports depends on what else is running on the machine,
     * which a test cannot decide. Two copies of this very test started
     * together see each other, and both are right to. The reading is
     * taken first and every check below is made against it, so the
     * question asked is whether starting a copy changes the answer. */
    alone = gs_ghost_others_running();
    check(alone == 0 || alone == 1, "the answer is a plain yes or no");
    if (alone != 0)
        printf("        another copy of this test is already running\n");

    if (pipe(fds) != 0) {
        check(0, "a pipe was made");
        return;
    }
    child = fork();
    if (child < 0) {
        check(0, "a second copy started");
        close(fds[0]);
        close(fds[1]);
        return;
    }
    if (child == 0) {
        char scratch[4];

        /* A fork of this test runs the same binary, which is exactly what
         * a second copy of the program looks like from the outside. It
         * waits on the pipe, so it is still alive when the check runs,
         * and the parent keeps the writing end open until then. */
        close(fds[1]);
        (void)read(fds[0], scratch, sizeof scratch);
        _exit(0);
    }
    close(fds[0]);
    check(gs_ghost_others_running() == 1,
          "a second copy of the same binary is seen");
    close(fds[1]);            /* the child's read ends and it goes */
    while (waitpid(child, NULL, 0) < 0)
        ;
    (void)note;

    /* The dead copy must stop counting, or the sweep never runs again.
     * On a machine running only this test the answer returns to nothing,
     * and on one running two it returns to the reading taken before the
     * copy was started. */
    check(gs_ghost_others_running() == alone,
          "and it goes back to what it said before, once the copy has "
          "gone");
}

static void test_sweep(void)
{
    int hidden = -1;
    int already = -1;

    printf("the sweep itself\n");
    if (!gs_ghost_available()) {
        printf("  skip  no Windows side on this machine\n");
        skipped++;
        check(gs_ghost_sweep(SAFE_TITLE, &hidden, &already) == GS_OK,
              "and asking for a sweep is quietly nothing");
        check(hidden == 0 && already == 0, "with both counts at nothing");
        check(gs_ghost_sweep_detached(SAFE_TITLE) == GS_OK,
              "and the detached form is quiet too");
        return;
    }

    check(gs_ghost_sweep(SAFE_TITLE, &hidden, &already) == GS_OK,
          "the sweep ran");
    check(hidden >= 0 && already >= 0, "and both counts came back");
    printf("        hid %d, found %d already hidden\n", hidden, already);

    /* Running it twice in a row has to leave nothing to do the second
     * time, or the sweep is not actually disposing of anything. */
    {
        int again = -1;

        check(gs_ghost_sweep(SAFE_TITLE, &again, NULL) == GS_OK,
              "a second sweep runs");
        check(again == 0, "and finds nothing left showing");
    }

    check(gs_ghost_sweep(NULL, &hidden, &already) == GS_ERR_ARG,
          "no title is refused");
    check(gs_ghost_sweep(SAFE_TITLE, NULL, NULL) == GS_OK,
          "and counts are optional");
    check(gs_ghost_sweep_detached(NULL) == GS_ERR_ARG,
          "the detached form refuses no title too");
    check(gs_ghost_sweep_detached(SAFE_TITLE) == GS_OK,
          "and runs without holding anything up");
}

int main(void)
{
    alarm(60);
    printf("ghost\n\n");

    test_pattern();
    test_script();
    test_deleted_note();
    test_detach();
    test_others_running();
    test_sweep();

    printf("\n%d checks, %d failures, %d skipped\n", checks, failures, skipped);
    return failures == 0 ? 0 : 1;
}
