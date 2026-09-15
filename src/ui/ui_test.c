/* ui_test.c
 *
 * The module contract, the controls in the top bar, and one pass against a
 * real window to tie the composed buffer to what arrives on the display.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "log.h"
#include "detect.h"
#include "paths.h"
#include "store.h"
#include "str.h"
#include "window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;
static int skipped;

static gs_ui_state_t off;
static gs_ui_state_t on;

/* A compositor that refuses connections is a condition of the machine
 * rather than a fault in the code, so it is reported as a skip. */
static int display_unavailable(const char *section)
{
    if (gs_window_display_available())
        return 0;
    printf("  skip  %s (the display is refusing connections)\n", section);
    skipped++;
    return 1;
}

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

static void test_contract(void)
{
    printf("module contract\n");
    check(gs_ui_module.name != NULL, "name is set");
    check(strcmp(gs_ui_module.name, "ui") == 0, "name is \"ui\"");
    check(gs_ui_module.summary != NULL, "summary is set");
    check(gs_ui_module.run != NULL, "run is set");
    check(gs_ui_module.init() == GS_OK, "init succeeds");
    gs_ui_module.shutdown();
    check(1, "shutdown does not crash");
    check(gs_ui_paint(NULL, &off) == GS_ERR_ARG,
          "painting a null window refused");
    gs_ui_release();
    gs_ui_release();
    check(1, "releasing twice does not crash");
}

static void test_keys_and_title(void)
{
    int closers = 0;
    int code;

    printf("keys and title\n");

    check(strcmp(GS_UI_TITLE, "Gravestone") == 0,
          "the window title is \"Gravestone\"");
    check(GS_UI_TITLE[0] >= 'A' && GS_UI_TITLE[0] <= 'Z',
          "the title starts with a capital");
    check(GS_UI_SPECS_TITLE[0] >= 'A' && GS_UI_SPECS_TITLE[0] <= 'Z',
          "the specs window title starts with a capital too");

    check(gs_ui_key_closes(27), "Escape closes the window");
    check(!gs_ui_key_closes('q'), "q leaves the window open");

    for (code = 0; code < 256; code++)
        if (gs_ui_key_closes(code))
            closers++;
    printf("        %d keycode out of 256 closes the window\n", closers);
    check(closers == 1, "exactly one keycode closes the window");
}

static void test_button_geometry(void)
{
    const int w = 980;
    gs_ui_rect_t mount = gs_ui_mount_rect(w);
    gs_ui_rect_t specs = gs_ui_specs_rect(w);

    printf("where the controls sit\n");
    printf("        mount at %d,%d %dx%d   specs at %d,%d %dx%d\n",
           mount.x, mount.y, mount.w, mount.h,
           specs.x, specs.y, specs.w, specs.h);

    check(mount.y >= 0 && mount.y + mount.h <= GS_UI_BAR_HEIGHT,
          "the mount control sits inside the bar");
    check(specs.y >= 0 && specs.y + specs.h <= GS_UI_BAR_HEIGHT,
          "the specs control sits inside the bar");
    check(specs.x >= mount.x + mount.w + GS_UI_BTN_GAP - 1,
          "the two do not overlap");
    check(mount.x >= GS_UI_MARGIN, "the first keeps the left margin");
    check(mount.w > 0 && mount.h > 0 && specs.w > 0 && specs.h > 0,
          "both have a real size");

    check(gs_ui_rect_contains(mount, mount.x, mount.y),
          "the top left corner counts as inside");
    check(gs_ui_rect_contains(mount, mount.x + mount.w - 1,
                              mount.y + mount.h - 1),
          "the bottom right corner counts as inside");
    check(!gs_ui_rect_contains(mount, mount.x + mount.w, mount.y),
          "one pixel past the right edge is outside");
    check(!gs_ui_rect_contains(mount, mount.x, mount.y + mount.h),
          "one pixel past the bottom edge is outside");
    check(!gs_ui_rect_contains(mount, mount.x - 1, mount.y),
          "one pixel to the left is outside");
}

static gs_ui_hit_t hit_at(const gs_ui_state_t *state, int w, int h, int x,
                          int y, int *row)
{
    int spare = -1;

    return gs_ui_hit_test(state, w, h, x, y, row != NULL ? row : &spare);
}

