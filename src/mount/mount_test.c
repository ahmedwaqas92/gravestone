/* mount_test.c
 *
 * Adversarial. Every check here works on text and on a database under
 * build/, and not one of them runs umount, mount, mkdir or sudo. This
 * test is part of `make check`, which is what a person types after every
 * edit, so a test that took a drive down would take down the drive of
 * whoever typed it.
 *
 * The reason for most of these checks is the same. A drive letter comes
 * back out of a database file the person owns, and the letter is what
 * every command is built from, so a row somebody edited has to end up
 * refused rather than running.
 */
#include "mount.h"
#include "mount_internal.h"

#include "gravestone.h"
#include "db.h"
#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void fresh(void)
{
    gs_mount_close();
    (void)system("rm -rf build/mount-test");
    gs_paths_override("build/mount-test");
}

/* One capital letter and nothing else, since the letter is what every
 * command is built from. */
static void test_letter(void)
{
    int i;
    int bad = 0;

    printf("which letters count\n");

    for (i = 'A'; i <= 'Z'; i++)
        if (!gs_mount_letter_ok((char)i))
            bad++;
    check(bad == 0, "every capital A to Z is taken");

    check(gs_mount_letter_ok('a') == 0, "a small letter is refused");
    check(gs_mount_letter_ok('z') == 0, "at both ends");
    check(gs_mount_letter_ok('0') == 0, "a digit is refused");
    check(gs_mount_letter_ok('/') == 0, "an oblique is refused");
    check(gs_mount_letter_ok('.') == 0, "a dot is refused");
    check(gs_mount_letter_ok(';') == 0, "a semicolon is refused");
    check(gs_mount_letter_ok('\0') == 0, "nothing at all is refused");
    check(gs_mount_letter_ok(' ') == 0, "a space is refused");
    check(gs_mount_letter_ok('@') == 0, "the character below A is refused");
    check(gs_mount_letter_ok('[') == 0, "and the one above Z");
}

/* The mount point is worked out from the letter every time. Nothing that
 * was stored is ever used to build a path. */
static void test_point(void)
{
    char out[32];
    int i;
    int wrong = 0;

    printf("where a letter puts a drive\n");

    check(gs_mount_point_for('D', out, sizeof out) == GS_OK &&
          strcmp(out, "/mnt/d") == 0, "D goes to /mnt/d");
    check(gs_mount_point_for('Z', out, sizeof out) == GS_OK &&
          strcmp(out, "/mnt/z") == 0, "Z goes to /mnt/z");

    for (i = 'A'; i <= 'Z'; i++) {
        char want[8];

        snprintf(want, sizeof want, "/mnt/%c", i - 'A' + 'a');
        if (gs_mount_point_for((char)i, out, sizeof out) != GS_OK ||
            strcmp(out, want) != 0)
            wrong++;
    }
    check(wrong == 0, "all twenty six work out the same way");

    out[0] = 'x';
    check(gs_mount_point_for('d', out, sizeof out) == GS_ERR_ARG &&
          out[0] == '\0', "a small letter gives nothing, and empties the answer");
    check(gs_mount_point_for('D', out, 6) == GS_ERR_ARG,
          "six bytes is one too few for /mnt/d");
    check(gs_mount_point_for('D', out, 7) == GS_OK,
          "seven is exactly enough");
    check(gs_mount_point_for('D', NULL, 32) == GS_ERR_ARG,
          "nowhere to write is refused");

    /* The pair check is what a stored row has to pass. */
    check(gs_mount_pair_ok('D', "/mnt/d") == 1, "D with /mnt/d agrees");
    check(gs_mount_pair_ok('D', "/mnt/e") == 0, "D with /mnt/e does not");
    check(gs_mount_pair_ok('D', "/mnt/d/") == 0, "a trailing oblique does not");
    check(gs_mount_pair_ok('D', "/mnt/d ") == 0, "a trailing space does not");
    check(gs_mount_pair_ok('D', "/mnt/d -o bind /etc") == 0,
          "a path carrying a second argument does not");
    check(gs_mount_pair_ok('D', "") == 0, "an empty path does not");
    check(gs_mount_pair_ok('D', NULL) == 0, "no path at all does not");
}

/* Paths that are never touched whatever the database says. */
static void test_refused(void)
{
    printf("what is never touched\n");

    check(gs_mount_point_refused("/") == 1, "the root is refused");
    check(gs_mount_point_refused("/mnt") == 1, "the whole of /mnt is refused");
    check(gs_mount_point_refused("/mnt/c") == 1,
          "the system drive is refused, since the file box runs from it");
    check(gs_mount_point_refused("/mnt/wslg") == 1,
          "and the directory holding the screen sockets");
    check(gs_mount_point_refused("/mnt/wsl") == 1, "and the one beside it");
    check(gs_mount_point_refused("") == 1, "an empty path is refused");
    check(gs_mount_point_refused(NULL) == 1, "no path at all is refused");
    check(gs_mount_point_refused("/mnt/d") == 0, "an ordinary drive is not");
    check(gs_mount_point_refused("/mnt/e") == 0, "nor another one");
}

/* The kernel writes some characters in base eight, so a Windows root
 * arrives as C:\134 and the backslash has to come back. */
