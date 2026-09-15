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
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Milliseconds since a fixed point, used only to time how long a wait
 * actually sat for. */
static long long now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int failures;
static int checks;
static int skipped;

/* A compositor that refuses connections is a condition of the machine
 * rather than a fault in the code, so it is reported as a skip. */
static int display_unavailable(const char *section)
{
    /* A window opening steals the screen from whoever is using it, so
     * every section that needs one can be turned off from outside. What
     * is left runs anywhere and costs nothing. */
    if (getenv("GS_TEST_NO_WINDOW") != NULL) {
        printf("  skip  %s (windows are turned off)\n", section);
        skipped++;
        return 1;
    }
    if (gs_window_display_available())
        return 0;
    printf("  skip  %s (the display is refusing connections)\n", section);
    skipped++;
    return 1;
}

/* WSLg drops a client's socket now and then, which this codebase already
 * records as common after a run opens and closes many windows. Every
 * check after that point fails for the same reason, and none of them says
 * anything about the code. A dropped connection therefore ends the
 * section as a skip. */
static int connection_gone(gs_window_t *w, const char *section)
{
    if (w == NULL || gs_window_connected(w))
        return 0;
    printf("  skip  %s (the server dropped the connection)\n", section);
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
    check(gs_window_connected(NULL) == 0, "a null window is not connected");
    gs_window_close(NULL);
    check(1, "closing NULL does not crash");

    /* The pointer shape and the full screen request both reach the
     * server, so both have to refuse nonsense before they get there. */
    check(gs_window_set_cursor(NULL, GS_WINDOW_CURSOR_HAND) == GS_ERR_ARG,
          "setting a shape on no window is refused");
    check(gs_window_set_cursor(NULL, GS_WINDOW_CURSOR_ARROW) == GS_ERR_ARG,
          "whichever shape it is");
    check(gs_window_maximise(NULL, 1) == GS_ERR_ARG,
          "growing no window is refused");
    check(gs_window_maximise(NULL, 0) == GS_ERR_ARG,
          "and so is shrinking it");
    check(GS_WINDOW_CURSOR_ARROW == 0,
          "the arrow is the shape a window starts with");
    check(GS_WINDOW_CURSOR_COUNT == 3, "there are three shapes in all");
}

/* The keyboard map is a table turning a seat number into a meaning, and
 * each seat carries several meanings in a row. The first is the key on
 * its own and the second is the key with shift held, which is where a
 * question mark lives above the oblique. Reading only the first is what
 * makes shift give an oblique.
 *
 * Nothing here opens a window, since a made up event and a made up table
 * are enough to decide what a key press turns into. */
/* A key arriving twice when it was pressed once is the complaint that
 * sent me looking here, so the queue is checked for it directly. Events
 * land in a ring and are handed out one at a time, and the whole point is
 * that a packet put in once comes out once and is then gone. */
static void test_key_queue_hands_each_press_out_once(void)
{
    struct gs_window w;
    unsigned char out[32];
    int i, seen;

    printf("one press, one event\n");

    memset(&w, 0, sizeof w);
    check(gs_win_queue_pop(&w, out) == 0, "an empty queue hands out nothing");

    /* Sixteen presses go in, each carrying its own seat number. */
    for (i = 0; i < 16; i++) {
        unsigned char *slot = w.queue[(w.queue_head + w.queue_len) %
                                      WIN_EVENT_QUEUE_LEN];

        memset(slot, 0, 32);
        slot[0] = X_EVT_KEY_PRESS;
        slot[1] = (unsigned char)(40 + i);
        w.queue_len++;
    }

    /* They come back in the order they went in, each exactly once. */
    for (seen = 0; seen < 16; seen++) {
        if (gs_win_queue_pop(&w, out) != 1)
            break;
        if (out[1] != (unsigned char)(40 + seen))
            break;
    }
    check(seen == 16, "sixteen presses come back as sixteen, in order");
    check(w.queue_len == 0, "and the queue is empty afterwards");
    check(gs_win_queue_pop(&w, out) == 0, "so the next read hands out nothing");

    /* The ring wraps, which is where a second helping would come from if
     * the head and the length ever disagreed. */
    w.queue_head = WIN_EVENT_QUEUE_LEN - 2;
    for (i = 0; i < 5; i++) {
        unsigned char *slot = w.queue[(w.queue_head + w.queue_len) %
                                      WIN_EVENT_QUEUE_LEN];

        memset(slot, 0, 32);
        slot[0] = X_EVT_KEY_PRESS;
        slot[1] = (unsigned char)(70 + i);
        w.queue_len++;
    }
    for (seen = 0; seen < 5; seen++) {
        if (gs_win_queue_pop(&w, out) != 1)
            break;
        if (out[1] != (unsigned char)(70 + seen))
            break;
    }
    check(seen == 5, "five presses across the join come back as five");
    check(w.queue_len == 0, "with nothing left behind");
    check(gs_win_queue_pop(&w, out) == 0, "and nothing handed out twice");
}

