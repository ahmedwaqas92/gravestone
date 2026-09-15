/* window_win32_test.c
 *
 * Runs on Windows against a real desktop. Two adversarial passes. The
 * first drives the message handler with the messages Windows itself
 * sends and looks for events that go missing, arrive out of order or
 * carry the wrong character. The second writes to the frame and reads it
 * back, looking for rows in the wrong order and for edges that are not
 * refused.
 */
#include "window.h"
#include "window_internal.h"
#include "gravestone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Sends the message straight to the handler, which is how Windows
 * delivers a keystroke or a click when the queue is being pumped. */
static void post(gs_window_t *w, UINT message, WPARAM a, LPARAM b)
{
    SendMessage(w->hwnd, message, a, b);
}

static int take(gs_window_t *w, gs_window_event_t *out)
{
    memset(out, 0, sizeof *out);
    return gs_win_queue_take(w, out);
}

static void drain(gs_window_t *w)
{
    gs_window_event_t e;

    while (take(w, &e))
        ;
}

/* ---- first pass: the events ---- */

static void test_characters(gs_window_t *w)
{
    gs_window_event_t e;

    printf("keys carry the character the interface reads\n");
    drain(w);

    post(w, WM_KEYDOWN, VK_ESCAPE, 0);
    check(take(w, &e) && e.kind == GS_WINDOW_EVENT_KEY && e.ch == 27,
          "escape arrives as 27");

    post(w, WM_KEYDOWN, VK_BACK, 0);
    check(take(w, &e) && e.ch == 8, "backspace arrives as 8");

    post(w, WM_KEYDOWN, 'A', 0);
    check(take(w, &e) && e.ch == 'a', "an unshifted letter arrives lower case");

    post(w, WM_KEYDOWN, VK_F5, 0);
    check(take(w, &e) && e.kind == GS_WINDOW_EVENT_KEY && e.ch == 0,
          "a function key carries no character");
    check(e.key == VK_F5, "and still reports its raw code");
}

static void test_queue_overflows(gs_window_t *w)
{
    gs_window_event_t e;
    int i;
    int taken = 0;
    int ordered = 1;

    printf("a flood of clicks does not corrupt the queue\n");
    drain(w);

    /* More than the queue holds, so the tail has to be dropped rather
     * than written over an event still waiting. */
    for (i = 0; i < GS_WIN_QUEUE_LEN + 16; i++)
        post(w, WM_LBUTTONDOWN, 0, MAKELPARAM(i, i * 2));

    for (i = 0; take(w, &e); i++) {
        taken++;
        if (e.x != i || e.y != i * 2)
            ordered = 0;
    }
    check(taken == GS_WIN_QUEUE_LEN, "the queue holds exactly its length");
    check(ordered, "and every event kept is the one that came in that place");
}

static void test_queue_wraps(gs_window_t *w)
{
    gs_window_event_t e;
    int i;
    int ordered = 1;

    printf("the ring keeps its order across the wrap\n");
    drain(w);

    for (i = 0; i < GS_WIN_QUEUE_LEN - 4; i++)
        post(w, WM_LBUTTONDOWN, 0, MAKELPARAM(i, 0));
    for (i = 0; i < GS_WIN_QUEUE_LEN - 8; i++)
        take(w, &e);
    /* The head now sits near the end, so these land past the fold. */
    for (i = 100; i < 108; i++)
        post(w, WM_LBUTTONDOWN, 0, MAKELPARAM(i, 0));

    for (i = GS_WIN_QUEUE_LEN - 8; take(w, &e); i++) {
        int want = i < GS_WIN_QUEUE_LEN - 4 ? i : 100 + i - (GS_WIN_QUEUE_LEN - 4);

        if (e.x != want)
            ordered = 0;
    }
    check(ordered, "events written past the fold still come out in turn");
}

