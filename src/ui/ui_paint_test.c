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

int main(void)
{
    gs_log_set_level(GS_LOG_ERROR);

    memset(&off, 0, sizeof off);
    memset(&on, 0, sizeof on);
    on.mounted = 1;
    gs_str_copy(on.status, sizeof on.status, "DEVICE MOUNTED");
    gs_str_copy(off.status, sizeof off.status, "NO DEVICE MOUNTED");

    test_palette_and_bounds();
    test_lamp();
    test_layout();
    test_determinism();
    test_awkward_sizes();
    test_speed();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