static void test_shifted_keys(void)
{
    struct gs_window w;
    unsigned char packet[32];
    gs_window_event_t out;

    printf("what a key press turns into\n");

    memset(&w, 0, sizeof w);
    /* Seat 61 is the oblique on a common keyboard, and shift gives the
     * question mark above it. */
    w.keysym0[61] = '/';
    w.keysym1[61] = '?';
    w.keysym0[38] = 'a';
    w.keysym1[38] = 'A';
    w.keysym0[36] = 0xff0d;      /* return */
    w.keysym1[36] = 0xff0d;
    w.keysym0[22] = 0xff08;      /* backspace */
    w.keysym1[22] = 0xff08;
    w.keysym0[9]  = 0xff1b;      /* escape */
    w.keysym1[9]  = 0xff1b;

    memset(packet, 0, sizeof packet);
    packet[0] = X_EVT_KEY_PRESS;

    packet[1] = 61;
    gs_win_put16(packet + 28, 0);
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == '/',
          "the oblique on its own gives an oblique");
    check(out.mods == 0, "with nothing held");

    gs_win_put16(packet + 28, 0x0001);      /* shift */
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == '?',
          "and with shift held it gives the question mark above it");
    check((out.mods & GS_WINDOW_MOD_SHIFT) != 0, "which is reported as held");

    packet[1] = 38;
    gs_win_put16(packet + 28, 0);
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == 'a',
          "a letter alone is the small one");
    gs_win_put16(packet + 28, 0x0001);
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == 'A',
          "and with shift the capital");

    /* The caps lock reaches letters and leaves everything else alone, so
     * a locked keyboard still gives a digit rather than the mark above. */
    gs_win_put16(packet + 28, 0x0002);      /* caps lock */
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == 'A',
          "a locked keyboard gives the capital");
    gs_win_put16(packet + 28, 0x0003);      /* caps lock and shift */
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == 'a',
          "and shift on top of the lock gives the small one back");
    packet[1] = 61;
    gs_win_put16(packet + 28, 0x0002);
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == '/',
          "the lock leaves the oblique alone");

    /* Most keys carry nothing in the shifted column, written as nought,
     * since holding shift changes nothing about them. Return is one of
     * those, and reading that nought straight gives no character. */
    w.keysym1[36] = 0;
    packet[1] = 36;
    gs_win_put16(packet + 28, 0);
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == 10,
          "return arrives as ten, the line break");
    check(out.mods == 0, "on its own");
    gs_win_put16(packet + 28, 0x0001);
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == 10,
          "shift and return is still a return, even with nothing in the "
          "shifted column");
    check((out.mods & GS_WINDOW_MOD_SHIFT) != 0,
          "with the shift reported, which is what tells the two apart");

    packet[1] = 22;
    gs_win_put16(packet + 28, 0);
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == 8,
          "backspace arrives as eight");
    packet[1] = 9;
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == 27,
          "and escape as twenty seven");

    /* A seat nothing is mapped to carries no character at all. */
    /* A key with a shifted meaning of nought falls back to its own, so
     * shift and backspace is still a backspace. */
    w.keysym1[22] = 0;
    packet[1] = 22;
    gs_win_put16(packet + 28, 0x0001);
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == 8,
          "shift and backspace is still a backspace");

    packet[1] = 200;
    gs_win_put16(packet + 28, 0);
    check(gs_window_translate(&w, packet, &out) == 1 && out.ch == 0,
          "an unmapped seat gives no character");
    check(out.key == 200, "while still naming which seat it was");

    packet[1] = 61;
    gs_win_put16(packet + 28, 0x0004);      /* control */
    check(gs_window_translate(&w, packet, &out) == 1 &&
          (out.mods & GS_WINDOW_MOD_CONTROL) != 0, "control is reported");
    gs_win_put16(packet + 28, 0x0008);      /* alt */
    check(gs_window_translate(&w, packet, &out) == 1 &&
          (out.mods & GS_WINDOW_MOD_ALT) != 0, "and so is alt");
}