static void test_awkward_messages(gs_window_t *w)
{
    gs_window_event_t e;
    int before_w = gs_window_width(w);
    int before_h = gs_window_height(w);

    printf("the messages that arrive at awkward moments\n");
    drain(w);

    /* Minimising sends a size of nothing by nothing. Treating that as a
     * real resize would tell the interface to lay itself out in no room
     * at all. */
    post(w, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(0, 0));
    check(take(w, &e) == 0, "a size of zero raises no resize");
    check(gs_window_width(w) == before_w && gs_window_height(w) == before_h,
          "and the size on record is untouched");

    /* The same size again is not a change and should stay quiet. */
    post(w, WM_SIZE, SIZE_RESTORED, MAKELPARAM(before_w, before_h));
    check(take(w, &e) == 0, "resizing to the size it already has is quiet");

    post(w, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(50, 60));
    check(take(w, &e) && e.kind == GS_WINDOW_EVENT_CLICK && e.button == 4,
          "the wheel forward arrives as button four");
    post(w, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), MAKELPARAM(50, 60));
    check(take(w, &e) && e.button == 5, "and the wheel back as button five");

    post(w, WM_CLOSE, 0, 0);
    check(take(w, &e) && e.kind == GS_WINDOW_EVENT_CLOSE,
          "the close button raises a close");
    check(IsWindow(w->hwnd), "and the window is still standing afterwards");
}

/* ---- second pass: the frame ---- */

static void test_row_order(gs_window_t *w)
{
    unsigned int *canvas;
    unsigned int got = 0;
    int x, y;
    const int cw = 40, chh = 30;

    printf("rows land the way up the caller wrote them\n");
    canvas = malloc((size_t)cw * (size_t)chh * sizeof *canvas);
    if (canvas == NULL) {
        check(0, "canvas allocated");
        return;
    }
    for (y = 0; y < chh; y++)
        for (x = 0; x < cw; x++)
            canvas[y * cw + x] = (unsigned int)(y * 1000 + x);

    check(gs_window_blit(w, canvas, cw, chh) == GS_OK, "the canvas went in");
    check(gs_window_read_pixel(w, 0, 0, &got) == GS_OK && got == 0,
          "the first row read back first");
    check(gs_window_read_pixel(w, 3, chh - 1, &got) == GS_OK &&
          got == (unsigned int)((chh - 1) * 1000 + 3),
          "the last row read back last");

    printf("the edges of the frame are refused\n");
    check(gs_window_read_pixel(w, -1, 0, &got) == GS_ERR_ARG,
          "a negative column is refused");
    check(gs_window_read_pixel(w, cw, 0, &got) == GS_ERR_ARG,
          "one past the right edge is refused");
    check(gs_window_read_pixel(w, 0, chh, &got) == GS_ERR_ARG,
          "one past the bottom edge is refused");
    check(gs_window_read_pixel(w, 0, 0, NULL) == GS_ERR_ARG,
          "nowhere to put the answer is refused");

    free(canvas);
}

static void test_partial_rows(gs_window_t *w)
{
    unsigned int *canvas;
    unsigned int got = 0;
    int i;
    const int cw = 40, chh = 30;

    printf("a band of rows on its own\n");
    canvas = malloc((size_t)cw * (size_t)chh * sizeof *canvas);
    if (canvas == NULL) {
        check(0, "canvas allocated");
        return;
    }
    for (i = 0; i < cw * chh; i++)
        canvas[i] = 0x111111u;
    gs_window_blit(w, canvas, cw, chh);

    for (i = 0; i < cw * chh; i++)
        canvas[i] = 0x222222u;
    check(gs_window_blit_rect(w, canvas, cw, chh, 10, 5) == GS_OK,
          "five rows from ten went in");
    check(gs_window_read_pixel(w, 0, 9, &got) == GS_OK && got == 0x111111u,
          "the row above the band is untouched");
    check(gs_window_read_pixel(w, 0, 10, &got) == GS_OK && got == 0x222222u,
          "the first row of the band changed");
    check(gs_window_read_pixel(w, 0, 14, &got) == GS_OK && got == 0x222222u,
          "the last row of the band changed");
    check(gs_window_read_pixel(w, 0, 15, &got) == GS_OK && got == 0x111111u,
          "the row below the band is untouched");

    /* A band that hangs off the top and one that hangs off the bottom.
     * Either one copying its whole span would run outside the frame. */
    for (i = 0; i < cw * chh; i++)
        canvas[i] = 0x333333u;
    check(gs_window_blit_rect(w, canvas, cw, chh, -5, 8) == GS_OK,
          "a band starting above the top is clipped, not refused");
    check(gs_window_read_pixel(w, 0, 0, &got) == GS_OK && got == 0x333333u,
          "the rows that were on the frame still landed");
    check(gs_window_read_pixel(w, 0, 3, &got) == GS_OK && got == 0x111111u,
          "and the rows past the clip did not");

    for (i = 0; i < cw * chh; i++)
        canvas[i] = 0x444444u;
    check(gs_window_blit_rect(w, canvas, cw, chh, chh - 2, 40) == GS_OK,
          "a band running past the bottom is clipped");
    check(gs_window_read_pixel(w, 0, chh - 1, &got) == GS_OK &&
          got == 0x444444u, "the last row of the frame took it");

    check(gs_window_blit_rect(w, canvas, cw, chh, 40, 3) == GS_OK,
          "a band wholly below the frame writes nothing");
    check(gs_window_blit_rect(w, canvas, cw, chh, 5, 0) == GS_OK,
          "a band of no rows writes nothing");
    check(gs_window_blit_rect(w, NULL, cw, chh, 0, 1) == GS_ERR_ARG,
          "no pixels is refused");

    free(canvas);
}