static void test_unescape(void)
{
    char text[64];

    printf("undoing the kernel spelling\n");

    strcpy(text, "C:\\134");
    gs_mount_unescape(text);
    check(strcmp(text, "C:\\") == 0, "C:\\134 becomes C: and a backslash");
    check(strlen(text) == 3, "three characters, so the letter is the first");

    strcpy(text, "/mnt/my\\040disk");
    gs_mount_unescape(text);
    check(strcmp(text, "/mnt/my disk") == 0, "040 becomes a space");

    strcpy(text, "plain");
    gs_mount_unescape(text);
    check(strcmp(text, "plain") == 0, "text with nothing in it is left alone");

    strcpy(text, "a\\19b");
    gs_mount_unescape(text);
    check(strcmp(text, "a\\19b") == 0,
          "a run that is not three digits in base eight is left alone");

    strcpy(text, "trail\\13");
    gs_mount_unescape(text);
    check(strcmp(text, "trail\\13") == 0, "and one cut short at the end");

    gs_mount_unescape(NULL);
    check(1, "no text at all does not crash");
}

/* Recognising a Windows drive in the kernel mount table. Both lines below
 * are copied from a real machine. */
static void test_parse(void)
{
    gs_mount_drive_t d;

    printf("reading the kernel mount table\n");

    check(gs_mount_parse_line(
        "D: /mnt/d 9p rw,relatime,aname=drvfs;path=D:;symlinkroot=/mnt/,"
        "cache=0x5,access=client,msize=65536,trans=fd,rfd=3,wfd=3 0 0",
        &d) == 1, "a real drive line is taken");
    check(d.letter == 'D', "the letter is read");
    check(strcmp(d.point, "/mnt/d") == 0, "and the point");

    check(gs_mount_parse_line(
        "C:\\134 /mnt/c 9p rw,noatime,aname=drvfs,uid=1000,"
        "gid=1000;symlinkroot=/mnt/,cache=0x5 0 0", &d) == 1,
        "the system drive line is taken, backslash and all");
    check(d.letter == 'C', "with the letter in front of the colon");

    /* The type field says 9p on a real WSL machine, and 9p is an ordinary
     * Linux filesystem carrying real shares elsewhere, so matching on the
     * type would aim the unmount command at somebody else's share. */
    check(gs_mount_parse_line(
        "host /mnt/share 9p rw,trans=virtio,version=9p2000.L 0 0", &d) == 0,
        "a plain 9p share is left alone, since it is not drvfs");
    check(gs_mount_parse_line(
        "/dev/sda1 /mnt/d ext4 rw,relatime 0 0", &d) == 0,
        "a real disk at the same point is left alone");
    check(gs_mount_parse_line(
        "server:/export /mnt/d nfs4 rw,relatime 0 0", &d) == 0,
        "and a network share");
    check(gs_mount_parse_line(
        "proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0", &d) == 0,
        "and something the kernel invented");

    /* A folder shared in is not a whole drive, and putting one back needs
     * a path this module has no safe way to carry. */
    check(gs_mount_parse_line(
        "D:\\work /mnt/work 9p rw,aname=drvfs,path=D 0 0", &d) == 0,
        "a shared folder is left alone, since it is not a whole drive");
    check(gs_mount_parse_line(
        "DD: /mnt/dd 9p rw,aname=drvfs 0 0", &d) == 0,
        "two letters is not a drive");
    check(gs_mount_parse_line(
        "1: /mnt/1 9p rw,aname=drvfs 0 0", &d) == 0,
        "a digit is not a drive");

    check(gs_mount_parse_line("", &d) == 0, "an empty line gives nothing");
    check(gs_mount_parse_line("only two fields", &d) == 0,
          "a short line gives nothing");
    check(gs_mount_parse_line(NULL, &d) == 0, "no line at all gives nothing");
    check(gs_mount_parse_line("D: /mnt/d 9p aname=drvfs 0 0", NULL) == 0,
          "nowhere to write gives nothing");

    /* WSL version one really does name the type drvfs. */
    check(gs_mount_parse_line("E: /mnt/e drvfs rw,noatime 0 0", &d) == 1 &&
          d.letter == 'E', "the older WSL spelling is taken too");

    /* A drive mounted somewhere unusual is still worth seeing, and the
     * point is carried as written. Nothing built from it runs. */
    check(gs_mount_parse_line("F: /srv/f 9p rw,aname=drvfs 0 0", &d) == 1 &&
          strcmp(d.point, "/srv/f") == 0,
          "a drive mounted off /mnt is still read");
    check(gs_mount_pair_ok(d.letter, d.point) == 0,
          "and it fails the pair check, so nothing repairs it");
}

/* Text on its way to a person's terminal. */
static void test_plain(void)
{
    char out[64];

    printf("text that reaches a terminal\n");

    gs_mount_plain("Debian", out, sizeof out);
    check(strcmp(out, "Debian") == 0, "an ordinary name comes through");

    gs_mount_plain("a; rm -rf /", out, sizeof out);
    check(strchr(out, ';') == NULL, "a semicolon is dropped");
    check(strcmp(out, "a rm -rf /") == 0, "and the rest is left readable");

    gs_mount_plain("x`id`y", out, sizeof out);
    check(strchr(out, '`') == NULL, "a backtick is dropped");
    gs_mount_plain("x$(id)y", out, sizeof out);
    check(strchr(out, '$') == NULL, "a dollar is dropped");
    gs_mount_plain("a && b", out, sizeof out);
    check(strchr(out, '&') == NULL, "an ampersand is dropped");
    gs_mount_plain("a | b", out, sizeof out);
    check(strchr(out, '|') == NULL, "a pipe is dropped");
    gs_mount_plain("a > b", out, sizeof out);
    check(strchr(out, '>') == NULL, "a redirection is dropped");
    gs_mount_plain("one\ntwo", out, sizeof out);
    check(strchr(out, '\n') == NULL, "a line break is dropped");
    gs_mount_plain("\033[2Jgone", out, sizeof out);
    check(out[0] == '[', "an escape is dropped, so the screen is not cleared");
    gs_mount_plain("a\\b", out, sizeof out);
    check(strchr(out, '\\') == NULL, "a backslash is dropped");

    gs_mount_plain("0123456789abcdef0123456789abcdef", out, 8);
    check(strlen(out) == 7, "a long answer is cut to fit");
    gs_mount_plain(NULL, out, sizeof out);
    check(out[0] == '\0', "no text at all gives an empty answer");
}