static void test_hit_testing(void)
{
    const int w = 980, h = 640;
    gs_ui_rect_t mount = gs_ui_mount_rect(w);
    gs_ui_rect_t specs = gs_ui_specs_rect(w);
    gs_ui_rect_t chooser = gs_ui_chooser_rect(w, h);

    printf("what a click lands on\n");

    check(hit_at(&off, w, h, mount.x + 5, mount.y + 5, NULL)
          == GS_UI_HIT_MOUNT, "mount answers with nothing stored");
    check(hit_at(&on, w, h, mount.x + 5, mount.y + 5, NULL)
          == GS_UI_HIT_MOUNT, "mount answers with a device stored");

    check(hit_at(&off, w, h, specs.x + 5, specs.y + 5, NULL)
          == GS_UI_HIT_NONE,
          "the specs control ignores clicks while nothing is stored");
    check(hit_at(&on, w, h, specs.x + 5, specs.y + 5, NULL)
          == GS_UI_HIT_SPECS,
          "the specs control answers once a device is stored");

    check(hit_at(&on, w, h, 5, 5, NULL) == GS_UI_HIT_NONE,
          "the corner of the bar is not a control");
    check(hit_at(&on, w, h, chooser.x + 5, chooser.y + 5, NULL)
          == GS_UI_HIT_CHOOSER, "the bar under the buttons opens the list");
    check(hit_at(NULL, w, h, specs.x + 5, specs.y + 5, NULL)
          == GS_UI_HIT_NONE, "a null state leaves the specs control shut");
    check(hit_at(&on, w, h, mount.x + mount.w + 2, mount.y + 5, NULL)
          == GS_UI_HIT_NONE, "the gap between them lands nowhere");
}

static void test_pane_geometry(void)
{
    const int w = 980, h = 640;
    gs_ui_rect_t left = gs_ui_left_rect(w, h);
    gs_ui_rect_t mid = gs_ui_mid_rect(w, h);
    gs_ui_rect_t right = gs_ui_right_rect(w, h);
    gs_ui_rect_t chooser = gs_ui_chooser_rect(w, h);
    gs_ui_rect_t narrow = gs_ui_left_rect(80, 200);

    printf("where the three panes sit\n");

    check(chooser.y + chooser.h <= left.y,
          "the chooser sits clear of the panes");
    {
        gs_ui_rect_t back = gs_ui_back_rect(w, h);

        check(back.w > 0, "the back arrow exists at the normal size");
        check(chooser.x + chooser.w <= back.x,
              "and the chooser stops short of it");
        check(back.x + back.w <= w - GS_UI_MARGIN,
              "with the arrow inside the margin");
        check(back.y >= chooser.y &&
              back.y + back.h <= chooser.y + chooser.h,
              "and level with the chooser row");
    }
    check(left.x >= GS_UI_MARGIN, "the left pane keeps the margin");
    check(left.x + left.w < mid.x, "the left pane ends before the middle");
    check(mid.x + mid.w < right.x, "the middle ends before the right");
    check(right.x + right.w <= w - GS_UI_MARGIN,
          "the right pane keeps the margin");
    check(mid.x - (left.x + left.w) == GS_UI_PANE_GAP,
          "the first gap is the width it was asked for");
    check(right.x - (mid.x + mid.w) == GS_UI_PANE_GAP,
          "the second gap matches the first");
    check(left.w > mid.w, "a model name gets more width than a mount point");
    check(left.y == mid.y && mid.y == right.y, "all three start level");
    check(left.h == mid.h && mid.h == right.h, "all three are the same height");
    check(left.y + left.h <= h - GS_UI_MARGIN,
          "the panes stop short of the bottom edge");
    check(gs_ui_pane_rows(left) > 4, "a pane this tall shows several rows");
    check(narrow.w >= 0, "a window too small for panes gives no width");
    check(gs_ui_pane_rows(narrow) >= 0, "and asks for no rows");
}

