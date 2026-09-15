/* ui_paint_test.c
 *
 * Everything here runs against a plain memory buffer, since composing the
 * canvas is pure arithmetic and needs no screen.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "log.h"
#include "str.h"
#include "mount.h"
#include "mount_internal.h"
#include "window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GUARD 64
#define GUARD_VALUE 0xDEADBEEFu

static int failures;
static int checks;

/* One state with nothing mounted and one with a device recorded, so the
 * bar can be checked in both conditions. */
static gs_ui_state_t off;
static gs_ui_state_t on;

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

static int lum(unsigned int c)
{
    return (int)((c >> 16) & 0xffu) * 30 + (int)((c >> 8) & 0xffu) * 59 +
           (int)(c & 0xffu) * 11;
}

static int red(unsigned int c)   { return (int)((c >> 16) & 0xffu); }
static int green(unsigned int c) { return (int)((c >> 8) & 0xffu); }
static int blue(unsigned int c)  { return (int)(c & 0xffu); }

/* Sum of the per channel differences between two colours. */
static int colour_gap(unsigned int a, unsigned int b)
{
    int dr = red(a) - red(b);
    int dg = green(a) - green(b);
    int db = blue(a) - blue(b);

    return (dr < 0 ? -dr : dr) + (dg < 0 ? -dg : dg) + (db < 0 ? -db : db);
}

/* Allocates a buffer with a fence either side, so a stray write past the
 * edge shows up rather than corrupting something quietly. */
static unsigned int *fenced_alloc(int w, int h, unsigned int **base_out)
{
    size_t n = (size_t)w * (size_t)h;
    unsigned int *base = malloc((n + 2 * GUARD) * sizeof *base);
    size_t i;

    if (base == NULL)
        return NULL;
    for (i = 0; i < n + 2 * GUARD; i++)
        base[i] = GUARD_VALUE;
    *base_out = base;
    return base + GUARD;
}

static int fence_intact(const unsigned int *base, int w, int h)
{
    size_t n = (size_t)w * (size_t)h;
    size_t i;

    for (i = 0; i < GUARD; i++)
        if (base[i] != GUARD_VALUE)
            return 0;
    for (i = 0; i < GUARD; i++)
        if (base[GUARD + n + i] != GUARD_VALUE)
            return 0;
    return 1;
}

static void test_palette_and_bounds(void)
{
    const int w = 980, h = 640;
    unsigned int *base, *px;
    size_t n = (size_t)w * (size_t)h;
    size_t i;
    int neutral = 1;
    int widest = 0;
    int top_byte_clear = 1;

    printf("palette and memory safety at %dx%d\n", w, h);

    px = fenced_alloc(w, h, &base);
    check(px != NULL, "buffer allocated");
    if (px == NULL)
        return;

    gs_ui_compose(px, w, h, &off);
    check(fence_intact(base, w, h), "nothing written outside the buffer");

    for (i = 0; i < n; i++) {
        unsigned int c = px[i];
        int row = (int)(i / (size_t)w);
        int hi, lo;

        if (c > 0x00ffffffu)
            top_byte_clear = 0;

        /* The status lamp inside the bar is the one thing meant to carry
         * colour, so neutrality is checked below the bar. */
        if (row < GS_UI_BAR_HEIGHT)
            continue;

        hi = red(c) > green(c) ? red(c) : green(c);
        lo = red(c) < green(c) ? red(c) : green(c);
        if (blue(c) > hi) hi = blue(c);
        if (blue(c) < lo) lo = blue(c);
        if (hi - lo > widest)
            widest = hi - lo;
        if (hi - lo > 20)
            neutral = 0;
    }
    check(top_byte_clear, "every pixel keeps its top byte clear");
    printf("        widest gap between channels below the bar is %d\n",
           widest);
    check(neutral, "nothing below the bar carries a colour cast");

    free(base);
}

