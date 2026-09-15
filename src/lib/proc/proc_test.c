#include "proc.h"
#include "gravestone.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

static int failures, checks;

static void check(int c, const char *what)
{
    checks++;
    if (c) printf("  ok    %s\n", what);
    else { printf("  FAIL  %s\n", what); failures++; }
}

/* A tool that refuses says why on its error channel. A caller about to
 * tell a person that something would not work needs those words, so one
 * form keeps them and the other throws them away. */
static void test_the_reason(void)
{
    char out[256];

    printf("keeping what a tool complained about\n");

    /* A command that writes only to the error channel and fails. Both
     * forms put their redirection on the end of what they are handed, so
     * the command has to be a single one. Two joined by a semicolon
     * would have the redirection land on the second alone, and the
     * complaint from the first would go to the real error channel
     * instead of into the answer. */
    out[0] = 'x';
    check(gs_proc_capture("sh -c 'echo trouble 1>&2; exit 3'", out,
                          sizeof out) != GS_OK,
          "the quiet form reports the failure");
    check(out[0] == '\0', "and keeps none of the complaint");

    out[0] = '\0';
    check(gs_proc_reason("sh -c 'echo trouble 1>&2; exit 3'", out,
                         sizeof out) != GS_OK,
          "the loud form reports it too");
    check(strstr(out, "trouble") != NULL, "while keeping what was said");

    /* A command that works gives the same answer either way. */
    out[0] = '\0';
    check(gs_proc_reason("echo fine", out, sizeof out) == GS_OK,
          "a command that works answers");
    check(strcmp(out, "fine") == 0, "with its own words and no more");

    check(gs_proc_reason(NULL, out, sizeof out) != GS_OK,
          "no command is refused");
    check(gs_proc_reason("echo fine", NULL, sizeof out) != GS_OK,
          "and nowhere to put the answer");
    check(gs_proc_reason("echo fine", out, 0) != GS_OK,
          "and no room at all");
}

/* Running a program with no shell anywhere. This is what every command
 * needing administrator rights goes through, and it exists because the
 * two forms above hand their text to /bin/sh. A value that came out of a
 * database would be read by that shell as instructions, so the checks
 * below are mostly about such a value arriving whole and being treated
 * as one argument. */