/* The line a person is told to type. Built from the letter alone. */
static void test_hand_command(void)
{
    char out[96];

    printf("the command a person is given\n");

    check(gs_mount_hand_command('D', out, sizeof out) == GS_OK,
          "a real letter gives a command");
    check(strcmp(out, "sudo mount -t drvfs D: /mnt/d") == 0,
          "and it reads exactly as it would be typed");
    check(gs_mount_hand_command('d', out, sizeof out) == GS_ERR_ARG &&
          out[0] == '\0', "a small letter gives nothing at all");
    check(gs_mount_hand_command(';', out, sizeof out) == GS_ERR_ARG,
          "and so does a semicolon");
    check(strstr(out, "rm") == NULL, "nothing is left in the answer");
}

/* The stored list. Every row is treated as text somebody could edit. */
static void test_storage(void)
{
    gs_mount_drive_t d;
    gs_mount_drive_t back[GS_MOUNT_MAX_DRIVES];
    int n;

    printf("the list that survives a restart\n");

    fresh();
    check(gs_mount_remember(&d) == GS_ERR_ARG,
          "nothing is written while the database is closed");
    check(gs_mount_open() == GS_OK, "the database opens");
    check(gs_mount_open() == GS_OK, "and opening it twice is harmless");

    check(gs_mount_recall(back, GS_MOUNT_MAX_DRIVES) == 0,
          "a new database holds no drives");

    memset(&d, 0, sizeof d);
    d.letter = 'D';
    strcpy(d.point, "/mnt/d");
    strcpy(d.distro, "Debian");
    d.seen_at = 1700000000;
    check(gs_mount_remember(&d) == GS_OK, "a drive is written down");

    n = gs_mount_recall(back, GS_MOUNT_MAX_DRIVES);
    check(n == 1, "and comes back");
    check(back[0].letter == 'D', "with its letter");
    check(strcmp(back[0].point, "/mnt/d") == 0, "and its point");
    check(strcmp(back[0].distro, "Debian") == 0, "and the distribution");
    check(back[0].seen_at == 1700000000, "and the time it last worked");
    check(back[0].misses == 0, "and no misses against it");

    /* Writing the same letter again replaces the row rather than adding
     * one, so the list cannot grow without bound. */
    d.seen_at = 1800000000;
    check(gs_mount_remember(&d) == GS_OK, "the same drive is written again");
    n = gs_mount_recall(back, GS_MOUNT_MAX_DRIVES);
    check(n == 1, "and there is still one row");
    check(back[0].seen_at == 1800000000, "carrying the newer time");

    memset(&d, 0, sizeof d);
    d.letter = 'E';
    strcpy(d.point, "/mnt/e");
    check(gs_mount_remember(&d) == GS_OK, "a second drive is written down");
    check(gs_mount_recall(back, GS_MOUNT_MAX_DRIVES) == 2, "and both come back");

    check(gs_mount_forget('E') == GS_OK, "one is dropped");
    check(gs_mount_recall(back, GS_MOUNT_MAX_DRIVES) == 1, "leaving the other");
    check(gs_mount_forget('e') == GS_ERR_ARG, "a small letter drops nothing");
    check(gs_mount_forget('Q') == GS_OK, "dropping one never there is harmless");

    /* A row with a letter that could never be right is refused on the way
     * in, so it never reaches the file. */
    memset(&d, 0, sizeof d);
    d.letter = ';';
    strcpy(d.point, "/mnt/d");
    check(gs_mount_remember(&d) == GS_ERR_ARG, "a semicolon is not a drive");
    d.letter = 'd';
    check(gs_mount_remember(&d) == GS_ERR_ARG, "nor a small letter");
    check(gs_mount_remember(NULL) == GS_ERR_ARG, "nor nothing at all");

    /* The distribution name is a stored value that reaches a person, so
     * it is cleaned on the way in. */
    memset(&d, 0, sizeof d);
    d.letter = 'F';
    strcpy(d.point, "/mnt/f");
    strcpy(d.distro, "Deb; rm -rf /");
    check(gs_mount_remember(&d) == GS_OK, "a dirty name is still stored");
    n = gs_mount_recall(back, GS_MOUNT_MAX_DRIVES);
    check(n == 2, "and the drive comes back");
    {
        int i;
        int found = 0;

        for (i = 0; i < n; i++)
            if (back[i].letter == 'F')
                found = strchr(back[i].distro, ';') == NULL;
        check(found, "with the semicolon gone from the name");
    }

    check(gs_mount_recall(NULL, 4) == GS_ERR_ARG, "nowhere to write is refused");
    check(gs_mount_recall(back, 0) == GS_ERR_ARG, "and no room at all");

    gs_mount_close();
    check(gs_mount_open() == GS_OK, "the database opens again");
    check(gs_mount_recall(back, GS_MOUNT_MAX_DRIVES) == 2,
          "and the drives survived the close");
    gs_mount_close();
}