static void test_lamp(void)
{
    const int w = 980, h = 640;
    size_t n = (size_t)w * (size_t)h;
    unsigned int *dark = malloc(n * sizeof *dark);
    unsigned int *lit = malloc(n * sizeof *lit);
    int lx = w - GS_UI_MARGIN - 8;
    int ly = GS_UI_BAR_HEIGHT / 2;
    unsigned int a, b;

    printf("the status lamp\n");
    if (dark == NULL || lit == NULL) {
        free(dark); free(lit);
        check(0, "buffers allocated");
        return;
    }

    gs_ui_compose(dark, w, h, &off);
    gs_ui_compose(lit, w, h, &on);
    a = dark[(size_t)ly * (size_t)w + (size_t)lx];
    b = lit[(size_t)ly * (size_t)w + (size_t)lx];

    printf("        reads 0x%06x unmounted and 0x%06x mounted\n", a, b);
    check(a != b, "the lamp changes when a device is mounted");
    check(green(b) > red(b) + 40, "the mounted lamp leans green");
    check(red(a) > green(a) + 40, "the unmounted lamp leans red");

    free(dark);
    free(lit);
}

static void test_layout(void)
{
    const int w = 980, h = 640;
    unsigned int *base, *px;
    int cx = w / 2;
    int scan_y, grid_x, grid_y;
    int darkest = 255;
    int widest_edge_gap = 0;
    int x, y;

    printf("layout at %dx%d\n", w, h);

    px = fenced_alloc(w, h, &base);
    if (px == NULL) {
        check(0, "buffer allocated");
        return;
    }
    /* The ground on its own. The bar and the panes are drawn over it and
     * are checked elsewhere, so painting them here would hide the very
     * thing these checks are about. */
    gs_ui_compose_surface(px, w, h);

#define AT(xx, yy) px[(size_t)(yy) * (size_t)w + (size_t)(xx)]

    /* Nothing below the bar may be black, which is what a border around
     * the surface would look like. */
    for (y = GS_UI_BAR_HEIGHT; y < h; y++) {
        for (x = 0; x < w; x++) {
            unsigned int c = AT(x, y);
            int lo = red(c) < green(c) ? red(c) : green(c);

            if (blue(c) < lo)
                lo = blue(c);
            if (lo < darkest)
                darkest = lo;
        }
    }
    printf("        darkest channel below the bar is %d\n", darkest);
    check(darkest >= 15, "no pixel is black, so no dark border exists");

    /* The very edge has to match the surface a little further in. */
    for (y = GS_UI_BAR_HEIGHT + 4; y < h; y += 7) {
        int g1 = colour_gap(AT(0, y), AT(60, y));
        int g2 = colour_gap(AT(w - 1, y), AT(w - 61, y));

        if (g1 > widest_edge_gap) widest_edge_gap = g1;
        if (g2 > widest_edge_gap) widest_edge_gap = g2;
    }
    for (x = 0; x < w; x += 7) {
        int g = colour_gap(AT(x, h - 1), AT(x, h - 61));

        if (g > widest_edge_gap) widest_edge_gap = g;
    }
    printf("        widest gap between an edge pixel and the surface 60 "
           "pixels in is %d\n", widest_edge_gap);
    check(widest_edge_gap < 30,
          "the edges carry the same surface as the middle");

    check(lum(AT(cx, h - 3)) > lum(AT(cx, GS_UI_BAR_HEIGHT + 20)),
          "surface runs lighter towards the bottom");

    /* Scanlines land on rows divisible by the step, so both neighbours of
     * such a row have to be lighter than it. */
    scan_y = GS_UI_BAR_HEIGHT + 40;
    while (scan_y % GS_UI_SCANLINE_STEP != 0)
        scan_y++;
    check(lum(AT(cx, scan_y)) < lum(AT(cx, scan_y - 1)) &&
          lum(AT(cx, scan_y)) < lum(AT(cx, scan_y + 1)),
          "a scanline row is darker than the rows either side of it");

    /* A rule across the surface would show as one row far brighter than
     * the rows around it. */
    {
        int worst = 0, worst_row = 0;

        for (y = GS_UI_BAR_HEIGHT + GS_UI_SCANLINE_STEP;
             y < h - GS_UI_SCANLINE_STEP; y++) {
            int here = lum(AT(cx, y));
            int above = lum(AT(cx, y - GS_UI_SCANLINE_STEP));
            int below = lum(AT(cx, y + GS_UI_SCANLINE_STEP));
            int lower = above < below ? above : below;

            if (here - lower > worst) {
                worst = here - lower;
                worst_row = y;
            }
        }
        printf("        brightest upward jump between rows is %d at row %d\n",
               worst, worst_row);
        check(worst < 250, "no horizontal rule runs across the surface");
    }

    /* A grid dot sits where both coordinates divide by the step. */
    grid_x = GS_UI_GRID_STEP * 8;
    grid_y = GS_UI_GRID_STEP * 8;
    while (grid_y < GS_UI_BAR_HEIGHT + GS_UI_GRID_STEP)
        grid_y += GS_UI_GRID_STEP;
    check(lum(AT(grid_x, grid_y)) > lum(AT(grid_x + 3, grid_y)) + 100,
          "a grid dot is brighter than the surface beside it");

    /* Nothing bright may sit near a corner below the bar. */
    {
        int brightest = 0;

        for (y = GS_UI_BAR_HEIGHT + 2; y < GS_UI_BAR_HEIGHT + 60; y++)
            for (x = 0; x < 60; x++) {
                int v = lum(AT(x, y));
                if (v > brightest)
                    brightest = v;
            }
        printf("        brightest pixel under the bar is %d, the surface "
               "there is %d\n", brightest, lum(AT(5, GS_UI_BAR_HEIGHT + 5)));
        check(brightest < lum(AT(5, GS_UI_BAR_HEIGHT + 5)) + 2000,
              "no corner mark remains, only the grid");
    }

#undef AT
    free(base);
}

