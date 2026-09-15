/* str_test.c
 *
 * Both pieces here feed something outside this program, so both get the
 * hard cases rather than the easy ones. A command carried across to
 * Windows has to survive every character a shell would act on, and a
 * distribution name reaches a shell and a Windows path alike.
 */
#include "str.h"
#include "gravestone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* Decodes base64 back into plain characters, so the encoder can be
 * checked against something other than itself. */
static int decode(const char *text, char *out, size_t cap)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char wide[8192];
    size_t at = 0;
    size_t i;
    size_t len = strlen(text);
    size_t plain = 0;

    if (len % 4 != 0)
        return -1;
    for (i = 0; i < len; i += 4) {
        unsigned int block = 0;
        int pad = 0;
        int j;

        for (j = 0; j < 4; j++) {
            char c = text[i + (size_t)j];
            const char *found;

            if (c == '=') {
                pad++;
                block <<= 6;
                continue;
            }
            found = strchr(alphabet, c);
            if (found == NULL)
                return -1;
            block = (block << 6) | (unsigned int)(found - alphabet);
        }
        if (at + 3 > sizeof wide)
            return -1;
        wide[at++] = (unsigned char)((block >> 16) & 0xff);
        if (pad < 2)
            wide[at++] = (unsigned char)((block >> 8) & 0xff);
        if (pad < 1)
            wide[at++] = (unsigned char)(block & 0xff);
    }
    /* Every second byte carries nothing, so only the first of each pair
     * is kept. */
    for (i = 0; i < at; i += 2) {
        if (wide[i + 1] != 0)
            return -1;
        if (plain + 1 >= cap)
            return -1;
        out[plain++] = (char)wide[i];
    }
    out[plain] = '\0';
    return (int)plain;
}

static void test_encode(void)
{
    char out[8192];
    char back[4096];
    int written;

    printf("carrying the script across without a shell touching it\n");
    written = gs_str_windows_command("A", out, sizeof out);
    check(written == 4, "one character makes four");
    check(strcmp(out, "QQA=") == 0, "and it is the pair Windows expects");

    written = gs_str_windows_command("", out, sizeof out);
    check(written == 0, "nothing makes nothing");
    check(out[0] == '\0', "and the result is still terminated");

    check(gs_str_windows_command("Hi", out, sizeof out) == 8, "two characters make eight");
    check(decode(out, back, sizeof back) == 2 && strcmp(back, "Hi") == 0,
          "and decoding gives back what went in");

    /* The characters a shell would act on are exactly the ones the script
     * is full of, so they are the ones worth checking survive. */
    {
        const char *nasty = "$x `y` \"z\" 'q' \\n | & ; > < \n\t";

        check(gs_str_windows_command(nasty, out, sizeof out) > 0,
              "a line full of shell punctuation encodes");
        check(decode(out, back, sizeof back) > 0 && strcmp(back, nasty) == 0,
              "and every character comes back exactly");
    }

    {
        char script[4096];

        snprintf(script, sizeof script,
                 "$d.Filter = 'x'\n"
                 "if ($d.ShowDialog() -eq 'OK') { Write-Output $d.FileName }\n");
        check(gs_str_windows_command(script, out, sizeof out) > 0,
              "the real script encodes");
        check(decode(out, back, sizeof back) > 0 &&
              strcmp(back, script) == 0,
              "and comes back byte for byte");
        check(strchr(out, '\'') == NULL && strchr(out, '"') == NULL &&
              strchr(out, '$') == NULL && strchr(out, '`') == NULL &&
              strchr(out, ' ') == NULL && strchr(out, '\n') == NULL,
              "with nothing left in it a shell would act on");
    }

    check(gs_str_windows_command(NULL, out, sizeof out) < 0, "no text is refused");
    check(gs_str_windows_command("x", NULL, 10) < 0, "nowhere to write is refused");
    check(gs_str_windows_command("x", out, 0) < 0, "no room is refused");
    check(gs_str_windows_command("abcd", out, 4) < 0,
          "too little room is refused rather than written short");
    {
        char exact[5];

        check(gs_str_windows_command("A", exact, sizeof exact) == 4,
              "room for exactly the answer and its terminator is enough");
    }
}

/* The guard answers by looking for another process running the same
 * binary. Both answers matter. A wrong yes stops the sweep ever running,
 * and a wrong no lets it hide the window of a copy that is still open. */
/* Rebuilding the program while a copy of it is open makes every reading of
 * that copy's path come back with a note on the end. Leaving the note on
 * means the guard stops recognising the copy, and the sweep then hides a
 * window that is still in use. */