/* A row somebody edited by hand. The point stored has to match the one
 * the letter works out to, and a row failing that is dropped whole rather
 * than repaired, since the only thing that writes one is an editor. */
static void test_edited_row(void)
{
    gs_mount_drive_t back[GS_MOUNT_MAX_DRIVES];
    gs_mount_drive_t d;
    char path[512];
    gs_db_t *raw;

    printf("a row somebody edited\n");

    fresh();
    check(gs_mount_open() == GS_OK, "the database opens");
    memset(&d, 0, sizeof d);
    d.letter = 'D';
    strcpy(d.point, "/mnt/d");
    check(gs_mount_remember(&d) == GS_OK, "a good drive is written down");
    check(gs_mount_recall(back, GS_MOUNT_MAX_DRIVES) == 1, "and comes back");
    gs_mount_close();

    check(gs_paths_db_file(path, sizeof path) == GS_OK, "the file is found");
    raw = gs_db_open(path);
    check(raw != NULL, "and opens for editing");
    if (raw == NULL)
        return;

    /* The point is rewritten to carry a second argument. Nothing in it is
     * a shell character, so a check that only looked for those would let
     * it through, and the words would still reach mount as arguments of
     * their own. */
    check(gs_db_exec(raw,
        "UPDATE mount_drive SET point = '/mnt/d -o bind /etc'"
        " WHERE letter = 'D';") == GS_OK, "the point is rewritten by hand");
    gs_db_close(raw);

    check(gs_mount_open() == GS_OK, "the database opens again");
    check(gs_mount_recall(back, GS_MOUNT_MAX_DRIVES) == 0,
          "and the edited row is dropped rather than used");
    gs_mount_close();

    /* Now the letter itself. */
    raw = gs_db_open(path);
    if (raw != NULL) {
        check(gs_db_exec(raw,
            "UPDATE mount_drive SET point = '/mnt/d',"
            " letter = '; rm -rf /' WHERE letter = 'D';") == GS_OK,
            "the letter is rewritten into a command");
        gs_db_close(raw);
    }
    check(gs_mount_open() == GS_OK, "the database opens again");
    check(gs_mount_recall(back, GS_MOUNT_MAX_DRIVES) == 0,
          "and a row whose letter is a command is dropped too");
    gs_mount_close();

    /* A letter of the right length that is still not a drive. */
    raw = gs_db_open(path);
    if (raw != NULL) {
        (void)gs_db_exec(raw, "DELETE FROM mount_drive;");
        (void)gs_db_exec(raw,
            "INSERT INTO mount_drive (letter, point, distro, seen_at, misses)"
            " VALUES ('*', '/mnt/*', '', 1, 0);");
        (void)gs_db_exec(raw,
            "INSERT INTO mount_drive (letter, point, distro, seen_at, misses)"
            " VALUES ('d', '/mnt/d', '', 1, 0);");
        (void)gs_db_exec(raw,
            "INSERT INTO mount_drive (letter, point, distro, seen_at, misses)"
            " VALUES ('', '/mnt/d', '', 1, 0);");
        (void)gs_db_exec(raw,
            "INSERT INTO mount_drive (letter, point, distro, seen_at, misses)"
            " VALUES ('E', '/etc', '', 1, 0);");
        (void)gs_db_exec(raw,
            "INSERT INTO mount_drive (letter, point, distro, seen_at, misses)"
            " VALUES ('F', '/mnt/f', '', 1, 0);");
        gs_db_close(raw);
    }
    check(gs_mount_open() == GS_OK, "the database opens once more");
    {
        int n = gs_mount_recall(back, GS_MOUNT_MAX_DRIVES);

        check(n == 1, "one row of five survives the checking");
        check(n == 1 && back[0].letter == 'F',
              "and it is the only one that was ever right");
        check(n == 1 && strcmp(back[0].point, "/mnt/f") == 0,
              "carrying the point its letter works out to");
    }
    gs_mount_close();
}

/* Naming the outcomes, and which of them a person hears about. */
static void test_names(void)
{
    int i;
    int unnamed = 0;
    int warnings = 0;

    printf("what each answer is called\n");

    for (i = GS_MOUNT_OK_ALREADY; i <= GS_MOUNT_FAILED; i++) {
        const char *name = gs_mount_outcome_name((gs_mount_outcome_t)i);

        if (name == NULL || strcmp(name, "unknown") == 0)
            unnamed++;
        if (gs_mount_outcome_is_warning((gs_mount_outcome_t)i))
            warnings++;
    }
    check(unnamed == 0, "every outcome has a name");
    check(warnings == 6, "six of the nine are worth telling somebody about");

    check(gs_mount_outcome_is_warning(GS_MOUNT_OK_ALREADY) == 0,
          "a drive already there is silent");
    check(gs_mount_outcome_is_warning(GS_MOUNT_OK_REMOUNTED) == 0,
          "and one put back successfully");
    check(gs_mount_outcome_is_warning(GS_MOUNT_SKIPPED) == 0,
          "and a machine with nothing to do");
    check(gs_mount_outcome_is_warning(GS_MOUNT_GONE) == 1,
          "a drive that has left is worth saying");
    check(gs_mount_outcome_is_warning(GS_MOUNT_NO_RIGHTS) == 1,
          "and one that needed a password");
    check(gs_mount_outcome_is_warning(GS_MOUNT_BUSY) == 1,
          "and one something is holding open");
    check(gs_mount_outcome_is_warning(GS_MOUNT_DETACHED) == 1,
          "and one taken down with nothing put back");

    check(strcmp(gs_mount_state_name(GS_MOUNT_HEALTHY), "healthy") == 0,
          "a working drive is called healthy");
    check(strcmp(gs_mount_state_name(GS_MOUNT_STALE), "stale") == 0,
          "and a dead one stale");
    check(strcmp(gs_mount_state_name((gs_mount_state_t)99), "unknown") == 0,
          "and anything else unknown");
}

