/* picker_test.c
 *
 * The box itself belongs to the system, so nothing here can press a
 * button in it. What can be checked is everything around the box: what
 * reaches a shell, what a refusal leaves behind, and that a machine with
 * no box says so rather than hanging.
 */
#include "picker.h"
#include "picker_internal.h"
#include "gravestone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void test_kinds(void)
{
    const char *kinds = gs_picker_kinds();

    printf("what the box says it will take\n");
    check(kinds != NULL && kinds[0] != '\0', "the box has something to say");
    /* The string goes into a quoted argument, so a quote in it would end
     * that argument early and the rest would run as its own command. */
    check(strchr(kinds, '\'') == NULL, "and no quote that would end it");
    check(strchr(kinds, '|') == NULL,
          "and no bar, which is what Windows uses to separate the parts");
}

/* The title reaches a shell. Every one of these would run something of
 * its own once inside a quoted argument, so all of them are refused. */
static void test_title_is_refused(void)
{
    static const char *const nasty[] = {
        "a'; rm -rf /; echo '",
        "a$(id)",
        "a`id`",
        "a|id",
        "a;id",
        "a\nid",
        "a\"b",
        "a\\b",
        "a&b",
        "a>b",
        ""
    };
    char out[64];
    size_t i;

    printf("a title that would reach past its quotes\n");
    for (i = 0; i < sizeof nasty / sizeof nasty[0]; i++) {
        int rc = gs_picker_open(nasty[i], out, sizeof out);

        check(rc == GS_ERR_ARG, "refused before anything is started");
        check(out[0] == '\0', "and nothing is left in the answer");
    }

    {
        char long_title[200];

        memset(long_title, 'a', sizeof long_title - 1);
        long_title[sizeof long_title - 1] = '\0';
        check(gs_picker_open(long_title, out, sizeof out) == GS_ERR_ARG,
              "a title longer than any real one is refused");
    }
    check(gs_picker_open(NULL, out, sizeof out) == GS_ERR_ARG,
          "no title is refused");
}

/* The script decides where the box opens and whether it lands in front,
 * and reading it costs nothing. Running it would put a box on somebody's
 * screen, so the text is checked instead. */
static void test_script(void)
{
    char script[4096];
    char small[64];

    printf("the script that puts the box on the screen\n");
    check(gs_picker_script("Attach a file", script, sizeof script) == GS_OK,
          "the script is built");

    check(strstr(script, "OpenFileDialog") != NULL, "it asks for a file box");
    check(strstr(script, "$d.Title = 'Attach a file'") != NULL,
          "carrying the title it was given");

    /* Front, and holding the keyboard. */
    check(strstr(script, "$owner.TopMost = $true") != NULL,
          "the box has an owner that sits above ordinary windows");
    check(strstr(script, "$owner.Activate()") != NULL,
          "and that owner takes the keyboard first");
    check(strstr(script, "ShowDialog($owner)") != NULL,
          "the box is opened against that owner rather than against nothing");
    /* A window filling the screen sits above ordinary topmost windows, so
     * the box is lifted again once it exists. */
    check(strstr(script, "SetWindowPos") != NULL,
          "and it is lifted again after it appears");
    check(strstr(script, "[IntPtr](-1)") != NULL,
          "to the place above every other always on top window");
    check(strstr(script, "$tick.Start()") != NULL,
          "on a timer, since the box does not exist until it is running");
    check(strstr(script, "$tick.Stop()") != NULL, "and the timer is stopped");

    check(strstr(script, "$owner.Close()") != NULL,
          "the owner is closed afterwards rather than left behind");

    /* Every extension the chat panel takes reaches the box. */
    check(strstr(script, "*.xlsx") != NULL && strstr(script, "*.mp4") != NULL &&
          strstr(script, "*.webp") != NULL && strstr(script, "*.pptx") != NULL,
          "every kind the panel accepts is offered");
    check(strstr(script, "Every file|*.*") != NULL,
          "along with a way past the filter");

    check(gs_picker_script(NULL, script, sizeof script) == GS_ERR_ARG,
          "no title is refused");
    check(gs_picker_script("a'; rm -rf /", script, sizeof script) == GS_ERR_ARG,
          "a title that would reach past its quotes is refused");
    check(gs_picker_script("Attach a file", NULL, 64) == GS_ERR_ARG,
          "nowhere to write is refused");
    check(gs_picker_script("Attach a file", small, sizeof small) != GS_OK,
          "too little room is refused rather than written short");
}