static void test_distro(void)
{
    char out[80];
    char saved[80];
    const char *had = getenv("WSL_DISTRO_NAME");

    printf("the name of the distribution this program sits inside\n");
    saved[0] = '\0';
    if (had != NULL)
        snprintf(saved, sizeof saved, "%s", had);

    setenv("WSL_DISTRO_NAME", "Debian", 1);
    check(gs_str_wsl_distro(out, sizeof out) == GS_OK, "a plain name is taken");
    check(strcmp(out, "Debian") == 0, "and comes through unchanged");

    setenv("WSL_DISTRO_NAME", "Ubuntu-22.04", 1);
    check(gs_str_wsl_distro(out, sizeof out) == GS_OK,
          "digits, dash and dot are all allowed");
    check(strcmp(out, "Ubuntu-22.04") == 0, "and survive whole");
    setenv("WSL_DISTRO_NAME", "my_distro", 1);
    check(gs_str_wsl_distro(out, sizeof out) == GS_OK, "underscore is allowed");

    /* The name goes into a quoted argument and into a Windows path, so
     * anything that would end either is refused outright. A trimmed name
     * would reach a different distribution. */
    {
        static const char *const nasty[] = {
            "a b", "a;rm -rf /", "a$(id)", "a`id`", "a|b", "a'b", "a\"b",
            "a\\b", "a/b", "a\nb", "a&b", "a>b", "a*b", ""
        };
        size_t i;

        for (i = 0; i < sizeof nasty / sizeof nasty[0]; i++) {
            setenv("WSL_DISTRO_NAME", nasty[i], 1);
            check(gs_str_wsl_distro(out, sizeof out) != GS_OK,
                  "a name that would reach past its quotes is refused");
            check(out[0] == '\0', "and leaves nothing behind");
        }
    }

    {
        char long_name[200];
        char room[300];

        /* Sixty five characters, refused on its length alone. The buffer
         * is wide enough to hold it, so nothing else can be doing the
         * refusing. */
        memset(long_name, 'a', 65);
        long_name[65] = '\0';
        setenv("WSL_DISTRO_NAME", long_name, 1);
        check(gs_str_wsl_distro(room, sizeof room) != GS_OK,
              "a name longer than any real one is refused on its length");
        check(room[0] == '\0', "and leaves nothing behind");

        /* Sixty four is the longest that is taken. */
        memset(long_name, 'a', 64);
        long_name[64] = '\0';
        setenv("WSL_DISTRO_NAME", long_name, 1);
        check(gs_str_wsl_distro(room, sizeof room) == GS_OK,
              "one character shorter is taken");
        check(strlen(room) == 64, "whole");
    }

    setenv("WSL_DISTRO_NAME", "Debian", 1);
    check(gs_str_wsl_distro(out, 4) != GS_OK,
          "a buffer too small for the name is refused");
    check(out[0] == '\0', "and written empty rather than short");
    check(gs_str_wsl_distro(NULL, 80) == GS_ERR_ARG,
          "nowhere to write is refused");
    check(gs_str_wsl_distro(out, 0) == GS_ERR_ARG, "no room at all is refused");

    unsetenv("WSL_DISTRO_NAME");
    check(gs_str_wsl_distro(out, sizeof out) != GS_OK,
          "a machine that is not WSL has no name to give");
    check(out[0] == '\0', "and leaves nothing behind");

    if (saved[0] != '\0')
        setenv("WSL_DISTRO_NAME", saved, 1);
}

/* A moment written by the clock of this machine. The store keeps seconds
 * since the start of 1970, and a person reads a wall clock, so the two
 * have to be joined somewhere. */
static void test_the_clock(void)
{
    char out[32];
    time_t now = time(NULL);

    printf("a moment on the local clock\n");

    /* Nothing recorded reads as nothing rather than as 1970. */
    gs_str_clock(0, out, sizeof out);
    check(out[0] == '\0', "a time of nought writes nothing");
    gs_str_clock(-5, out, sizeof out);
    check(out[0] == '\0', "and neither does one before it");

    /* Today carries the hour and minute alone. */
    gs_str_clock((long long)now, out, sizeof out);
    check(strlen(out) == 5, "today is five characters, as in 14:32");
    check(out[2] == ':', "with the colon in the middle");
    check(out[0] >= '0' && out[0] <= '2', "an hour starting nought to two");
    printf("        now reads %s\n", out);

    /* An earlier day carries its date, since a bare hour from last week
     * says nothing. */
    gs_str_clock((long long)now - 8 * 24 * 3600, out, sizeof out);
    check(strlen(out) > 5, "a week ago carries more than the hour");
    check(strstr(out, ":") != NULL, "still naming the minute");
    printf("        a week ago reads %s\n", out);

    /* The local clock is what is asked for, so two moments an hour apart
     * differ by an hour on it. */
    {
        char later[32];

        gs_str_clock((long long)now, out, sizeof out);
        gs_str_clock((long long)now + 3600, later, sizeof later);
        check(strcmp(out, later) != 0, "an hour later reads differently");
    }

    /* Nowhere to write, and no room to write in. */
    gs_str_clock((long long)now, NULL, sizeof out);
    out[0] = 'x';
    gs_str_clock((long long)now, out, 0);
    check(out[0] == 'x', "no room leaves the buffer alone");
    gs_str_clock((long long)now, out, 3);
    check(out[0] == '\0', "too little room writes nothing rather than a piece");
}