static void test_frame_changes_size(gs_window_t *w)
{
    unsigned int *small;
    unsigned int got = 0;
    int i;

    printf("the frame follows the canvas when the canvas changes size\n");
    small = malloc(20u * 10u * sizeof *small);
    if (small == NULL) {
        check(0, "canvas allocated");
        return;
    }
    for (i = 0; i < 20 * 10; i++)
        small[i] = 0x556677u;

    check(gs_window_blit(w, small, 20, 10) == GS_OK, "a smaller canvas went in");
    check(gs_window_read_pixel(w, 19, 9, &got) == GS_OK && got == 0x556677u,
          "its far corner reads back");
    check(gs_window_read_pixel(w, 20, 0, &got) == GS_ERR_ARG,
          "the old wider frame is gone");

    /* A band sized for the old frame must not be copied into the new
     * one, since the rows are a different length now. */
    check(gs_window_blit_rect(w, small, 20, 10, 0, 2) == GS_OK,
          "a band matching the new size is taken");

    check(gs_window_blit(w, small, 0, 10) == GS_ERR_ARG, "no width is refused");
    check(gs_window_blit(w, small, 20, -1) == GS_ERR_ARG,
          "a negative height is refused");
    check(gs_window_blit(w, NULL, 20, 10) == GS_ERR_ARG, "no pixels is refused");

    free(small);
}

/* Reading the frame back gives the same array the caller wrote, so it
 * would agree with itself even if the bitmap were laid out bottom up.
 * Drawing with GDI settles it, since GDI goes through the bitmap's own
 * idea of which way up it is. */