/* Under WSL the box has to open in the files of the distribution rather
 * than in a Windows folder, since that is where the person is working. */
static void test_start_directory(void)
{
    char script[4096];
    char saved[80];
    const char *had = getenv("WSL_DISTRO_NAME");

    printf("where the box opens\n");
    saved[0] = '\0';
    if (had != NULL)
        snprintf(saved, sizeof saved, "%s", had);

    setenv("WSL_DISTRO_NAME", "Debian", 1);
    check(gs_picker_script("Attach a file", script, sizeof script) == GS_OK,
          "the script is built");
    check(strstr(script, "$d.InitialDirectory = "
                 "'\\\\wsl.localhost\\Debian\\'") != NULL,
          "it opens at the root of the distribution, not in a Windows folder");

    setenv("WSL_DISTRO_NAME", "Ubuntu-22.04", 1);
    gs_picker_script("Attach a file", script, sizeof script);
    check(strstr(script, "wsl.localhost\\Ubuntu-22.04\\") != NULL,
          "whatever the distribution is called");

    /* A name that could reach past its quotes must not become a path. */
    setenv("WSL_DISTRO_NAME", "a'; rm -rf /", 1);
    gs_picker_script("Attach a file", script, sizeof script);
    check(strstr(script, "InitialDirectory") == NULL,
          "an unclean name leaves the box to open wherever it last was");

    unsetenv("WSL_DISTRO_NAME");
    gs_picker_script("Attach a file", script, sizeof script);
    check(strstr(script, "InitialDirectory") == NULL,
          "and so does a machine that is not WSL at all");

    if (saved[0] != '\0')
        setenv("WSL_DISTRO_NAME", saved, 1);
}

static void test_bad_arguments(void)
{
    printf("nowhere to put the answer\n");
    check(gs_picker_open("Attach a file", NULL, 64) == GS_ERR_ARG,
          "no buffer is refused");
    {
        char tiny[1];

        check(gs_picker_open("Attach a file", tiny, sizeof tiny) == GS_ERR_ARG,
              "a buffer with no room is refused");
    }
    {
        char out[64];

        check(gs_picker_open("Attach a file", out, 0) == GS_ERR_ARG,
              "no room at all is refused");
    }
}

/* A machine with no way to put a box on the screen has to say so. Coming
 * back with an error is the answer, since a program that waited would
 * look as though it had stopped. */
static void test_no_box(void)
{
    printf("a machine with no file box\n");
    if (gs_picker_available()) {
        printf("  skip  this machine has one\n");
        skipped++;
        return;
    }
    {
        char out[64];
        int rc = gs_picker_open("Attach a file", out, sizeof out);

        check(rc != GS_OK, "asking for one fails rather than waiting");
        check(out[0] == '\0', "and leaves nothing behind");
    }
}

/* The answer says which of the three cases happened, and a program
 * reading it has to be able to tell them apart. */
static void test_answers_are_distinct(void)
{
    printf("the three answers are three different values\n");
    check(GS_OK != GS_PICKER_NONE, "chose a file is not chose nothing");
    check(GS_PICKER_NONE != GS_ERR, "chose nothing is not a failure");
    check(GS_PICKER_NONE != GS_ERR_ARG, "nor a refused argument");
    check(GS_PICKER_NONE > 0,
          "and it reads as an answer rather than as an error");
}

int main(void)
{
    /* A box left on screen would hold this still for ever, so the test
     * gives up rather than hanging the suite. */
    alarm(60);

    printf("picker\n\n");
    printf("        a file box is %s on this machine\n",
           gs_picker_available() ? "available" : "not available");

    test_kinds();
    test_title_is_refused();
    test_script();
    test_start_directory();
    test_bad_arguments();
    test_no_box();
    test_answers_are_distinct();

    printf("\n%d checks, %d failures, %d skipped\n", checks, failures, skipped);
    return failures == 0 ? 0 : 1;
}