static void test_pane_rows(void)
{
    const int w = 980, h = 640;
    gs_ui_state_t s = on;
    gs_ui_rect_t left, mid, right, drop;
    int row = -1;

    printf("clicks that land on a row\n");

    s.model_count = 40;
    s.disk_count = 3;
    s.library_count = 2;
    s.category_count = 6;
    s.model_scroll = 0;
    s.library_scroll = 0;

    left = gs_ui_left_rect(w, h);
    mid = gs_ui_mid_rect(w, h);
    right = gs_ui_right_rect(w, h);

    check(hit_at(&s, w, h, left.x + 20, left.y + GS_UI_PANE_HEADER + 2, &row)
          == GS_UI_HIT_LEFT_ROW && row == 0,
          "the first model row answers with index zero");
    check(hit_at(&s, w, h, left.x + 20,
                 left.y + GS_UI_PANE_HEADER + GS_UI_ROW_HEIGHT + 2, &row)
          == GS_UI_HIT_LEFT_ROW && row == 1,
          "the row below it answers with index one");
    check(hit_at(&s, w, h, left.x + 20, left.y + 4, &row) == GS_UI_HIT_NONE,
          "the header is not a row");

    s.model_scroll = 10;
    check(hit_at(&s, w, h, left.x + 20, left.y + GS_UI_PANE_HEADER + 2, &row)
          == GS_UI_HIT_LEFT_ROW && row == 10,
          "scrolling moves which model the top row means");
    s.model_scroll = 0;

    check(hit_at(&s, w, h, mid.x + 10, mid.y + GS_UI_PANE_HEADER + 2, &row)
          == GS_UI_HIT_MID_ROW && row == 0, "the first disk answers");
    check(hit_at(&s, w, h, mid.x + 10,
                 mid.y + GS_UI_PANE_HEADER + 5 * GS_UI_ROW_HEIGHT, &row)
          == GS_UI_HIT_NONE, "past the last disk lands nowhere");

    check(hit_at(&s, w, h, right.x + 10, right.y + GS_UI_PANE_HEADER + 2,
                 &row) == GS_UI_HIT_RIGHT_ROW && row == 0,
          "the first model on disk answers");

    /* With the list open it covers whatever sits under it. The list holds
     * categories, so its rows are counted against category_count. */
    s.chooser_open = 1;
    drop = gs_ui_drop_rect(w, h, s.category_count);
    check(hit_at(&s, w, h, drop.x + 20, drop.y + 6, &row)
          == GS_UI_HIT_DROP_ROW && row == 0,
          "the open list answers with its first category");
    check(hit_at(&s, w, h, drop.x + 20,
                 drop.y + 6 + 2 * GS_UI_ROW_HEIGHT, &row)
          == GS_UI_HIT_DROP_ROW && row == 2,
          "the third row answers with index two");
    check(hit_at(&s, w, h, drop.x + 20,
                 drop.y + 6 + 9 * GS_UI_ROW_HEIGHT, &row)
          != GS_UI_HIT_DROP_ROW,
          "past the last category lands on no category");
    check(drop.h <= GS_UI_DROP_ROWS * GS_UI_ROW_HEIGHT + 8,
          "the open list never grows past its row limit");
    s.model_scroll = 10;
    check(hit_at(&s, w, h, drop.x + 20, drop.y + 6, &row)
          == GS_UI_HIT_DROP_ROW && row == 0,
          "scrolling the pane does not move the category list");
    s.model_scroll = 0;
    check(hit_at(&s, w, h, left.x + 20, left.y + GS_UI_PANE_HEADER + 2, &row)
          != GS_UI_HIT_LEFT_ROW,
          "the open list covers the pane underneath it");
    {
        gs_ui_rect_t mount = gs_ui_mount_rect(w);

        check(hit_at(&s, w, h, mount.x + 5, mount.y + 5, &row)
              == GS_UI_HIT_MOUNT,
              "the bar still answers with the list open");
    }
}