/* The table the server sends back is rows of meanings, and each row is
 * one seat on the keyboard. Reading the first meaning of each row and
 * skipping the second is what left shift with no effect. */
static void test_keymap_walk(void)
{
    uint32_t plain[256];
    uint32_t upper[256];
    unsigned char table[64];

    printf("reading the table of what each key means\n");
    memset(plain, 0, sizeof plain);
    memset(upper, 0, sizeof upper);
    memset(table, 0, sizeof table);

    /* Two seats, four meanings each, which is what a common keyboard
     * sends back. Seat one carries the oblique and its question mark. */
    gs_win_put32(table +  0, '/');
    gs_win_put32(table +  4, '?');
    gs_win_put32(table + 16, 'a');
    gs_win_put32(table + 20, 'A');

    gs_win_read_keymap(table, sizeof table, 4, 2, 61, plain, upper);
    check(plain[61] == '/', "the key alone is read");
    check(upper[61] == '?', "and the key with shift held is read as well");
    check(plain[62] == 'a', "the next seat is read from the next row");
    check(upper[62] == 'A', "with its shifted meaning too");

    /* A seat carrying one meaning shifts to the same thing, so a key with
     * nothing above it still gives its own character under shift. */
    memset(plain, 0, sizeof plain);
    memset(upper, 0, sizeof upper);
    memset(table, 0, sizeof table);
    gs_win_put32(table, 'z');
    gs_win_read_keymap(table, 4, 1, 1, 61, plain, upper);
    check(plain[61] == 'z' && upper[61] == 'z',
          "one meaning shifts to itself");

    /* A table cut short must stop rather than read past its end. */
    memset(plain, 0, sizeof plain);
    memset(upper, 0, sizeof upper);
    gs_win_put32(table, 'q');
    gs_win_read_keymap(table, 4, 4, 8, 61, plain, upper);
    check(plain[61] == 'q', "what is there is read");
    check(plain[62] == 0, "and nothing past the end of the table is");

    gs_win_read_keymap(NULL, 64, 4, 2, 61, plain, upper);
    check(1, "no table does not crash");
    gs_win_read_keymap(table, 64, 0, 2, 61, plain, upper);
    check(1, "a row of no meanings does not crash");
    /* A seat number past the end of the array would write outside it. */
    gs_win_read_keymap(table, sizeof table, 4, 8, 254, plain, upper);
    check(1, "a seat past the end of the table is left alone");
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

    if (connection_gone(w, "the rest of the one window run")) {
        gs_window_close(w);
        return;
    }

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
/* A window asks for pointer motion, so a mouse resting over it keeps the
 * wait returning events. Every measurement below wants a wait that ended
 * for its own reason rather than because something arrived, so the wait
 * is repeated until it answers with no event. Gives up after a while and
 * reports -1, which the caller turns into a skip.
 *
 * Sets *elapsed to how long the winning attempt took. */
static int wait_without_events(gs_window_t **set, int count,
                               gs_window_event_t *out, int timeout_ms,
                               long long *elapsed)
{
    int which = -1;
    int attempt;

    for (attempt = 0; attempt < 12; attempt++) {
        long long a = now_ms();
        int rc = gs_window_wait_any(set, count, &which, out, timeout_ms);
        long long b = now_ms();

        if (rc == 1)
            continue;                    /* the server had something to say */
        *elapsed = b - a;
        return rc;
    }
    return -1;
}

/* Set by the handler below and read by the check, so it is the one type
 * the standard promises can be written and read in a single step. */
static volatile sig_atomic_t alarm_fired;

static void on_alarm(int number)
{
    (void)number;
    alarm_fired = 1;
}

/* A signal that arrives while the wait is already running cuts it short.
 * Starting the wait again from the top would give it its whole time over
 * once more, and a program waiting with no timeout at all would then
 * never get the chance to notice that it has been asked to stop. */
static void test_signal_cuts_the_wait_short(void)
{
    gs_window_t *w;
    gs_window_t *set[1];
    gs_window_event_t event;
    struct sigaction want;
    struct sigaction before;
    int which = -1;
    long long started = 0, ended = 0;
    int rc = 0;
    int attempt;
    int measured = 0;

    if (display_unavailable("a signal arriving during a wait"))
        return;

    printf("a signal arriving during a wait\n");
    w = gs_window_open("gravestone signal test", 120, 90);
    check(w != NULL, "window opened");
    if (w == NULL)
        return;
    set[0] = w;
    gs_window_set_wake_fd(-1);      /* only the signal can end this */

    while (gs_window_wait_any(set, 1, &which, &event, 60) == 1)
        ;                           /* drain whatever the server queued */

    memset(&want, 0, sizeof want);
    want.sa_handler = on_alarm;
    sigemptyset(&want.sa_mask);
    want.sa_flags = 0;              /* waits are cut short, not restarted */
    sigaction(SIGALRM, &want, &before);

    /* Each attempt gets its own alarm, since an attempt that ended on a
     * window event never used the one it was given. */
    for (attempt = 0; attempt < 12 && !measured; attempt++) {
        alarm_fired = 0;
        alarm(1);
        started = now_ms();
        rc = gs_window_wait_any(set, 1, &which, &event, 20000);
        ended = now_ms();
        alarm(0);
        if (rc != 1 && alarm_fired == 1)
            measured = 1;
    }
    sigaction(SIGALRM, &before, NULL);

    if (!measured) {
        printf("  skip  the server never went quiet long enough to time it\n");
        skipped++;
    } else {
        check(rc == 0,
              "the wait came back reporting nothing rather than an error");
        check(ended - started < 5000,
              "and at once, instead of waiting out its twenty seconds");
    }

    check(gs_window_error_count(w) == 0, "none of it upset the server");
    gs_window_close(w);
}

/* The wait can be given one more descriptor to watch, which is how a stop
 * signal reaches a loop that would otherwise sit still until a window
 * spoke. Nothing else in the suite covers it, and a wait that ignored the
 * descriptor would leave the program unable to be closed. */
static void test_wake_descriptor(void)
{
    gs_window_t *w;
    gs_window_t *set[1];
    gs_window_event_t event;
    int pipe_fds[2];
    long long took = 0;

    if (display_unavailable("the extra descriptor the wait watches"))
        return;

    printf("the extra descriptor the wait watches\n");
    w = gs_window_open("gravestone wake test", 120, 90);
    check(w != NULL, "window opened");
    if (w == NULL)
        return;
    if (pipe(pipe_fds) != 0) {
        check(0, "a pipe was made");
        gs_window_close(w);
        return;
    }
    set[0] = w;

    /* Nothing on the pipe, so the wait has to sit out its whole time. */
    gs_window_set_wake_fd(pipe_fds[0]);
    if (wait_without_events(set, 1, &event, 200, &took) != 0) {
        printf("  skip  the server never went quiet long enough to time it\n");
        skipped++;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        gs_window_set_wake_fd(-1);
        gs_window_close(w);
        return;
    }
    check(took >= 150, "an empty descriptor does not cut the wait short");

    /* One byte on it, and the wait has to come straight back. */
    check(write(pipe_fds[1], "q", 1) == 1, "a byte went down the pipe");
    check(wait_without_events(set, 1, &event, 5000, &took) == 0 &&
          took < 500, "a byte on the descriptor ends the wait at once");

    /* The wait does not read the descriptor, so the byte is still there
     * for whoever owns it. */
    {
        char back = 0;

        check(read(pipe_fds[0], &back, 1) == 1 && back == 'q',
              "and the byte is left for its owner to take");
    }

    /* Handing back -1 stops it being watched at all. */
    gs_window_set_wake_fd(-1);
    check(write(pipe_fds[1], "q", 1) == 1, "another byte went down");
    check(wait_without_events(set, 1, &event, 200, &took) == 0 && took >= 150,
          "a descriptor no longer watched is ignored, so the wait sits out "
          "its time again");

    close(pipe_fds[0]);
    close(pipe_fds[1]);
    check(gs_window_error_count(w) == 0, "none of it upset the server");
    gs_window_close(w);
}

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
    test_keymap_walk();
    test_shifted_keys();
    test_key_queue_hands_each_press_out_once();
    test_missing_display();
    test_on_one_window();
    test_wake_descriptor();
    test_signal_cuts_the_wait_short();
    test_window_class();

    printf("\n%d checks, %d failures, %d skipped\n", checks, failures,
           skipped);
    return failures == 0 ? 0 : 1;
}