static void test_gdi_agrees_which_way_up(gs_window_t *w)
{
    unsigned int *canvas;
    unsigned int got = 0;
    int i;
    const int cw = 40, chh = 30;

    printf("the drawing calls and the frame agree which way up it is\n");
    canvas = malloc((size_t)cw * (size_t)chh * sizeof *canvas);
    if (canvas == NULL) {
        check(0, "canvas allocated");
        return;
    }
    for (i = 0; i < cw * chh; i++)
        canvas[i] = 0x101010u;
    gs_window_blit(w, canvas, cw, chh);

    /* A band across the top, drawn the way the interface draws. */
    gs_window_fill(w, 0, 0, cw, 4, 0xff0000u);
    gs_window_flush(w);
    check(gs_window_read_pixel(w, 10, 1, &got) == GS_OK && got == 0xff0000u,
          "a band drawn at the top is at the top of the frame");
    check(gs_window_read_pixel(w, 10, chh - 1, &got) == GS_OK &&
          got == 0x101010u, "and the bottom of the frame is untouched");

    gs_window_fill(w, 0, chh - 4, cw, 4, 0x00ff00u);
    gs_window_flush(w);
    check(gs_window_read_pixel(w, 10, chh - 1, &got) == GS_OK &&
          got == 0x00ff00u, "a band drawn at the bottom is at the bottom");
    check(gs_window_read_pixel(w, 10, 1, &got) == GS_OK && got == 0xff0000u,
          "and the top band is still where it was");

    /* Text sits above its baseline, so a baseline near the bottom must
     * mark the rows above it and not the rows below. */
    gs_window_blit(w, canvas, cw, chh);
    gs_window_text(w, 1, chh - 2, "M", 0xffffffu);
    gs_window_flush(w);
    {
        int above = 0, below = 0;
        int x, y;

        for (y = 0; y < chh; y++)
            for (x = 0; x < cw; x++) {
                if (gs_window_read_pixel(w, x, y, &got) != GS_OK)
                    continue;
                if (got == 0x101010u)
                    continue;
                if (y < chh - 2)
                    above++;
                else
                    below++;
            }
        check(above > 0, "a letter marks the rows above its baseline");
        check(below == 0, "and marks nothing below it");
    }

    printf("drawing outside the frame is clipped, not written\n");
    gs_window_blit(w, canvas, cw, chh);
    gs_window_fill(w, -20, -20, 25, 25, 0x0000ffu);
    gs_window_flush(w);
    check(gs_window_read_pixel(w, 0, 0, &got) == GS_OK && got == 0x0000ffu,
          "the corner that was on the frame took the colour");
    check(gs_window_read_pixel(w, 10, 10, &got) == GS_OK && got == 0x101010u,
          "and the part past the clip did not");
    gs_window_fill(w, cw - 2, chh - 2, 500, 500, 0x00ffffu);
    gs_window_flush(w);
    check(gs_window_read_pixel(w, cw - 1, chh - 1, &got) == GS_OK &&
          got == 0x00ffffu, "a fill running off the far corner is clipped");
    gs_window_fill(w, 0, 0, 0, 0, 0x123456u);
    gs_window_fill(NULL, 0, 0, 5, 5, 0x123456u);
    gs_window_text(NULL, 0, 0, "x", 0u);
    gs_window_text(w, 0, 10, NULL, 0u);
    check(1, "an empty fill and a missing window do not crash");

    free(canvas);
}

static void test_font(gs_window_t *w)
{
    printf("the font measures the same every time\n");
    check(gs_window_font_height(w) > 0, "the font has a height");
    check(gs_window_font_ascent(w) > 0, "and an ascent");
    check(gs_window_text_width(w, "") == 0, "an empty line is no wide");
    check(gs_window_text_width(w, "ab") == 2 * gs_window_text_width(w, "a"),
          "two letters are twice one letter");
    check(gs_window_text_width(w, NULL) == 0, "no text is no wide");
    check(gs_window_text_width(NULL, "a") == 0, "no window is no wide");
}

static void test_no_window(void)
{
    unsigned int got = 0;
    unsigned int one = 0;

    printf("nothing at all is handed in\n");
    check(gs_window_blit(NULL, &one, 1, 1) == GS_ERR_ARG, "blit refuses it");
    check(gs_window_read_pixel(NULL, 0, 0, &got) == GS_ERR_ARG,
          "read refuses it");
    check(gs_window_present(NULL) == GS_ERR_ARG, "present refuses it");
    check(gs_window_has_frame(NULL) == 0, "and there is no frame on it");
    gs_window_flush(NULL);
    check(1, "flushing it does not crash");
}

int main(void)
{
    gs_window_t *w;

    printf("window win32\n\n");

    w = gs_window_open("gravestone win32 test", 200, 120);
    if (w == NULL) {
        printf("  FAIL  the window would not open\n");
        return 1;
    }
    check(gs_window_has_frame(w) == 0, "a new window has no frame yet");
    check(gs_window_present(w) == GS_OK, "presenting nothing is quiet");

    test_characters(w);
    test_queue_overflows(w);
    test_queue_wraps(w);
    test_awkward_messages(w);
    test_row_order(w);
    test_partial_rows(w);
    test_gdi_agrees_which_way_up(w);
    test_frame_changes_size(w);
    test_font(w);
    test_no_window();

    check(gs_window_present(w) == GS_OK, "the frame presents to the window");
    gs_window_close(w);
    check(1, "the window closed without complaint");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