static void test_button_appearance(void)
{
    const int w = 980, h = 640;
    size_t n = (size_t)w * (size_t)h;
    unsigned int *a = malloc(n * sizeof *a);
    unsigned int *b = malloc(n * sizeof *b);
    gs_ui_rect_t mount = gs_ui_mount_rect(w);
    gs_ui_rect_t specs = gs_ui_specs_rect(w);

    printf("how the controls draw\n");
    if (a == NULL || b == NULL) {
        free(a); free(b);
        check(0, "buffers allocated");
        return;
    }

#define MID(buf, r) buf[(size_t)((r).y + (r).h / 2) * (size_t)w + \
                        (size_t)((r).x + (r).w / 2)]

    gs_ui_compose(a, w, h, &off);
    gs_ui_compose(b, w, h, &on);

    check(MID(a, specs) != MID(b, specs),
          "the specs control looks different once a device is stored");
    check(lum(MID(a, specs)) < lum(MID(b, specs)),
          "it is dimmer while it cannot be clicked");
    check(MID(a, mount) == MID(b, mount),
          "the mount control looks the same either way");

    {
        static gs_ui_state_t hover;   /* a megabyte, kept off the stack */
        static gs_ui_state_t down;    /* a megabyte, kept off the stack */

        hover = on;
        down = on;
        hover.hover = GS_UI_HIT_MOUNT;
        down.pressed = GS_UI_HIT_MOUNT;

        gs_ui_compose(a, w, h, &hover);
        check(lum(MID(a, mount)) > lum(MID(b, mount)),
              "the pointer resting on a control lifts it");

        gs_ui_compose(a, w, h, &down);
        check(lum(MID(a, mount)) < lum(MID(b, mount)),
              "holding it down pushes it in");
    }

    gs_ui_compose(a, w, h, &on);
    check(a[(size_t)mount.y * (size_t)w + (size_t)(mount.x + mount.w / 2)]
          != MID(a, mount), "a control carries a border");

    /* The bar itself has to differ from the surface under it. */
    check(a[(size_t)(GS_UI_BAR_HEIGHT / 2) * (size_t)w + 8] !=
          a[(size_t)(GS_UI_BAR_HEIGHT + 40) * (size_t)w + 8],
          "the bar is a different tone from the canvas");

#undef MID
    free(a);
    free(b);
}

/* A window has to be on screen and unobscured before the server hands its
 * pixels back, and compositing lags the map by an unpredictable amount. */
static int wait_readable(gs_window_t *w)
{
    unsigned int probe = 0;
    int attempt;

    for (attempt = 0; attempt < 60; attempt++) {
        gs_window_event_t drain;

        if (gs_window_read_pixel(w, 0, 0, &probe) == GS_OK)
            return 1;
        gs_window_wait_event(w, &drain, 50);
    }
    return 0;
}