static void test_determinism(void)
{
    const int w = 400, h = 300;
    size_t n = (size_t)w * (size_t)h;
    unsigned int *a = malloc(n * sizeof *a);
    unsigned int *b = malloc(n * sizeof *b);

    printf("determinism\n");
    if (a == NULL || b == NULL) {
        check(0, "buffers allocated");
        free(a); free(b);
        return;
    }
    gs_ui_compose(a, w, h, &on);
    gs_ui_compose(b, w, h, &on);
    check(memcmp(a, b, n * sizeof *a) == 0,
          "composing twice gives byte identical output");
    free(a);
    free(b);
}

static void test_awkward_sizes(void)
{
    static const int sizes[][2] = {
        {1, 1}, {1, 500}, {500, 1}, {20, 20}, {57, 3}, {52, 40},
        {46, 46}, {121, 97}, {2, 900}, {900, 2},
        {GS_UI_BAR_HEIGHT, GS_UI_BAR_HEIGHT}, {200, 95}, {400, 93}
    };
    size_t i;
    int all_safe = 1;

    printf("awkward window sizes\n");
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        unsigned int *base, *px;
        int w = sizes[i][0], h = sizes[i][1];

        px = fenced_alloc(w, h, &base);
        if (px == NULL) {
            all_safe = 0;
            continue;
        }
        gs_ui_compose(px, w, h, &on);
        if (!fence_intact(base, w, h)) {
            printf("        wrote outside the buffer at %dx%d\n", w, h);
            all_safe = 0;
        }
        gs_ui_compose(px, w, h, &off);
        if (!fence_intact(base, w, h)) {
            printf("        wrote outside the buffer at %dx%d unmounted\n",
                   w, h);
            all_safe = 0;
        }
        free(base);
    }
    printf("        %d awkward sizes composed, in both states\n",
           (int)(sizeof sizes / sizeof sizes[0]));
    check(all_safe, "every awkward size composed without overrunning");
}

static void test_speed(void)
{
    const int w = 980, h = 640;
    size_t n = (size_t)w * (size_t)h;
    unsigned int *px = malloc(n * sizeof *px);
    struct timespec a, b;
    long ms;

    printf("speed\n");
    if (px == NULL) {
        check(0, "buffer allocated");
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &a);
    gs_ui_compose(px, w, h, &on);
    clock_gettime(CLOCK_MONOTONIC, &b);
    ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
    printf("        one full compose at %dx%d took %ld ms\n", w, h, ms);
    check(ms < 250, "a repaint stays under 250 ms");
    free(px);
}

/* The prompt box. A key pressed there has to reach the screen on its own
 * clock, and return on its own has to send rather than break the line. */
