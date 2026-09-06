#include "proc.h"
#include "gravestone.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

static void check(int c, const char *what)
{
    checks++;
    if (c) printf("  ok    %s\n", what);
    else { printf("  FAIL  %s\n", what); failures++; }
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

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