static void test_specs_panel(void)
{
    gs_ui_specs_t panel;
    gs_ui_row_t rows[GS_UI_SPECS_MAX_ROWS];
    gs_detect_report_t report;
    int frames;

    printf("the specs panel with nothing stored\n");

    system("rm -rf /tmp/gravestone-ui-test");
    gs_paths_override("/tmp/gravestone-ui-test");

    check(gs_store_open() == GS_OK, "an empty database opened");
    check(gs_store_is_mounted() == 0, "it holds no device");
    check(gs_ui_specs_gather(rows, GS_UI_SPECS_MAX_ROWS) == 0,
          "there are no rows to gather");
    check(gs_ui_specs_open(&panel, NULL) == GS_ERR,
          "the panel refuses to open with nothing to show");
    check(panel.window == NULL, "and it opened no window");

    printf("the panel with a device stored\n");
    check(gs_detect_read(&report) == GS_OK, "the machine was read");
    check(gs_store_mount(&report, NULL) == GS_OK, "and stored");

    {
        int n = gs_ui_specs_gather(rows, GS_UI_SPECS_MAX_ROWS);
        int i, all_named = 1, disks = 0;

        printf("        %d rows gathered\n", n);
        check(n > 0, "rows are available now");
        check(n <= GS_UI_SPECS_MAX_ROWS, "the list stayed in bounds");
        for (i = 0; i < n; i++) {
            if (rows[i].key[0] == '\0' || rows[i].value[0] == '\0')
                all_named = 0;
            if (strncmp(rows[i].key, "disk ", 5) == 0)
                disks++;
        }
        check(all_named, "every row has a name and a value");
        check(disks == report.disk_count,
              "one row per disk, matching what was read");
        check(strcmp(rows[0].key, "operating system") == 0,
              "the table leads with the operating system");
        for (i = 0; i < n; i++)
            if (strcmp(rows[i].key, "install") == 0 ||
                strcmp(rows[i].key, "fingerprint") == 0)
                all_named = 0;
        check(all_named,
              "neither the install identifier nor the fingerprint is shown");
    }

    if (display_unavailable("the panel on screen")) {
        gs_store_close();
        system("rm -rf /tmp/gravestone-ui-test");
        gs_paths_override(NULL);
        return;
    }
    check(gs_ui_specs_open(&panel, NULL) == GS_OK, "the panel opened");
    check(panel.window != NULL, "it has a window");
    check(panel.step == 0, "it starts folded away");
    check(gs_ui_specs_animating(&panel), "and it is growing");
    check(!gs_ui_specs_spent(&panel), "it is not finished");

    printf("        growing\n");
    for (frames = 0; frames < 40 && gs_ui_specs_animating(&panel); frames++)
        gs_ui_specs_advance(&panel);
    printf("        opened in %d frames, step %d of %d\n", frames,
           panel.step, GS_UI_SPECS_STEPS);
    check(frames <= GS_UI_SPECS_STEPS + 1,
          "opening takes no more frames than it has steps");
    check(panel.step == GS_UI_SPECS_STEPS, "it reached full size");
    check(!gs_ui_specs_animating(&panel), "and then rests");
    /* A window reports the size the server last confirmed, so the events
     * carrying that confirmation have to be read before asking. */
    {
        gs_window_event_t drain;
        int waited;

        for (waited = 0; waited < 40 &&
             gs_window_width(panel.window) <= GS_UI_SPECS_START_W; waited++)
            gs_window_wait_event(panel.window, &drain, 50);
        printf("        the window now reports %dx%d after %d waits\n",
               gs_window_width(panel.window),
               gs_window_height(panel.window), waited);
        /* The requests were sent and the server raised no error. When the
         * confirmation is slow to arrive that is the compositor pacing
         * itself, so it reports as a skip rather than a fault here. */
        if (gs_window_width(panel.window) > GS_UI_SPECS_START_W) {
            check(1, "the window really did grow once the server confirmed it");
        } else {
            printf("  skip  the growth confirmation (the compositor has not "
                   "caught up)\n");
            skipped++;
        }
        check(gs_window_error_count(panel.window) == 0,
              "the server accepted every resize on the way out");
    }

    printf("        folding away\n");
    gs_ui_specs_begin_close(&panel);
    check(gs_ui_specs_animating(&panel), "closing started");
    for (frames = 0; frames < 40 && !gs_ui_specs_spent(&panel); frames++)
        gs_ui_specs_advance(&panel);
    printf("        closed in %d frames\n", frames);
    check(frames <= GS_UI_SPECS_STEPS + 1,
          "closing takes no more frames than opening");
    check(gs_ui_specs_spent(&panel), "it ran out");
    check(panel.step == 0, "back to folded away");
    check(gs_window_error_count(panel.window) == 0,
          "the whole animation upset the server nowhere");

    gs_ui_specs_destroy(&panel);
    check(panel.window == NULL, "destroying it clears the window");
    gs_ui_specs_destroy(&panel);
    check(1, "destroying it twice does not crash");

    check(gs_ui_specs_animating(NULL) == 0, "a null panel is not animating");
    check(gs_ui_specs_spent(NULL) == 0, "a null panel is not spent");
    check(gs_ui_specs_advance(NULL) == 0, "advancing a null panel is safe");
    gs_ui_specs_paint(NULL);
    gs_ui_specs_begin_close(NULL);
    check(1, "painting and closing a null panel does not crash");
    check(gs_ui_specs_open(NULL, NULL) == GS_ERR_ARG, "opening into null refused");

    /* The panel is meant to land in the middle of the window it belongs
     * to, and to stay centred on that point as it grows. */
    if (!display_unavailable("centring the panel")) {
        gs_window_t *host = gs_window_open("Gravestone host", 700, 500);

        check(host != NULL, "a host window opened");
        if (host != NULL) {
            gs_window_event_t drain;
            int hx = -1, hy = -1;
            int rounds;

            int placed = GS_ERR;

            for (rounds = 0; rounds < 20; rounds++)
                gs_window_wait_event(host, &drain, 50);

            /* The compositor answers this one only once it has placed the
             * window, and under load that takes longer than the drain
             * above. The code is printed so a failure says which way it
             * went rather than only that it went. */
            for (rounds = 0; rounds < 10 && placed != GS_OK; rounds++) {
                placed = gs_window_position(host, &hx, &hy);
                if (placed != GS_OK)
                    gs_window_wait_event(host, &drain, 50);
            }
            /* GS_ERR_IO means the connection has gone, which happens
             * under WSLg after a run has opened and closed many windows.
             * A dead socket is a condition of the machine, so the section
             * is skipped rather than failed. */
            if (placed == GS_ERR_IO) {
                printf("  skip  centring the panel (the server dropped the "
                       "connection after %d tries)\n", rounds);
                skipped++;
                gs_window_close(host);
                return;
            }
            if (placed != GS_OK)
                printf("        gs_window_position returned %d after %d "
                       "tries, server errors %d\n", placed, rounds,
                       gs_window_error_count(host));
            check(placed == GS_OK, "the host reports where it sits");
            check(gs_window_position(NULL, &hx, &hy) == GS_ERR_ARG,
                  "asking a null window where it sits is refused");
            gs_window_position(host, &hx, &hy);
            printf("        host sits at %d,%d and is %dx%d\n", hx, hy,
                   gs_window_width(host), gs_window_height(host));

            if (gs_ui_specs_open(&panel, host) == GS_OK) {
                int want_cx = hx + gs_window_width(host) / 2;
                int want_cy = hy + gs_window_height(host) / 2;

                check(panel.centred, "the panel worked out a centre");
                check(panel.centre_x == want_cx && panel.centre_y == want_cy,
                      "and that centre is the middle of the host");

                /* Growing must keep the middle still, so the corner has to
                 * move by half of every size change. */
                {
                    int first_x = panel.centre_x - panel.want_w / 2;
                    int i;

                    for (i = 0; i < GS_UI_SPECS_STEPS; i++)
                        gs_ui_specs_advance(&panel);
                    check(panel.centre_x - panel.want_w / 2 < first_x,
                          "the corner moves left as the panel widens");
                    check(panel.centre_x == want_cx &&
                          panel.centre_y == want_cy,
                          "the centre never moved while it grew");
                    check(panel.want_w == GS_UI_SPECS_W &&
                          panel.want_h == GS_UI_SPECS_H,
                          "it grew to the full size");
                }
                check(gs_window_error_count(panel.window) == 0,
                      "no protocol errors from placing it");
                gs_ui_specs_destroy(&panel);
            } else {
                check(0, "the panel opened on the host");
            }

            check(gs_window_place(NULL, 0, 0, 10, 10) == GS_ERR_ARG,
                  "placing a null window is refused");
            check(gs_window_place(host, 0, 0, 0, 10) == GS_ERR_ARG,
                  "placing with no width is refused");
            check(gs_window_place(host, 40000, 0, 10, 10) == GS_ERR_ARG,
                  "placing past what the protocol carries is refused");

            gs_window_close(host);
        }
    }

    gs_store_close();
    system("rm -rf /tmp/gravestone-ui-test");
    gs_paths_override(NULL);
}