static void test_typing(void)
{
    gs_ui_state_t s;
    int i;

    printf("typing\n");

    /* A frame already owed goes out at once, whatever the animation on
     * screen asked to wait for. Before this rule a keystroke waited for
     * the caret to turn over, up to GS_UI_CARET_MS away. */
    check(gs_ui_wait_ms(GS_UI_CARET_MS, 1) == 0,
          "a frame owed beats the caret clock");
    check(gs_ui_wait_ms(GS_UI_SPECS_FRAME_MS, 1) == 0,
          "a frame owed beats the panel clock");
    check(gs_ui_wait_ms(-1, 1) == 0,
          "a frame owed beats waiting for ever");
    check(gs_ui_wait_ms(0, 1) == 0, "nought stays nought");

    /* With nothing owed the loop waits for whatever asked. */
    check(gs_ui_wait_ms(GS_UI_CARET_MS, 0) == GS_UI_CARET_MS,
          "the caret clock is kept when no frame is owed");
    check(gs_ui_wait_ms(-1, 0) == -1,
          "waiting for ever is kept when no frame is owed");
    check(gs_ui_wait_ms(-4, 0) == -1,
          "a wait below minus one reads as waiting for ever");

    /* The caret period has to be long enough to read as a blink and
     * short enough that a typist never waits for it. */
    check(GS_UI_CARET_MS >= 300 && GS_UI_CARET_MS <= 800,
          "the caret turns over about twice a second");

    /* Return on its own sends. */
    check(gs_ui_key_sends('\n', 0) == 1, "return sends the prompt");
    check(gs_ui_key_sends('\n', GS_WINDOW_MOD_SHIFT) == 0,
          "shift and return does not send");
    check(gs_ui_key_sends('\n', GS_WINDOW_MOD_CONTROL) == 1,
          "control and return still sends");
    check(gs_ui_key_sends('\n',
                          GS_WINDOW_MOD_SHIFT | GS_WINDOW_MOD_CONTROL) == 0,
          "shift wins over control");

    /* Nothing else sends, whatever is held. */
    for (i = 0x20; i <= 0x7e; i++)
        if (gs_ui_key_sends(i, 0) || gs_ui_key_sends(i, GS_WINDOW_MOD_SHIFT))
            break;
    check(i > 0x7e, "no printable character sends");
    check(gs_ui_key_sends(8, 0) == 0, "backspace does not send");
    check(gs_ui_key_sends(27, 0) == 0, "escape does not send");
    check(gs_ui_key_sends(9, 0) == 0, "tab does not send");
    check(gs_ui_key_sends(0, 0) == 0, "no character does not send");

    /* Shift and return is the one that reaches the prompt as a break, so
     * the box still takes more than one line. */
    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;
    check(gs_ui_chat_type(&s, 'a') == 1, "a letter goes in");
    check(gs_ui_chat_type(&s, '\n') == 1, "a break goes in");
    check(gs_ui_chat_type(&s, 'b') == 1, "and typing carries on after it");
    check(strcmp(s.prompt, "a\nb") == 0, "the break is kept as written");

    /* One press puts in one character and no more, whatever went before. */
    memset(&s, 0, sizeof s);
    for (i = 0; i < 64; i++)
        gs_ui_chat_type(&s, 'x');
    check(strlen(s.prompt) == 64, "sixty four presses leave sixty four");

    /* Escape does not reach the prompt as a character. */
    memset(&s, 0, sizeof s);
    check(gs_ui_chat_type(&s, 27) == 0, "escape writes nothing");
    check(s.prompt[0] == '\0', "and leaves the box empty");
    check(gs_ui_key_closes(27) == 1, "escape is what closes");
    check(gs_ui_key_closes('\n') == 0, "return is not");
}

/* The system table of filesystems. A line there carrying the word user
 * is both the record of which drive belongs where and the right to mount
 * it, so reading one wrongly either loses a drive or hands out a right
 * that was never granted. */
static void test_fstab_options(void)
{
    printf("reading the options against a line\n");

    check(gs_mount_option_present("defaults,noauto,user", "user") == 1,
          "user standing on its own is found");
    check(gs_mount_option_present("user", "user") == 1,
          "even as the only option");
    check(gs_mount_option_present("user,rw", "user") == 1,
          "at the front");
    check(gs_mount_option_present("rw,user", "user") == 1,
          "and at the back");

    /* nouser means the opposite, and users is a different option. Both
     * carry the letters of user inside them. */
    check(gs_mount_option_present("defaults,nouser", "user") == 0,
          "nouser does not count as user, since it means the opposite");
    check(gs_mount_option_present("defaults,users", "user") == 0,
          "and users is a different option");
    check(gs_mount_option_present("defaults,useradd", "user") == 0,
          "nor does a longer word starting the same way");
    check(gs_mount_option_present("defaults", "user") == 0,
          "a line without it does not have it");
    check(gs_mount_option_present("", "user") == 0, "nor an empty line");
    check(gs_mount_option_present(NULL, "user") == 0, "nor no line at all");
    check(gs_mount_option_present("user", NULL) == 0, "nor no word to find");
}