/* Text a model wrote, on its way to a screen that draws only ASCII. The
 * first case is the line from the chat that came out as "questionand". */
static void test_plain_ascii(void)
{
    char out[256];
    size_t n;

    printf("\nwriting a model's text in plain ASCII\n");

    n = gs_str_to_ascii("That's a great question\xe2\x80\x94" "and I appreciate "
                        "you asking! \xf0\x9f\x98\x8a Let me clarify",
                        out, sizeof out);
    check(strcmp(out, "That's a great question - and I appreciate you "
                      "asking! Let me clarify") == 0,
          "the dash comes through as a spaced hyphen and the emoji leaves "
          "no double space");
    check(n == strlen(out), "and the count matches what was written");

    gs_str_to_ascii("a \xe2\x80\x94 b", out, sizeof out);
    check(strcmp(out, "a - b") == 0,
          "a dash that already had spaces gets no more");
    gs_str_to_ascii("1\xe2\x80\x93" "2", out, sizeof out);
    check(strcmp(out, "1 - 2") == 0, "and so does an en dash");

    gs_str_to_ascii("\xe2\x80\x9cquoted\xe2\x80\x9d and it\xe2\x80\x99s",
                    out, sizeof out);
    check(strcmp(out, "\"quoted\" and it's") == 0,
          "curly quotes become straight ones");

    gs_str_to_ascii("wait\xe2\x80\xa6", out, sizeof out);
    check(strcmp(out, "wait...") == 0, "an ellipsis becomes three dots");

    gs_str_to_ascii("\xe2\x80\xa2 one", out, sizeof out);
    check(strcmp(out, "- one") == 0, "a bullet becomes a hyphen");

    gs_str_to_ascii("a \xe2\x86\x92 b, x \xe2\x89\xa4 y", out, sizeof out);
    check(strcmp(out, "a -> b, x <= y") == 0, "arrows and comparisons");

    gs_str_to_ascii("10\xc2\xa0GB", out, sizeof out);
    check(strcmp(out, "10 GB") == 0, "a no-break space becomes a space");

    /* Something with no plain spelling between two words is left out, and
     * a space keeps the words apart. */
    gs_str_to_ascii("yes\xf0\x9f\x94\x91no", out, sizeof out);
    check(strcmp(out, "yes no") == 0,
          "an emoji jammed between words leaves a space rather than joining "
          "them");
    gs_str_to_ascii("### \xf0\x9f\x94\x91 Core Idea", out, sizeof out);
    check(strcmp(out, "### Core Idea") == 0,
          "and one standing between spaces leaves a single space");

    /* A multi-part emoji carries a joiner and a selector, both invisible. */
    gs_str_to_ascii("ok \xe2\x9c\x85\xef\xb8\x8f done", out, sizeof out);
    check(strcmp(out, "ok done") == 0,
          "an emoji with a hidden selector after it leaves nothing behind");

    /* Plain text is left exactly as it was, spacing and line breaks too. */
    gs_str_to_ascii("  indented\n  code  here", out, sizeof out);
    check(strcmp(out, "  indented\n  code  here") == 0,
          "plain ASCII keeps its own spaces and line breaks");

    /* Malformed bytes are skipped rather than trusted. */
    gs_str_to_ascii("a\xff" "b\xe2\x80" "c", out, sizeof out);
    check(strchr(out, (char)0xff) == NULL, "a stray byte is skipped");
    {
        size_t i;
        int high = 0;

        for (i = 0; out[i] != '\0'; i++)
            if ((unsigned char)out[i] > 0x7e)
                high = 1;
        check(high == 0, "and nothing above 126 ever reaches the answer");
    }

    /* A cut sequence at the very end is not read past the terminator. */
    gs_str_to_ascii("end\xe2", out, sizeof out);
    check(strcmp(out, "end") == 0, "a sequence cut at the end is dropped");

    gs_str_to_ascii("abcdefghij", out, 5);
    check(strcmp(out, "abcd") == 0, "a small buffer is filled and terminated");
    gs_str_to_ascii("wait\xe2\x80\xa6", out, 6);
    check(strlen(out) < 6, "and a spelling that will not fit is not half written");
    out[0] = 'x';
    check(gs_str_to_ascii(NULL, out, sizeof out) == 0 && out[0] == '\0',
          "no text gives an empty answer");
    check(gs_str_to_ascii("x", NULL, 8) == 0, "nowhere to write gives nothing");
}

int main(void)
{
    printf("str\n\n");
    test_encode();
    test_distro();
    test_the_clock();
    test_plain_ascii();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