/* Nothing at all happens off a Windows machine, whatever is written down.
 * A database carried from a WSL laptop onto a Raspberry Pi in a home
 * backup holds rows naming drives, and a real disk mounted at the same
 * path on the Pi must never be taken down. */
static void test_does_nothing_elsewhere(void)
{
    char note[64];
    char why[256];

    printf("a machine with no Windows under it\n");

    if (gs_mount_under_wsl()) {
        printf("        this machine is WSL, so the refusals are the ones "
               "that can be checked here\n");
        check(gs_mount_restore('a', why, sizeof why) == GS_MOUNT_SKIPPED,
              "a small letter is skipped");
        check(gs_mount_restore(';', why, sizeof why) == GS_MOUNT_SKIPPED,
              "and a semicolon");
        check(why[0] == '\0', "with nothing said about either");
        return;
    }

    fresh();
    check(gs_mount_open() == GS_OK, "the database opens");
    {
        gs_mount_drive_t d;

        memset(&d, 0, sizeof d);
        d.letter = 'D';
        strcpy(d.point, "/mnt/d");
        gs_mount_remember(&d);
    }
    check(gs_mount_restore('D', why, sizeof why) == GS_MOUNT_SKIPPED,
          "a written down drive is skipped");
    check(gs_mount_restore_all(note, sizeof note) == 0, "and so is the lot");
    check(note[0] == '\0', "with nothing said");
    check(gs_mount_keep_reading() == 0, "and nothing new written down");
    gs_mount_close();
}

/* The reading of this machine, whatever machine it is. Nothing here
 * changes anything. */
static void test_reading(void)
{
    gs_mount_drive_t live[GS_MOUNT_MAX_DRIVES];
    int n;
    int i;
    int malformed = 0;

    printf("what this machine shows\n");

    n = gs_mount_read(live, GS_MOUNT_MAX_DRIVES);
    check(n >= 0, "the mount table is readable");
    printf("        %d Windows drive%s in the table\n", n,
           n == 1 ? "" : "s");

    for (i = 0; i < n; i++) {
        if (!gs_mount_letter_ok(live[i].letter))
            malformed++;
        if (live[i].point[0] != '/')
            malformed++;
        printf("        %c: at %-10s %s\n", live[i].letter, live[i].point,
               gs_mount_answers(live[i].point) ? "answers" : "does not answer");
    }
    check(malformed == 0, "every drive read carries a real letter and path");

    check(gs_mount_read(NULL, 4) == GS_ERR_ARG, "nowhere to write is refused");
    check(gs_mount_read(live, 0) == GS_ERR_ARG, "and no room at all");

    /* The refusal list has to cover whatever the system drive is here. */
    for (i = 0; i < n; i++)
        if (live[i].letter == 'C')
            check(gs_mount_point_refused(live[i].point) == 1,
                  "the system drive on this machine is on the refusal list");
}

/* Who is holding a mount open. A dead mount somebody is still reading is
 * never taken down, because a model file is mapped into memory rather
 * than read and pulling the drive out from under it kills the program
 * holding it. A holder walk that silently found nobody would turn that
 * guard off without anything failing, so it is checked against a
 * directory this test is certainly holding itself. */
static void test_holders(void)
{
    char who[160];
    char here[512];
    int n;

    printf("who is holding a mount open\n");

    check(gs_mount_holders(NULL, who, sizeof who) == 0,
          "no path at all finds nobody");
    check(gs_mount_holders("", who, sizeof who) == 0,
          "and an empty one");
    check(who[0] == '\0', "with nothing named");

    check(gs_mount_holders("/mnt/this-was-never-mounted", who, sizeof who) == 0,
          "a path nothing is mounted at finds nobody");

    /* This test is running with its own working directory somewhere, and
     * a working directory is one of the things the walk looks at. */
    if (getcwd(here, sizeof here) == NULL) {
        check(0, "the working directory is readable");
        return;
    }
    n = gs_mount_holders(here, who, sizeof who);
    check(n > 0, "the directory this test runs in has a holder");
    check(who[0] != '\0', "and the holder is named");
    printf("        %d holder(s) of %s, first %s\n", n, here, who);

    /* The name reaches a person, so it carries nothing a terminal reads. */
    check(strchr(who, ';') == NULL, "the name carries no semicolon");
    check(strchr(who, '`') == NULL, "nor a backtick");
    check(strchr(who, '\n') == NULL, "nor a line break");

    /* The walk must not match a path that merely starts with the same
     * letters, since /mnt/d and /mnt/data are different mounts. */
    {
        char longer[600];

        snprintf(longer, sizeof longer, "%s-not-a-real-directory", here);
        check(gs_mount_holders(longer, who, sizeof who) == 0,
              "a longer path sharing the same start finds nobody");
    }
}

/* An empty directory nobody has mounted anything on. This is what /mnt/d
 * looks like once the drive behind it has gone and the dead row has been
 * cleared, and asking the filesystem for its size answers perfectly well
 * because the answer describes whatever filesystem the path sits on. A
 * health check built on that question alone reports the size of the
 * system disk and reads as a drive that is working. */