/* One line of the table read into its parts. */
static void test_fstab_parse(void)
{
    gs_mount_fstab_t row;

    printf("reading one line of the system table\n");

    check(gs_mount_fstab_parse("D: /mnt/d drvfs defaults 0 0", &row) == 1,
          "the line this machine already had is read");
    check(row.letter == 'D', "with its letter");
    check(strcmp(row.point, "/mnt/d") == 0, "and its point");
    check(strcmp(row.options, "defaults") == 0, "and its options");
    check(row.mountable_by_user == 0,
          "and it does not let an ordinary person mount it");

    check(gs_mount_fstab_parse(
        "D: /mnt/d drvfs defaults,noauto,user,uid=1000,gid=1000 0 0",
        &row) == 1, "the line gravestone writes is read");
    check(row.mountable_by_user == 1,
          "and that one does let an ordinary person mount it");

    /* A comment describes nothing the system mounts. */
    check(gs_mount_fstab_parse("# D: /mnt/d drvfs user 0 0", &row) == 0,
          "a commented line is not a drive");
    check(gs_mount_fstab_parse("   # spaced comment", &row) == 0,
          "and neither is one with spaces in front of the mark");
    check(gs_mount_fstab_parse("", &row) == 0, "an empty line gives nothing");
    check(gs_mount_fstab_parse("\n", &row) == 0, "and a bare line break");
    check(gs_mount_fstab_parse(NULL, &row) == 0, "and no line at all");
    check(gs_mount_fstab_parse("D: /mnt/d drvfs user 0 0", NULL) == 0,
          "and nowhere to write it");

    /* Everything that is not a whole Windows drive. */
    check(gs_mount_fstab_parse("/dev/sda1 /mnt/d ext4 defaults 0 0",
                               &row) == 0, "a real disk is not a drive");
    check(gs_mount_fstab_parse("D:\\work /mnt/w drvfs user 0 0", &row) == 0,
          "a shared folder is not a whole drive");
    check(gs_mount_fstab_parse("DD: /mnt/dd drvfs user 0 0", &row) == 0,
          "two letters is not a drive");
    check(gs_mount_fstab_parse("D: /mnt/d", &row) == 0,
          "a line with no type is not read");

    /* A line with only three fields means the defaults. */
    check(gs_mount_fstab_parse("E: /mnt/e drvfs", &row) == 1 &&
          strcmp(row.options, "defaults") == 0,
          "a line with no options reads as the defaults");
    check(row.mountable_by_user == 0, "which grant nothing");
}

/* Building the line and the whole table around it. */
static void test_fstab_rewrite(void)
{
    char line[512];
    char table[8192];
    size_t len = 0;

    printf("writing the line into the table\n");

    check(gs_mount_fstab_line('D', "/mnt/d", 1000, 1000, line,
                              sizeof line) == GS_OK, "the line is built");
    check(strcmp(line,
        "D: /mnt/d drvfs defaults,noauto,user,uid=1000,gid=1000 0 0\n") == 0,
        "and reads exactly as it has to");

    /* The word user turns on noexec, nosuid and nodev by itself, so
     * nothing can be run from the drive and no file on it can gain
     * rights. Writing those out would be the place to get one wrong. */
    check(strstr(line, ",user") != NULL, "carrying the word user");
    check(strstr(line, "noauto") != NULL,
          "and noauto, since this program does the mounting");
    check(strstr(line, "exec") == NULL,
          "and nothing that would turn running programs back on");
    check(strstr(line, "suid") == NULL,
          "and nothing that would let a file gain rights");

    /* Every value in the line is worked out, never read back. */
    check(gs_mount_fstab_line('d', "/mnt/d", 1000, 1000, line,
                              sizeof line) == GS_ERR_ARG,
          "a small letter builds nothing");
    check(line[0] == '\0', "leaving the answer empty");
    check(gs_mount_fstab_line('D', "/mnt/e", 1000, 1000, line,
                              sizeof line) == GS_ERR_ARG,
          "a point that does not match the letter builds nothing");
    check(gs_mount_fstab_line('D', "/mnt/d -o bind /etc", 1000, 1000, line,
                              sizeof line) == GS_ERR_ARG,
          "and one carrying a second argument builds nothing");
    check(gs_mount_fstab_line('D', "/mnt/d", -1, 1000, line,
                              sizeof line) == GS_ERR_ARG,
          "a nonsense owner builds nothing");
    check(gs_mount_fstab_line('D', "/mnt/d", 1000, 1000, line, 8) == GS_ERR,
          "and a buffer too small refuses rather than cutting the line");

    /* The whole table. A drive already named gets its line replaced, so
     * two lines never name one point. */
    check(gs_mount_fstab_rewrite('D', "/mnt/d", 1000, 1000, table,
                                 sizeof table, &len) == GS_OK,
          "the whole table is built");
    check(len > 0 && strlen(table) == len, "and its length is reported");
    check(strstr(table, ",user") != NULL, "carrying the new line");
    {
        const char *first = strstr(table, "D: /mnt/d");
        const char *second = first != NULL
            ? strstr(first + 1, "D: /mnt/d") : NULL;

        check(first != NULL, "which names the drive");
        check(second == NULL, "exactly once, never twice");
    }
    check(table[len - 1] == '\n', "and the table ends with a line break");

    check(gs_mount_fstab_rewrite('d', "/mnt/d", 1000, 1000, table,
                                 sizeof table, &len) == GS_ERR_ARG,
          "a small letter builds no table");
    check(gs_mount_fstab_rewrite('D', "/mnt/d", 1000, 1000, table, 4,
                                 &len) == GS_ERR,
          "and a buffer too small refuses rather than writing part of one");
}