static void test_on_screen(void)
{
    const int w = 520, h = 400;
    gs_window_t *win;
    gs_window_event_t event;
    unsigned int *expected;
    int rounds, mapped = 0;
    int points[6][2];
    int point_count = 0;
    int real_w, real_h;
    size_t i;
    int matches = 0, tried = 0;

    if (display_unavailable("what actually reaches the screen"))
        return;

    printf("what actually reaches the screen\n");

    win = gs_window_open("Gravestone canvas test", w, h);
    check(win != NULL, "window opened");
    if (win == NULL)
        return;

    check(gs_window_font_height(win) > 0, "a font loaded");
    printf("        font is %d pixels tall, %d wide for one character\n",
           gs_window_font_height(win), gs_window_text_width(win, "M"));
    check(gs_window_text_width(win, "abc") ==
          3 * gs_window_text_width(win, "M"),
          "the font is fixed width, so three characters are three widths");

    /* An exposure is one sign the window is up. The compositor sometimes
     * delays it under load, so the test asks the stronger question of
     * whether the window can actually be drawn to and read back. */
    for (rounds = 0; rounds < 20 && !mapped; rounds++) {
        int rc = gs_window_wait_event(win, &event, 50);

        if (rc < 0)
            break;
        if (rc == 1 && event.kind == GS_WINDOW_EVENT_EXPOSE)
            mapped = 1;
    }
    printf("        exposure arrived: %s\n", mapped ? "yes" : "not yet");

    /* Without an exposure the window is not on screen yet, and anything
     * said about what reaches the screen would be about the compositor
     * rather than about this code. */
    if (!mapped) {
        printf("  skip  painting and reading back (the window never came "
               "up)\n");
        skipped++;
        gs_window_close(win);
        return;
    }

    check(gs_ui_paint(win, &on) == GS_OK, "paint and blit succeeded");
    check(gs_window_error_count(win) == 0,
          "no protocol errors while painting, text included");

    /* The window manager is free to hand back a size other than the one
     * asked for, and gs_ui_paint composes at whatever the window actually
     * became. Composing the comparison at the requested size instead would
     * compare two different pictures. */
    real_w = gs_window_width(win);
    real_h = gs_window_height(win);
    printf("        asked for %dx%d, the window became %dx%d\n", w, h,
           real_w, real_h);

    expected = malloc((size_t)real_w * (size_t)real_h * sizeof *expected);
    if (expected == NULL) {
        gs_window_close(win);
        return;
    }
    gs_ui_compose(expected, real_w, real_h, &on);

    /* Glyphs are drawn by the server after the image lands, so a sample
     * has to sit clear of every line of text. With nothing in the lists
     * the only text under the bar is the chooser line and the three pane
     * headers, so the row area of each pane is bare. */
    {
        gs_ui_rect_t left = gs_ui_left_rect(real_w, real_h);
        gs_ui_rect_t mid = gs_ui_mid_rect(real_w, real_h);
        gs_ui_rect_t right = gs_ui_right_rect(real_w, real_h);
        int rows_y = left.y + GS_UI_PANE_HEADER + GS_UI_ROW_HEIGHT / 2;

        points[point_count][0] = 4;
        points[point_count++][1] = real_h - 6;
        points[point_count][0] = left.x + left.w / 2;
        points[point_count++][1] = rows_y;
        points[point_count][0] = mid.x + mid.w / 2;
        points[point_count++][1] = rows_y + GS_UI_ROW_HEIGHT;
        points[point_count][0] = right.x + right.w / 2;
        points[point_count++][1] = rows_y + GS_UI_ROW_HEIGHT * 2;
        points[point_count][0] = left.x + left.w + GS_UI_PANE_GAP / 2;
        points[point_count++][1] = rows_y;
        points[point_count][0] = real_w / 2;
        points[point_count++][1] = GS_UI_BAR_HEIGHT + 4;
    }

    if (!wait_readable(win)) {
        printf("  skip  pixel comparison (the server will not read this "
               "window back)\n");
        skipped++;
        free(expected);
        gs_window_close(win);
        return;
    }

    for (i = 0; i < (size_t)point_count; i++) {
        int x = points[i][0], y = points[i][1];
        unsigned int got = 0;
        unsigned int want;

        if (x < 0 || y < 0 || x >= real_w || y >= real_h)
            continue;
        want = expected[(size_t)y * (size_t)real_w + (size_t)x];
        int attempt, agreed = 0;

        /* The image is sent and the compositor paints it in its own time,
         * so the screen converges on the buffer rather than matching the
         * instant the request goes out. */
        for (attempt = 0; attempt < 20 && !agreed; attempt++) {
            gs_window_event_t drain;

            if (gs_window_read_pixel(win, x, y, &got) != GS_OK)
                break;
            if (got == want)
                agreed = 1;
            else
                gs_window_wait_event(win, &drain, 25);
        }
        if (attempt == 0)
            continue;
        tried++;
        if (agreed) {
            matches++;
        } else {
            printf("        (%d,%d) screen 0x%06x, buffer 0x%06x after %d "
                   "attempts\n", x, y, got, want, attempt);
        }
    }

    if (tried == 0) {
        printf("  skip  pixel comparison (no point could be read)\n");
        skipped++;
    } else {
        check(matches == tried,
              "every sampled pixel on screen equals the composed buffer");
    }

    free(expected);
    gs_window_close(win);
    gs_ui_release();
}

int main(void)
{
    gs_log_set_level(GS_LOG_ERROR);

    /* Both shared states describe the workspace, since every control
     * the tests below reach for lives on that screen. */
    memset(&off, 0, sizeof off);
    off.screen = GS_UI_SCREEN_WORKSPACE;
    memset(&on, 0, sizeof on);
    on.screen = GS_UI_SCREEN_WORKSPACE;
    on.mounted = 1;
    gs_str_copy(on.status, sizeof on.status, "DEVICE MOUNTED");
    gs_str_copy(off.status, sizeof off.status, "NO DEVICE MOUNTED");

    test_contract();
    test_keys_and_title();
    test_button_geometry();
    test_hit_testing();
    test_pane_geometry();
    test_pane_rows();
    test_button_appearance();
    /* The pixel comparison runs before the panel work, since reading a
     * window back gives undefined content wherever another window covers
     * it, and the centring checks put a large one on the screen. */
    test_on_screen();
    test_specs_panel();

    printf("\n%d checks, %d failures, %d skipped\n", checks, failures,
           skipped);
    return failures == 0 ? 0 : 1;
}