static void test_a_bare_directory_is_not_a_mount(void)
{
    printf("telling a mount from an ordinary directory\n");

    check(gs_mount_is_mounted("/") == 0,
          "the root shares its device with itself, so it reads as no mount");
    check(gs_mount_is_mounted("/mnt/this-was-never-mounted") == 0,
          "a path that does not exist is no mount");
    check(gs_mount_is_mounted(NULL) == 0, "no path at all is no mount");
    check(gs_mount_is_mounted("") == 0, "an empty path is no mount");

    /* A directory made here and nothing put on it. */
    (void)system("rm -rf build/mount-bare && mkdir -p build/mount-bare/d");
    check(gs_mount_is_mounted("build/mount-bare/d") == 0,
          "a directory just made is no mount");
    check(gs_mount_answers("build/mount-bare/d") == 0,
          "and it does not count as a drive that answers");

    /* The system drive is a real mount on a machine that has one. */
    if (gs_mount_is_mounted("/mnt/c")) {
        check(gs_mount_answers("/mnt/c") == 1,
              "the system drive is a real mount and answers");
        check(gs_mount_is_mounted("/mnt") == 0,
              "while the directory holding it is not itself a mount");
    } else {
        printf("  skip  the system drive (nothing mounted at /mnt/c here)\n");
    }
    (void)system("rm -rf build/mount-bare");
}

/* Reading the answer the Windows tool gives. The word Drives ends in an
 * s standing in front of a colon, so a reader taking any letter before a
 * colon reports a drive S the machine has never had. That happened once
 * and this is what stops it happening again. */
static void test_reading_the_drive_list(void)
{
    char out[32];

    printf("reading what the Windows tool printed\n");

    check(gs_mount_read_drive_list("\r\nDrives: C:\\ D:\\ \r\n", out,
                                   sizeof out) == GS_OK,
          "a real answer is read");
    check(strcmp(out, "CD") == 0,
          "giving C and D, with the s of Drives left alone");

    check(gs_mount_read_drive_list("Drives: C:\\ ", out, sizeof out) == GS_OK &&
          strcmp(out, "C") == 0, "one drive gives one letter");
    check(gs_mount_read_drive_list("Drives: ", out, sizeof out) == GS_OK &&
          out[0] == '\0', "no drives gives nothing");
    check(gs_mount_read_drive_list("", out, sizeof out) == GS_OK &&
          out[0] == '\0', "an empty answer gives nothing");

    /* A letter and a colon with no backslash is not a drive. */
    check(gs_mount_read_drive_list("Drives: C: D:", out, sizeof out) == GS_OK &&
          out[0] == '\0', "a letter and a colon alone is not a drive");
    /* A letter joined to the word in front of it is part of that word. */
    check(gs_mount_read_drive_list("xyzD:\\", out, sizeof out) == GS_OK &&
          out[0] == '\0', "a letter inside a word is not a drive");

    check(gs_mount_read_drive_list("Drives: c:\\ ", out, sizeof out) == GS_OK &&
          strcmp(out, "C") == 0, "a small letter is read as its capital");

    /* Every drive a machine could have. */
    check(gs_mount_read_drive_list(
        " A:\\ B:\\ C:\\ D:\\ E:\\ F:\\ G:\\ H:\\ I:\\ J:\\ K:\\ L:\\ M:\\"
        " N:\\ O:\\ P:\\ Q:\\ R:\\ S:\\ T:\\ U:\\ V:\\ W:\\ X:\\ Y:\\ Z:\\",
        out, sizeof out) == GS_OK, "a full alphabet is read");
    check(strlen(out) == 26, "giving twenty six letters");

    check(gs_mount_read_drive_list("Drives: C:\\ D:\\ E:\\", out, 3) == GS_OK &&
          strlen(out) == 2, "a small answer is cut to fit rather than overrun");

    check(gs_mount_read_drive_list(NULL, out, sizeof out) == GS_ERR_ARG,
          "no text at all is refused");
    check(gs_mount_read_drive_list("Drives: C:\\", NULL, 8) == GS_ERR_ARG,
          "nowhere to write is refused");
}

/* Which drives Windows has. A drive plugged in after this Linux started
 * is in no mount table, so the only way to learn it is there is to ask
 * Windows, and the answer is a line of text that has to be read
 * carefully. */
static void test_windows_letters(void)
{
    char letters[GS_MOUNT_MAX_DRIVES + 1];
    int rc;
    int i;

    printf("which drives Windows has\n");

    check(gs_mount_windows_letters(NULL, 8) == GS_ERR_ARG,
          "nowhere to write is refused");
    check(gs_mount_windows_letters(letters, 0) == GS_ERR_ARG,
          "and no room at all");

    rc = gs_mount_windows_letters(letters, sizeof letters);
    if (rc != GS_OK) {
        printf("  skip  the drive list (this machine cannot ask Windows)\n");
        return;
    }
    printf("        Windows names %lu drive(s): %s\n",
           (unsigned long)strlen(letters), letters);

    for (i = 0; letters[i] != '\0'; i++)
        if (!gs_mount_letter_ok(letters[i]))
            break;
    check(letters[i] == '\0', "every letter named is a capital A to Z");

    check(strlen(letters) <= GS_MOUNT_MAX_DRIVES,
          "no more letters than there are in the alphabet");

    /* Whatever Windows named, the system drive is in the list and is
     * still refused, so discovery never aims at it. */
    if (strchr(letters, 'C') != NULL)
        check(gs_mount_point_refused("/mnt/c") == 1,
              "the system drive is named and still refused");
}