/* The password box. What is typed lives in ui_secret.c and the only
 * thing the rest of the program can learn is how many characters there
 * are, which is what draws the dots. */
static void test_secret_never_leaks(void)
{
    gs_ui_state_t s;
    const unsigned char *raw;
    size_t i;
    int found = 0;

    printf("what the password box keeps\n");

    memset(&s, 0, sizeof s);
    gs_ui_secret_open(&s, 'D');
    check(gs_ui_secret_visible(&s) == 1, "the box opens for a real letter");
    check(s.secret_letter == 'D', "against that drive");
    check(gs_ui_secret_length() == 0, "with nothing typed");

    check(gs_ui_secret_type(&s, 'h') == 1, "a letter is taken");
    check(gs_ui_secret_type(&s, 'u') == 1, "and another");
    check(gs_ui_secret_type(&s, 'n') == 1, "and another");
    check(gs_ui_secret_type(&s, 't') == 1, "and another");
    check(gs_ui_secret_length() == 4, "four characters are held");
    check(s.secret_len == 4, "and the count reaches the screen");

    /* The state is what every drawing routine is handed. Nothing of what
     * was typed may be anywhere inside it. */
    raw = (const unsigned char *)&s;
    for (i = 0; i + 4 <= sizeof s; i++)
        if (memcmp(raw + i, "hunt", 4) == 0)
            found = 1;
    check(found == 0,
          "and not one byte of it is anywhere in the interface state");

    check(gs_ui_secret_type(&s, 8) == 1, "a rub out is taken");
    check(gs_ui_secret_length() == 3, "leaving three");
    check(gs_ui_secret_type(&s, 27) == 0, "escape is not a character");
    check(gs_ui_secret_type(&s, '\n') == 0, "and neither is a line break");
    check(gs_ui_secret_length() == 3, "so neither changed anything");

    gs_ui_secret_close(&s);
    check(gs_ui_secret_visible(&s) == 0, "the box closes");
    check(gs_ui_secret_length() == 0, "and what was typed is gone");
    check(s.secret_len == 0, "with the count cleared too");
    check(gs_ui_secret_type(&s, 'x') == 0, "a closed box takes nothing");

    /* A box that is busy takes nothing either, so a second press cannot
     * start the work twice. */
    gs_ui_secret_open(&s, 'D');
    s.secret_busy = 1;
    check(gs_ui_secret_type(&s, 'x') == 0, "a busy box takes nothing");
    s.secret_busy = 0;
    gs_ui_secret_close(&s);

    gs_ui_secret_open(&s, 'd');
    check(gs_ui_secret_visible(&s) == 0, "a small letter opens nothing");
    gs_ui_secret_open(&s, ';');
    check(gs_ui_secret_visible(&s) == 0, "and neither does a semicolon");
    gs_ui_secret_open(NULL, 'D');
    check(1, "opening against no state does not crash");

    /* Applying with nothing typed asks for the typing rather than
     * running sudo against an empty line. */
    gs_ui_secret_open(&s, 'D');
    check(gs_ui_secret_apply(&s) == GS_ERR_ARG, "an empty box applies nothing");
    check(s.secret_note[0] != '\0', "and says so");
    gs_ui_secret_close(&s);
}

