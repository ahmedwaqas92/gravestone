/* window_test.c
 *
 * Runs against a real X server. A window flashes on screen for a moment,
 * which is the point, since a mapped window is the only proof the protocol
 * work landed correctly.
 */
#include "window.h"
#include "window_internal.h"
#include "gravestone.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
static int checks;
static int skipped;

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

static void test_bad_arguments(void)
{
    printf("bad arguments\n");
    check(gs_window_open(NULL, 100, 100) == NULL, "null title refused");
    check(gs_window_open("t", 0, 100) == NULL, "zero width refused");
    check(gs_window_open("t", 100, -5) == NULL, "negative height refused");
    check(gs_window_width(NULL) == 0, "width of NULL is 0");
    check(gs_window_height(NULL) == 0, "height of NULL is 0");
    check(gs_window_error_count(NULL) == 0, "error count of NULL is 0");
    gs_window_close(NULL);
    check(1, "closing NULL does not crash");
}

static void test_missing_display(void)
{
    char *saved = getenv("DISPLAY");
    char keep[256];

    printf("missing display\n");
    if (saved != NULL) {
        strncpy(keep, saved, sizeof keep - 1);
        keep[sizeof keep - 1] = '\0';
    } else {
        keep[0] = '\0';
    }

    unsetenv("DISPLAY");
    check(gs_window_open("t", 100, 100) == NULL, "no DISPLAY refused");

    setenv("DISPLAY", "nonsense-without-a-colon", 1);
    check(gs_window_open("t", 100, 100) == NULL, "malformed DISPLAY refused");

    setenv("DISPLAY", ":77", 1);
    check(gs_window_open("t", 100, 100) == NULL, "absent server refused");

    if (keep[0] != '\0')
        setenv("DISPLAY", keep, 1);
    else
        unsetenv("DISPLAY");
}

/* One window serves every check that needs one. Opening and destroying
 * several in quick succession makes a compositor refuse connections, and
 * that flakiness is worth more to avoid than the tidiness of a window
 * each. */