static void test_run(void)
{
    char said[256];
    const char *argv[6];
    int code = -1;

    printf("running a program with no shell\n");

    argv[0] = "/bin/echo";
    argv[1] = "hello";
    argv[2] = NULL;
    check(gs_proc_run("/bin/echo", argv, said, sizeof said, &code, 5) == GS_OK,
          "a program runs");
    check(code == 0, "and its exit number comes back");
    check(strcmp(said, "hello") == 0, "with what it printed");

    /* A program that works in silence is the normal case for mount and
     * umount, and the exit number is the only thing that says so. Both
     * older forms report that same case as a failure. */
    argv[0] = "/bin/true";
    argv[1] = NULL;
    code = -1;
    check(gs_proc_run("/bin/true", argv, said, sizeof said, &code, 5) == GS_OK,
          "a program that prints nothing still runs");
    check(code == 0, "and is reported as having worked");
    check(said[0] == '\0', "with an empty answer");

    argv[0] = "/bin/false";
    argv[1] = NULL;
    code = -1;
    check(gs_proc_run("/bin/false", argv, said, sizeof said, &code, 5) == GS_OK,
          "a program that refuses still runs");
    check(code == 1, "and its refusal comes back as the number one");

    /* Every one of these arrives as one argument and nothing takes it
     * apart, which is the whole reason this call exists. */
    argv[0] = "/bin/echo";
    argv[1] = "; rm -rf /";
    argv[2] = NULL;
    check(gs_proc_run("/bin/echo", argv, said, sizeof said, &code, 5) == GS_OK,
          "an argument carrying a semicolon runs");
    check(strcmp(said, "; rm -rf /") == 0,
          "and arrives whole, since nothing read it as two commands");

    argv[1] = "$(id -u)";
    check(gs_proc_run("/bin/echo", argv, said, sizeof said, &code, 5) == GS_OK,
          "an argument carrying a substitution runs");
    check(strcmp(said, "$(id -u)") == 0, "and arrives unevaluated");

    argv[1] = "`id -u`";
    check(gs_proc_run("/bin/echo", argv, said, sizeof said, &code, 5) == GS_OK,
          "and one written the older way");
    check(strcmp(said, "`id -u`") == 0, "arrives unevaluated as well");

    argv[1] = "a && b | c > d";
    check(gs_proc_run("/bin/echo", argv, said, sizeof said, &code, 5) == GS_OK,
          "and one carrying every joining character");
    check(strcmp(said, "a && b | c > d") == 0, "arrives whole");

    /* A relative name would be looked up on a search path, and under WSL
     * most of that path sits on the Windows drive where another program
     * can write. */
    argv[0] = "echo";
    argv[1] = NULL;
    check(gs_proc_run("echo", argv, said, sizeof said, &code, 5) == GS_ERR_ARG,
          "a program named without a path is refused");
    check(gs_proc_run("./echo", argv, said, sizeof said, &code, 5) == GS_ERR_ARG,
          "and one named relative to here");

    argv[0] = "/bin/echo";
    check(gs_proc_run(NULL, argv, said, sizeof said, &code, 5) == GS_ERR_ARG,
          "no program is refused");
    check(gs_proc_run("/bin/echo", NULL, said, sizeof said, &code, 5)
          == GS_ERR_ARG, "no arguments at all is refused");
    check(gs_proc_run("/bin/echo", argv, NULL, 0, &code, 5) == GS_ERR_ARG,
          "nowhere to write is refused");
    check(gs_proc_run("/bin/echo", argv, said, sizeof said, &code, 0)
          == GS_ERR_ARG, "and no time to run in");

    /* A program that is not there fails rather than finding another one
     * of the same name somewhere else. */
    argv[0] = "/bin/this-program-is-not-installed";
    argv[1] = NULL;
    code = -1;
    check(gs_proc_run("/bin/this-program-is-not-installed", argv, said,
                      sizeof said, &code, 5) == GS_OK,
          "a missing program is still a child that ran");
    check(code == 127, "reported the way a shell reports one");

    /* The child gets a search path of its own, since the one this
     * program has may name directories another program can write into. */
    argv[0] = "/usr/bin/printenv";
    argv[1] = "PATH";
    argv[2] = NULL;
    if (gs_proc_run("/usr/bin/printenv", argv, said, sizeof said, &code, 5)
            == GS_OK && code == 0) {
        check(strstr(said, "/mnt/") == NULL,
              "the child's search path names no Windows directory");
        check(strcmp(said, "/usr/sbin:/usr/bin:/sbin:/bin") == 0,
              "it is the fixed one this wrapper sets");
    } else {
        printf("  skip  the child's search path (no printenv here)\n");
    }

    /* A long answer is cut rather than overrunning, and the program is
     * still waited for. */
    argv[0] = "/bin/echo";
    argv[1] = "abcdefghijklmnopqrstuvwxyz";
    argv[2] = NULL;
    check(gs_proc_run("/bin/echo", argv, said, 6, &code, 5) == GS_OK,
          "a long answer still runs");
    check(strlen(said) == 5, "and is cut to fit");
    check(code == 0, "with the exit number still read");

    /* A program that never finishes is stopped rather than holding the
     * interface still for ever. */
    argv[0] = "/bin/sleep";
    argv[1] = "30";
    argv[2] = NULL;
    code = -1;
    check(gs_proc_run("/bin/sleep", argv, said, sizeof said, &code, 1)
          == GS_ERR, "a program past its deadline is reported as a failure");
    check(code == -1, "with no exit number, since it never exited");
}

/* Starting a program and walking away from it. A server started this way
 * has to outlive the thing that started it, or closing the window would
 * take the inference server down with it. */