/* A box that answers clicks has to be drawn, on every screen it can
 * cover. The hit test lives in one file and the drawing in another, and
 * both drawing paths return early on the home screen, so a box wired into
 * one branch of each ended up swallowing every click while showing
 * nothing at all. */
static void test_the_box_is_drawn_on_every_screen(void)
{
    static const int screens[] = { GS_UI_SCREEN_HOME, GS_UI_SCREEN_WORKSPACE };
    const int w = 980, h = 640;
    unsigned int *shut = malloc((size_t)w * (size_t)h * sizeof *shut);
    unsigned int *open = malloc((size_t)w * (size_t)h * sizeof *open);
    gs_ui_state_t s;
    int which;

    printf("the password box reaches the screen\n");

    if (shut == NULL || open == NULL) {
        check(0, "buffers allocated");
        free(shut);
        free(open);
        return;
    }

    for (which = 0; which < 2; which++) {
        size_t i, n = (size_t)w * (size_t)h;
        size_t changed = 0;

        memset(&s, 0, sizeof s);
        s.screen = screens[which];
        gs_ui_compose(shut, w, h, &s);

        gs_ui_secret_open(&s, 'D');
        gs_ui_compose(open, w, h, &s);
        gs_ui_secret_close(&s);

        for (i = 0; i < n; i++)
            if (shut[i] != open[i])
                changed++;

        printf("        screen %d: %lu pixel(s) changed by the box\n",
               screens[which], (unsigned long)changed);
        check(changed > 10000,
              screens[which] == GS_UI_SCREEN_HOME
                  ? "the home screen draws it"
                  : "and so does the workspace");
    }

    /* Wherever the box answers a click, it has to be visible there. */
    for (which = 0; which < 2; which++) {
        gs_ui_rect_t ok, no;
        gs_ui_hit_t hit;

        memset(&s, 0, sizeof s);
        s.screen = screens[which];
        gs_ui_secret_open(&s, 'D');

        ok = gs_ui_secret_ok_rect(w, h);
        no = gs_ui_secret_no_rect(w, h);

        hit = gs_ui_hit_test(&s, w, h, ok.x + ok.w / 2, ok.y + ok.h / 2, NULL);
        check(hit == GS_UI_HIT_SECRET_OK, "the set up button answers");
        hit = gs_ui_hit_test(&s, w, h, no.x + no.w / 2, no.y + no.h / 2, NULL);
        check(hit == GS_UI_HIT_SECRET_NO, "the not now button answers");

        /* Everything behind the box is out of reach, so no stray click
         * opens something the person cannot see. */
        hit = gs_ui_hit_test(&s, w, h, 4, 4, NULL);
        check(hit == GS_UI_HIT_NONE, "the corner behind it answers nothing");
        hit = gs_ui_hit_test(&s, w, h, w - 4, h - 4, NULL);
        check(hit == GS_UI_HIT_NONE, "and the far corner");

        /* With the box shut, that corner goes back to whatever it was,
         * so the box is never left eating clicks. */
        gs_ui_secret_close(&s);
        hit = gs_ui_hit_test(&s, w, h, ok.x + ok.w / 2, ok.y + ok.h / 2, NULL);
        check(hit != GS_UI_HIT_SECRET_OK,
              "and a shut box answers nothing at all");
    }

    free(shut);
    free(open);
}

int main(void)
{
    gs_log_set_level(GS_LOG_ERROR);

    /* Both describe the workspace, since the bar and the panes the
     * checks below look for live on that screen. */
    memset(&off, 0, sizeof off);
    off.screen = GS_UI_SCREEN_WORKSPACE;
    memset(&on, 0, sizeof on);
    on.screen = GS_UI_SCREEN_WORKSPACE;
    on.mounted = 1;
    gs_str_copy(on.status, sizeof on.status, "DEVICE MOUNTED");
    gs_str_copy(off.status, sizeof off.status, "NO DEVICE MOUNTED");

    test_palette_and_bounds();
    test_lamp();
    test_layout();
    test_determinism();
    test_awkward_sizes();
    test_typing();
    test_fstab_options();
    test_fstab_parse();
    test_fstab_rewrite();
    test_secret_never_leaks();
    test_the_box_is_drawn_on_every_screen();
    test_speed();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