/* Turning the box down has to stick, or it comes back at every start and
 * the person cannot get past it. */
static void test_declining(void)
{
    gs_mount_drive_t back[GS_MOUNT_MAX_DRIVES];

    printf("turning the box down\n");

    fresh();
    check(gs_mount_open() == GS_OK, "the database opens");
    check(gs_mount_declined('D') == 0, "a drive never asked about is not declined");

    check(gs_mount_decline('D') == GS_OK, "the drive is turned down");
    check(gs_mount_declined('D') == 1, "and that is remembered");
    check(gs_mount_declined('E') == 0, "while another drive is untouched");

    check(gs_mount_decline('d') == GS_ERR_ARG, "a small letter declines nothing");
    check(gs_mount_decline(';') == GS_ERR_ARG, "and neither does a semicolon");
    check(gs_mount_declined(';') == 0, "which is not remembered either");

    /* A refusal survives the program closing, since that is the whole
     * point of writing it down. */
    gs_mount_close();
    check(gs_mount_open() == GS_OK, "the database opens again");
    check(gs_mount_declined('D') == 1, "and the refusal survived");

    /* A drive that later works is written down as working, and the
     * refusal stops mattering once the drive is up. */
    {
        gs_mount_drive_t d;

        memset(&d, 0, sizeof d);
        d.letter = 'D';
        strcpy(d.point, "/mnt/d");
        check(gs_mount_remember(&d) == GS_OK, "the drive is written down");
        check(gs_mount_recall(back, GS_MOUNT_MAX_DRIVES) == 1,
              "and comes back as an ordinary row");
    }
    gs_mount_close();
}

/* The argument list that carries a command down a road to root. Built
 * here without running anything, since the whole point of it is that no
 * shell ever reads it and every piece arrives whole. */