static void test_on_one_window(void)
{
    gs_window_t *w;
    gs_window_event_t event;
    int rounds, saw = 0;

    if (display_unavailable("everything needing a window"))
        return;

    printf("opening one window for the rest\n");

    w = gs_window_open("gravestone window test", 320, 240);
    check(w != NULL, "window opened");
    if (w == NULL)
        return;

    check(gs_window_width(w) == 320, "width reported as 320");
    check(gs_window_height(w) == 240, "height reported as 240");
    check(gs_window_font_height(w) > 0, "a font loaded");
    printf("        font is %d tall, %d wide per character\n",
           gs_window_font_height(w), gs_window_text_width(w, "M"));
    check(gs_window_text_width(w, "MMMMM") == 5 * gs_window_text_width(w, "M"),
          "the font is fixed width");
    check(gs_window_text_width(w, "") == 0, "an empty string has no width");
    check(gs_window_text_width(NULL, "x") == 0, "a null window measures 0");

    /* A server that accepted CreateWindow and MapWindow sends Expose once
     * the window is on screen. */
    for (rounds = 0; rounds < 40 && !saw; rounds++) {
        int rc = gs_window_wait_event(w, &event, 100);

        if (rc < 0)
            break;
        if (rc == 1 && event.kind == GS_WINDOW_EVENT_EXPOSE)
            saw = 1;
    }
    printf("        exposure arrived: %s\n", saw ? "yes" : "not yet");

    printf("drawing\n");
    gs_window_fill(w, 0, 0, 320, 240, 0x00101810u);
    gs_window_fill(w, 10, 10, 60, 40, 0x001E5B2Eu);
    gs_window_fill(w, -20, -20, 50, 50, 0x00FF0000u);
    gs_window_fill(w, 300, 220, 100, 100, 0x0000FF00u);
    gs_window_fill(w, 0, 0, 0, 10, 0x00FFFFFFu);
    gs_window_fill(w, 0, 0, 10, -3, 0x00FFFFFFu);
    gs_window_flush(w);
    check(1, "fills accepted, including clipped and empty ones");

    gs_window_text(w, 10, 30, "Mount Device to Gravestone", 0x00D6DAE2u);
    gs_window_text(w, 10, 50, "", 0x00FFFFFFu);
    gs_window_text(w, 10, 70, NULL, 0x00FFFFFFu);
    gs_window_text(NULL, 10, 90, "nowhere", 0x00FFFFFFu);
    gs_window_text(w, -50, 110, "off the left edge", 0x00FFFFFFu);
    {
        char big[400];

        memset(big, 'x', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        gs_window_text(w, 4, 130, big, 0x00FFFFFFu);
    }
    check(1, "text accepted, including empty, null and over long");

    printf("size hints\n");
    check(gs_window_set_hints(NULL, -1, -1, 100, 100, 10, 10) == GS_ERR_ARG,
          "hints for a null window refused");
    check(gs_window_set_hints(w, -1, -1, 0, 100, 10, 10) == GS_ERR_ARG,
          "a zero width wish refused");
    check(gs_window_set_hints(w, -1, -1, 640, 480, 200, 150) == GS_OK,
          "hints were sent");
    check(gs_window_set_hints(w, -1, -1, 640, 480, 0, 0) == GS_OK,
          "a minimum of nothing is accepted and made sane");

    printf("resizing from code\n");
    check(gs_window_resize(NULL, 100, 100) == GS_ERR_ARG,
          "resizing a null window refused");
    check(gs_window_resize(w, 0, 100) == GS_ERR_ARG, "zero width refused");
    check(gs_window_resize(w, 100, -4) == GS_ERR_ARG,
          "negative height refused");
    check(gs_window_resize(w, 40000, 100) == GS_ERR_ARG,
          "a size past what the protocol carries is refused");

    check(gs_window_resize(w, 520, 330) == GS_OK, "growing was sent");
    saw = 0;
    for (rounds = 0; rounds < 60 && !saw; rounds++) {
        int rc = gs_window_wait_event(w, &event, 50);

        if (rc < 0)
            break;
        if (rc == 1 && event.kind == GS_WINDOW_EVENT_RESIZE &&
            event.width == 520 && event.height == 330)
            saw = 1;
    }
    printf("        the window now reports %dx%d\n",
           gs_window_width(w), gs_window_height(w));
    check(saw, "the server reported the new size back");

    check(gs_window_resize(w, 180, 60) == GS_OK, "shrinking was sent");
    saw = 0;
    for (rounds = 0; rounds < 60 && !saw; rounds++) {
        int rc = gs_window_wait_event(w, &event, 50);

        if (rc < 0)
            break;
        if (rc == 1 && event.kind == GS_WINDOW_EVENT_RESIZE &&
            event.width == 180 && event.height == 60)
            saw = 1;
    }
    check(saw, "the server reported the smaller size back");

    /* A whole run of steps, the way the panel actually animates. */
    {
        int step, clean = 1;

        for (step = 0; step <= 9; step++) {
            if (gs_window_resize(w, 180 + 40 * step, 60 + 30 * step) != GS_OK)
                clean = 0;
            gs_window_wait_event(w, &event, 14);
        }
        for (step = 9; step >= 0; step--) {
            if (gs_window_resize(w, 180 + 40 * step, 60 + 30 * step) != GS_OK)
                clean = 0;
            gs_window_wait_event(w, &event, 14);
        }
        check(clean, "twenty resize steps all sent cleanly");
    }

    printf("waiting\n");
    check(gs_window_wait_event(w, NULL, 0) == GS_ERR_ARG,
          "null event pointer refused");
    check(gs_window_wait_event(NULL, &event, 0) == GS_ERR_ARG,
          "null window refused");

    /* The window asks for pointer motion, so a mouse resting over it keeps
     * sending events. What matters is the call returning near its deadline
     * rather than waiting for ever. */
    {
        struct timespec a, b;
        long ms;
        int rc;

        while (gs_window_wait_event(w, &event, 60) == 1)
            ;
        clock_gettime(CLOCK_MONOTONIC, &a);
        rc = gs_window_wait_event(w, &event, 120);
        clock_gettime(CLOCK_MONOTONIC, &b);
        ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
        printf("        the wait returned %d after %ld ms\n", rc, ms);
        check(ms < 500, "the deadline was honoured rather than blocking");
    }

    printf("        protocol errors across the whole session: %d\n",
           gs_window_error_count(w));
    check(gs_window_error_count(w) == 0,
          "nothing above upset the server");

    gs_window_close(w);
    check(1, "closed cleanly");
}

/* WSLg will not put a window on the Windows desktop unless the window
 * says which application it belongs to, and WM_CLASS is where it looks.
 * A window with none appears as an icon on the taskbar and nothing else,
 * so this is checked by reading the property back off the server. */
static void test_window_class(void)
{
    gs_window_t *w;
    unsigned char req[24];
    unsigned char reply[32];
    unsigned char value[128];
    size_t len = 0;
    uint32_t value_len;
    size_t instance_len = strlen(GS_WINDOW_INSTANCE) + 1;
    size_t class_len = strlen(GS_WINDOW_CLASS) + 1;

    if (display_unavailable("the application name on the window"))
        return;

    printf("the application name on the window\n");

    w = gs_window_open("Gravestone class test", 320, 200);
    check(w != NULL, "window opened");
    if (w == NULL)
        return;

    memset(req, 0, sizeof req);
    req[0] = 20;                          /* GetProperty */
    req[1] = 0;                           /* leave it in place */
    gs_win_put16(req + 2, 6);
    gs_win_put32(req + 4, w->window_id);
    gs_win_put32(req + 8, X_ATOM_WM_CLASS);
    gs_win_put32(req + 12, 0);            /* any type */
    gs_win_put32(req + 16, 0);            /* from the start */
    gs_win_put32(req + 20, 32);           /* at most 128 bytes */

    w->sequence++;
    check(gs_win_write_all(w, req, sizeof req) == GS_OK,
          "the property request went out");
    check(gs_win_await_reply_data(w, reply, value, sizeof value, &len)
          == GS_OK, "the server answered");

    /* The reply pads its payload out to a multiple of four bytes, so the
     * real length is the item count the server reports rather than the
     * number of bytes that arrived. */
    value_len = gs_win_get32(reply + 16);
    printf("        WM_CLASS holds %u bytes inside %zu delivered, wanted "
           "%zu\n", value_len, len, instance_len + class_len);
    check(len >= (size_t)value_len, "the payload carries every item");
    check(value_len == instance_len + class_len,
          "the property is as long as the two names plus their zero bytes");

    if (value_len == instance_len + class_len) {
        check(memcmp(value, GS_WINDOW_INSTANCE, instance_len) == 0,
              "the instance name comes first, ending in a zero byte");
        check(memcmp(value + instance_len, GS_WINDOW_CLASS, class_len) == 0,
              "the class name follows it, also ending in a zero byte");
    }

    check(gs_window_error_count(w) == 0,
          "setting the name upset the server nowhere");

    gs_window_close(w);
}

int main(void)
{
    gs_log_set_level(GS_LOG_ERROR);

    test_bad_arguments();
    test_missing_display();
    test_on_one_window();
    test_window_class();

    printf("\n%d checks, %d failures, %d skipped\n", checks, failures,
           skipped);
    return failures == 0 ? 0 : 1;
}