static void test_spawn(void)
{
    const char *argv[4];
    char log[256];
    char line[256];
    FILE *f;

    printf("starting a program and walking away\n");

    check(gs_proc_spawn(NULL, argv, NULL) == GS_ERR_ARG,
          "no program is refused");
    check(gs_proc_spawn("/bin/echo", NULL, NULL) == GS_ERR_ARG,
          "no arguments at all is refused");
    argv[0] = NULL;
    check(gs_proc_spawn("/bin/echo", argv, NULL) == GS_ERR_ARG,
          "an empty argument list is refused");

    /* A relative name would be looked up on a search path, and under WSL
     * most of that path sits on the Windows drive where another program
     * can write. */
    argv[0] = "echo";
    argv[1] = NULL;
    check(gs_proc_spawn("echo", argv, NULL) == GS_ERR_ARG,
          "a program named without a path is refused");
    check(gs_proc_spawn("./echo", argv, NULL) == GS_ERR_ARG,
          "and one named relative to here");

    /* What it prints goes to the file it was given, written fresh. */
    snprintf(log, sizeof log, "build/spawn-test.log");
    (void)system("rm -f build/spawn-test.log");
    argv[0] = "/bin/echo";
    argv[1] = "started and gone";
    argv[2] = NULL;
    check(gs_proc_spawn("/bin/echo", argv, log) == GS_OK,
          "a real program starts");

    /* It runs on its own clock, so the file is waited for rather than
     * read at once. */
    {
        int tries;

        line[0] = '\0';
        for (tries = 0; tries < 50; tries++) {
            f = fopen(log, "r");
            if (f != NULL) {
                if (fgets(line, sizeof line, f) != NULL && line[0] != '\0') {
                    fclose(f);
                    break;
                }
                fclose(f);
            }
            {
                struct timespec tenth;

                tenth.tv_sec = 0;
                tenth.tv_nsec = 100000000L;
                nanosleep(&tenth, NULL);
            }
        }
        check(strstr(line, "started and gone") != NULL,
              "and what it printed reached the file it was given");
    }

    /* Nothing is left waiting to be collected. A program started and
     * never waited for would sit in the table as a dead entry for as
     * long as this one runs. */
    {
        int reaped = waitpid(-1, NULL, WNOHANG);

        check(reaped <= 0, "and no child is left waiting to be collected");
    }

    /* The program is put in a session of its own, so the terminal going
     * away does not take it with it. A sleeper is watched, since it is
     * still there to be looked at. */
    argv[0] = "/bin/sleep";
    argv[1] = "30";
    argv[2] = NULL;
    check(gs_proc_spawn("/bin/sleep", argv, NULL) == GS_OK,
          "a long running program starts");
    {
        char command[256];
        char answer[256];
        int tries;
        long mine = (long)getpid();

        answer[0] = '\0';
        for (tries = 0; tries < 30; tries++) {
            struct timespec tenth;

            if (gs_proc_capture("pgrep -x sleep", answer,
                                sizeof answer) == GS_OK && answer[0] != '\0')
                break;
            tenth.tv_sec = 0;
            tenth.tv_nsec = 100000000L;
            nanosleep(&tenth, NULL);
        }
        if (answer[0] == '\0') {
            printf("  skip  the session it runs in (no sleeper was found)\n");
        } else {
            char ppid[64];

            snprintf(command, sizeof command,
                     "ps -o ppid= -p %.20s", answer);
            ppid[0] = '\0';
            if (gs_proc_capture(command, ppid, sizeof ppid) == GS_OK) {
                long parent = strtol(ppid, NULL, 10);

                check(parent != mine,
                      "and it is no longer a child of this program");
            } else {
                printf("  skip  its parent (ps would not answer)\n");
            }
            /* Tidied up, so a sleeper is not left behind by the test. */
            snprintf(command, sizeof command, "kill %.20s 2>/dev/null",
                     answer);
            (void)system(command);
        }
    }
}

int main(void)
{
    char out[256];

    gs_log_set_level(GS_LOG_ERROR);

    printf("bad arguments\n");
    check(gs_proc_capture(NULL, out, sizeof out) == GS_ERR_ARG,
          "a null command is refused");
    check(gs_proc_capture("echo hi", NULL, 10) == GS_ERR_ARG,
          "a null buffer is refused");
    check(gs_proc_capture("echo hi", out, 0) == GS_ERR_ARG,
          "a buffer of no size is refused");

    printf("running something\n");
    check(gs_proc_capture("echo hello", out, sizeof out) == GS_OK,
          "a working command succeeds");
    check(strcmp(out, "hello") == 0, "its output came back without a newline");

    check(gs_proc_capture("printf 'one\\ntwo\\n'", out, sizeof out) == GS_OK,
          "several lines are captured");
    check(strcmp(out, "one\ntwo") == 0, "both lines arrived, trailing cut");

    printf("failures\n");
    check(gs_proc_capture("exit 3", out, sizeof out) == GS_ERR,
          "a non zero exit is reported as failure");
    check(out[0] == '\0', "and the buffer is left empty");
    check(gs_proc_capture("this-command-does-not-exist", out,
                          sizeof out) == GS_ERR,
          "a missing program is reported as failure");
    check(gs_proc_capture("true", out, sizeof out) == GS_ERR,
          "a command that prints nothing counts as no answer");

    printf("standard error stays out\n");
    check(gs_proc_capture("echo oops >&2; echo good", out,
                          sizeof out) == GS_OK, "the command ran");
    check(strcmp(out, "good") == 0,
          "only what went to standard output came back");

    printf("a small buffer\n");
    check(gs_proc_capture("echo abcdefghijklmnop", out, 6) == GS_OK,
          "a long answer still succeeds");
    check(strlen(out) == 5, "it was cut to fit the buffer");
    check(out[5] == '\0', "and left terminated");

    printf("looking for a program\n");
    check(gs_proc_exists("sh") == 1, "the shell is found");
    check(gs_proc_exists("this-command-does-not-exist") == 0,
          "a missing one is not");
    check(gs_proc_exists(NULL) == 0, "a null name is not");
    check(gs_proc_exists("") == 0, "an empty name is not");
    check(gs_proc_exists("sh; rm -rf /tmp/nothing") == 0,
          "a name carrying shell syntax is refused outright");
    check(gs_proc_exists("sh $(whoami)") == 0,
          "so is one carrying a substitution");

    test_the_reason();
    test_run();
    test_spawn();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