static void test_the_road_to_root(void)
{
    const char *argv[16];
    const char *tail[2];
    int n;

    printf("the road to root\n");

    tail[0] = "/mnt/d";
    n = gs_mount_root_argv(GS_MOUNT_ROAD_WSL, "/mnt/c/Windows/System32/wsl.exe",
                           "Debian", "/usr/bin/umount", tail, 1, argv, 16);
    check(n == 8, "the wsl road carries eight words");
    check(n == 8 && strcmp(argv[0], "/mnt/c/Windows/System32/wsl.exe") == 0 &&
          strcmp(argv[1], "-d") == 0 && strcmp(argv[2], "Debian") == 0 &&
          strcmp(argv[3], "-u") == 0 && strcmp(argv[4], "root") == 0 &&
          strcmp(argv[5], "-e") == 0,
          "naming the distribution, the user root, and a straight run");
    check(n == 8 && strcmp(argv[6], "/usr/bin/umount") == 0 &&
          strcmp(argv[7], "/mnt/d") == 0 && argv[8] == NULL,
          "then the program and its argument, ended by nothing");

    tail[0] = "-t";
    tail[1] = "drvfs";
    n = gs_mount_root_argv(GS_MOUNT_ROAD_SUDO, "/usr/bin/sudo", NULL,
                           "/usr/bin/mount", tail, 2, argv, 16);
    check(n == 5 && strcmp(argv[0], "/usr/bin/sudo") == 0 &&
          strcmp(argv[1], "-n") == 0 && strcmp(argv[2], "/usr/bin/mount") == 0,
          "the sudo road is sudo, then the form that never asks");
    check(n == 5 && strcmp(argv[4], "drvfs") == 0 && argv[5] == NULL,
          "with the arguments after and nothing else");

    n = gs_mount_root_argv(GS_MOUNT_ROAD_SELF, NULL, NULL, "/usr/bin/mount",
                           tail, 2, argv, 16);
    check(n == 3 && strcmp(argv[0], "/usr/bin/mount") == 0,
          "already root, the program runs as it is");

    /* Every refusal. */
    check(gs_mount_root_argv(GS_MOUNT_ROAD_WSL, "/x/wsl.exe", NULL,
                             "/usr/bin/mount", tail, 2, argv, 16) < 0,
          "the wsl road without a distribution name is refused");
    check(gs_mount_root_argv(GS_MOUNT_ROAD_WSL, "/x/wsl.exe", "",
                             "/usr/bin/mount", tail, 2, argv, 16) < 0,
          "and with an empty one");
    check(gs_mount_root_argv(GS_MOUNT_ROAD_WSL, NULL, "Debian",
                             "/usr/bin/mount", tail, 2, argv, 16) < 0,
          "and without the helper");
    check(gs_mount_root_argv(GS_MOUNT_ROAD_WSL, "wsl.exe", "Debian",
                             "/usr/bin/mount", tail, 2, argv, 16) < 0,
          "a helper named without a path is refused");
    check(gs_mount_root_argv(GS_MOUNT_ROAD_SUDO, "/usr/bin/sudo", NULL,
                             "mount", tail, 2, argv, 16) < 0,
          "and so is a program named without a path");
    check(gs_mount_root_argv(GS_MOUNT_ROAD_NONE, NULL, NULL, "/usr/bin/mount",
                             tail, 2, argv, 16) < 0, "no road builds nothing");
    check(gs_mount_root_argv(GS_MOUNT_ROAD_WSL, "/x/wsl.exe", "Debian",
                             "/usr/bin/mount", tail, 2, argv, 8) < 0,
          "a list too small for the road is refused rather than cut");
    check(gs_mount_root_argv(GS_MOUNT_ROAD_SELF, NULL, NULL, "/usr/bin/mount",
                             NULL, 2, argv, 16) < 0,
          "arguments promised and not given are refused");
    check(gs_mount_root_argv(GS_MOUNT_ROAD_SELF, NULL, NULL, "/usr/bin/mount",
                             tail, 0, argv, 16) == 1,
          "and none promised is fine");

    /* The answer is looked for among the lines, since wsl.exe writes
     * warnings of its own down the same pipe, two bytes a character. */
    {
        static const char wide[] =
            "w\0s\0l\0:\0 \0P\0r\0o\0c\0e\0s\0s\0i\0n\0g\0 \0"
            "f\0s\0t\0a\0b\0 \0f\0a\0i\0l\0e\0d\0.\0\r\0\n\0" "0\n";
        static const char plain[] =
            "wsl: Processing /etc/fstab with mount -a failed.\r\n0\n";
        char buf[64];

        check(gs_mount_line_is_nought("0\n", 2), "a bare nought is root");
        check(gs_mount_line_is_nought("0", 1), "with or without its newline");
        check(gs_mount_line_is_nought("0\r\n", 3), "and with a Windows one");
        check(gs_mount_line_is_nought(plain, sizeof plain),
              "a warning printed first does not hide it");
        check(gs_mount_line_is_nought(wide, sizeof wide),
              "nor does a warning printed two bytes a character");
        check(!gs_mount_line_is_nought("1000\n", 5), "an ordinary user is not");
        check(!gs_mount_line_is_nought("10\n", 3), "nor is ten");
        check(!gs_mount_line_is_nought("", 0), "nor is nothing");
        check(!gs_mount_line_is_nought(NULL, 8), "nor is no text");
        memset(buf, 0, sizeof buf);
        memcpy(buf, "0\n", 2);
        check(gs_mount_line_is_nought(buf, sizeof buf),
              "a zeroed buffer after the answer is stepped over");
        memset(buf, 0, sizeof buf);
        memcpy(buf, "0\n1000\n", 7);
        check(gs_mount_line_is_nought(buf, sizeof buf),
              "and the nought counts wherever it stands");
    }

    /* On this machine. The probe runs a command as root through wsl.exe
     * or sudo, which make check never does on its own, so it runs only
     * for whoever asks by setting GS_TEST_ROAD. Windows grants the owner
     * of a distribution root inside it with nothing asked, and that is
     * the road expected under WSL. */
    if (getenv("GS_TEST_ROAD") != NULL) {
        const char *helper = NULL;
        gs_mount_road_t road = gs_mount_root_road(&helper);

        printf("        road %d, helper %s\n", (int)road,
               helper != NULL ? helper : "(none)");
        if (gs_mount_under_wsl() && geteuid() != 0) {
            check(road == GS_MOUNT_ROAD_WSL,
                  "under WSL the road is wsl.exe, with nothing asked");
            check(helper != NULL && strstr(helper, "wsl.exe") != NULL,
                  "carried by wsl.exe");
            check(gs_mount_root_free() == 1, "so root is free");
            check(gs_mount_root_settled() == 1, "and settled");
            check(gs_mount_wants_setup() == '\0',
                  "and no drive wants the password box");
        } else {
            printf("  skip  the road on this machine (not WSL)\n");
        }
        if (gs_mount_root_settled())
            check(gs_mount_root_road(&helper) == road,
                  "asked again, the same road comes back without looking");
    } else {
        printf("  skip  the live road (set GS_TEST_ROAD=1 to run it)\n");
        check(gs_mount_wants_setup() == '\0',
              "before the road is known no drive wants the box");
    }
}

/* What sits at the mount point decides whether it may be mounted over,
 * and a directory that cannot be read counts as full. */
static void test_what_sits_at_the_point(void)
{
    const char *dir = "build/mount-test/point";
    int exists = 0;

    printf("what sits at the point\n");

    (void)system("rm -rf build/mount-test/point");
    (void)system("mkdir -p build/mount-test/point");
    check(gs_mount_dir_at(dir, &exists) && exists, "a made directory is there");
    check(!gs_mount_dir_has_content(dir), "and holds nothing");
    check(!gs_mount_is_mounted(dir), "and is no mount of anything");
    check(!gs_mount_dir_has_content("build/mount-test/never-made"),
          "a directory that is not there holds nothing");

    (void)system("touch build/mount-test/point/file");
    check(gs_mount_dir_has_content(dir), "one file makes it full");
    (void)system("rm -f build/mount-test/point/file");

    if (geteuid() != 0) {
        (void)system("chmod 000 build/mount-test/point");
        check(gs_mount_dir_has_content(dir),
              "a directory that refuses to be read counts as full");
        (void)system("chmod 755 build/mount-test/point");
    } else {
        printf("  skip  the unreadable directory (root reads anything)\n");
    }
    (void)system("rm -rf build/mount-test/point");
}

int main(void)
{
    test_letter();
    test_point();
    test_refused();
    test_unescape();
    test_parse();
    test_plain();
    test_hand_command();
    test_storage();
    test_edited_row();
    test_declining();
    test_names();
    test_does_nothing_elsewhere();
    test_reading();
    test_holders();
    test_what_sits_at_the_point();
    test_the_road_to_root();
    test_a_bare_directory_is_not_a_mount();
    test_reading_the_drive_list();
    test_windows_letters();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
