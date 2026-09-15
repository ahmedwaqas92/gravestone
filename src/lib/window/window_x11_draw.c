/* window_x11_draw.c
 *
 * Painting, and turning the packets the server sends back into the small
 * set of events the rest of the program understands.
 */
#include "window.h"
#include "window_internal.h"
#include "gravestone.h"
#include "log.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t draw_target(gs_window_t *window);


/* The graphics context holds one colour at a time, so a fill only has to
 * announce a change when the colour actually differs from the last one. */
int gs_win_set_foreground(struct gs_window *w, uint32_t colour)
{
    unsigned char req[16];

    if (w->gc_foreground == colour)
        return GS_OK;

    memset(req, 0, sizeof req);
    req[0] = X_CHANGE_GC;
    gs_win_put16(req + 2, 4);
    gs_win_put32(req + 4, w->gc_id);
    gs_win_put32(req + 8, 0x00000004u);     /* foreground */
    gs_win_put32(req + 12, colour);

    w->gc_foreground = colour;
    return gs_win_send_request(w, req, sizeof req);
}

void gs_window_fill(gs_window_t *window, int x, int y, int w, int h,
                    unsigned int colour)
{
    unsigned char req[20];

    if (window == NULL || window->broken || w <= 0 || h <= 0)
        return;
    if (w > 65535 || h > 65535 || x < -32768 || x > 32767 ||
        y < -32768 || y > 32767)
        return;

    if (gs_win_set_foreground(window, colour & 0x00ffffffu) != GS_OK)
        return;

    memset(req, 0, sizeof req);
    req[0] = X_POLY_FILL_RECTANGLE;
    gs_win_put16(req + 2, 5);               /* three fixed words plus one rect */
    gs_win_put32(req + 4, draw_target(window));
    gs_win_put32(req + 8, window->gc_id);
    gs_win_put16(req + 12, (uint16_t)(int16_t)x);
    gs_win_put16(req + 14, (uint16_t)(int16_t)y);
    gs_win_put16(req + 16, (uint16_t)w);
    gs_win_put16(req + 18, (uint16_t)h);

    gs_win_send_request(window, req, sizeof req);
}

void gs_window_text(gs_window_t *window, int x, int y, const char *text,
                    unsigned int colour)
{
    unsigned char req[16];
    unsigned char item[2];
    unsigned char pad[4] = {0, 0, 0, 0};
    size_t len;
    size_t bytes;
    int padding;

    if (window == NULL || text == NULL || window->broken)
        return;

    len = strlen(text);
    if (len == 0)
        return;
    /* A text item carries its length in one byte and 255 is reserved as a
     * marker, so a longer string has to be split. */
    if (len > 254)
        len = 254;

    if (gs_win_set_foreground(window, colour & 0x00ffffffu) != GS_OK)
        return;

    bytes = 2 + len;
    padding = gs_win_pad4((int)bytes);

    memset(req, 0, sizeof req);
    req[0] = X_POLY_TEXT8;
    gs_win_put16(req + 2, (uint16_t)(4 + (bytes + (size_t)padding) / 4));
    gs_win_put32(req + 4, draw_target(window));
    gs_win_put32(req + 8, window->gc_id);
    gs_win_put16(req + 12, (uint16_t)(int16_t)x);
    gs_win_put16(req + 14, (uint16_t)(int16_t)y);

    item[0] = (unsigned char)len;
    item[1] = 0;                        /* no extra gap before the string */

    window->sequence++;
    if (gs_win_write_all(window, req, sizeof req) != GS_OK) return;
    if (gs_win_write_all(window, item, sizeof item) != GS_OK) return;
    if (gs_win_write_all(window, text, len) != GS_OK) return;
    if (padding > 0)
        gs_win_write_all(window, pad, (size_t)padding);
}

void gs_window_flush(gs_window_t *window)
{
    (void)window;   /* every request goes straight down the socket */
}

/* Turns one queued packet into an event. Returns 1 when out was filled and
 * 0 when the packet carried nothing this program cares about. */
int gs_window_translate(gs_window_t *window,
                        const unsigned char *packet,
                        gs_window_event_t *out)
{
    /* The top bit marks an event the server sent on someone else's
     * request, which makes no difference to what it means. */
    unsigned char code = packet[0] & 0x7f;

    switch (code) {
    case X_EVT_EXPOSE:
        window->exposed = 1;
        out->kind = GS_WINDOW_EVENT_EXPOSE;
        return 1;

    case X_EVT_CONFIGURE: {
        int nw = gs_win_get16(packet + 20);
        int nh = gs_win_get16(packet + 22);
        if (nw == window->width && nh == window->height)
            return 0;
        window->width = nw;
        window->height = nh;
        out->kind = GS_WINDOW_EVENT_RESIZE;
        out->width = nw;
        out->height = nh;
        return 1;
    }

    case X_EVT_KEY_PRESS: {
        /* The keys held down while this one went down. Bit nought is
         * shift, bit one is the caps lock, and bit two is control. */
        uint16_t held = gs_win_get16(packet + 28);
        int shift = (held & 0x0001u) != 0;
        int lock  = (held & 0x0002u) != 0;
        uint32_t plain = window->keysym0[packet[1]];
        uint32_t upper = window->keysym1[packet[1]];
        uint32_t sym;

        /* Most keys carry nothing in the shifted column, written as
         * nought, because holding shift changes nothing about them. The
         * return key is one of those, so reading the column straight gave
         * no character at all and shift with return did nothing. */
        if (upper == 0)
            upper = plain;

        out->kind = GS_WINDOW_EVENT_KEY;
        out->key = packet[1];            /* raw keycode */
        out->ch = 0;
        out->mods = 0;
        if (shift)
            out->mods |= GS_WINDOW_MOD_SHIFT;
        if (held & 0x0004u)
            out->mods |= GS_WINDOW_MOD_CONTROL;
        if (held & 0x0008u)
            out->mods |= GS_WINDOW_MOD_ALT;

        /* The caps lock only reaches letters, so a locked keyboard still
         * gives a digit rather than the mark above it. */
        if (lock && !shift && plain >= 'a' && plain <= 'z')
            sym = upper;
        else if (lock && shift && plain >= 'a' && plain <= 'z')
            sym = plain;
        else
            sym = shift ? upper : plain;

        if (sym >= 0x20 && sym <= 0x7e)
            out->ch = (int)sym;          /* plain printable character */
        else if (sym == 0xff08)
            out->ch = 8;                 /* backspace */
        else if (sym == 0xff1b)
            out->ch = 27;                /* escape */
        else if (sym == 0xff0d || sym == 0xff8d)
            out->ch = 10;                /* return, and the one on the pad */
        else if (sym == 0xff09)
            out->ch = 9;                 /* tab */
        return 1;
    }

    case X_EVT_BUTTON_PRESS:
        out->kind = GS_WINDOW_EVENT_CLICK;
        out->button = packet[1];
        out->x = (int)(int16_t)gs_win_get16(packet + 24);
        out->y = (int)(int16_t)gs_win_get16(packet + 26);
        return 1;

    case X_EVT_BUTTON_RELEASE:
        out->kind = GS_WINDOW_EVENT_RELEASE;
        out->button = packet[1];
        out->x = (int)(int16_t)gs_win_get16(packet + 24);
        out->y = (int)(int16_t)gs_win_get16(packet + 26);
        return 1;

    case X_EVT_MOTION:
        out->kind = GS_WINDOW_EVENT_MOVE;
        out->x = (int)(int16_t)gs_win_get16(packet + 24);
        out->y = (int)(int16_t)gs_win_get16(packet + 26);
        return 1;

    case X_EVT_CLIENT_MESSAGE:
        if (gs_win_get32(packet + 12) != window->atom_wm_delete)
            return 0;
        out->kind = GS_WINDOW_EVENT_CLOSE;
        return 1;

    default:
        return 0;
    }
}

int gs_window_wait_event(gs_window_t *window, gs_window_event_t *out,
                         int timeout_ms)
{
    unsigned char packet[32];
    struct pollfd pfd;

    if (window == NULL || out == NULL)
        return GS_ERR_ARG;
    if (window->broken)
        return GS_ERR_IO;

    memset(out, 0, sizeof *out);

    for (;;) {
        while (gs_win_queue_pop(window, packet))
            if (gs_window_translate(window, packet, out))
                return 1;

        pfd.fd = window->fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        {
            int ready = poll(&pfd, 1, timeout_ms);
            if (ready < 0) {
                /* Cut short by a signal, which reads as a wait that found
                 * nothing. Going round again would start the timeout over
                 * and the caller would never get its turn. */
                if (errno == EINTR)
                    return 0;
                gs_log_error("window: poll failed: %s", strerror(errno));
                window->broken = 1;
                return GS_ERR_IO;
            }
            if (ready == 0)
                return 0;
        }

        if (gs_win_read_packet(window, NULL) < 0)
            return GS_ERR_IO;
    }
}

int gs_window_read_pixel(gs_window_t *window, int x, int y,
                         unsigned int *out)
{
    unsigned char req[20];
    unsigned char reply[32];
    unsigned char pixel[4] = {0, 0, 0, 0};
    size_t got = 0;

    if (window == NULL || out == NULL)
        return GS_ERR_ARG;
    if (window->broken)
        return GS_ERR_IO;
    if (x < 0 || y < 0 || x >= window->width || y >= window->height)
        return GS_ERR_ARG;

    memset(req, 0, sizeof req);
    req[0] = X_GET_IMAGE;
    req[1] = 2;                          /* format 2 is ZPixmap */
    gs_win_put16(req + 2, 5);
    gs_win_put32(req + 4, window->window_id);
    gs_win_put16(req + 8, (uint16_t)(int16_t)x);
    gs_win_put16(req + 10, (uint16_t)(int16_t)y);
    gs_win_put16(req + 12, 1);              /* one pixel wide */
    gs_win_put16(req + 14, 1);              /* one pixel tall */
    gs_win_put32(req + 16, 0xffffffffu);    /* every plane */

    window->sequence++;
    if (gs_win_write_all(window, req, sizeof req) != GS_OK)
        return GS_ERR_IO;

    if (gs_win_await_reply_data(window, reply, pixel, sizeof pixel,
                             &got) != GS_OK)
        return GS_ERR;
    if (got < 4)
        return GS_ERR;

    *out = gs_win_get32(pixel) & 0x00ffffffu;
    return GS_OK;
}

/* Where drawing lands. Once a frame pixmap exists every operation goes
 * there, and the window only changes when gs_window_present copies the
 * finished frame across. */
static uint32_t draw_target(gs_window_t *window)
{
    return window->pixmap_id != 0 ? window->pixmap_id : window->window_id;
}

/* Makes sure the frame pixmap matches the canvas size. Failure leaves
 * pixmap_id at zero and drawing falls back to the window directly, which
 * is the old behaviour rather than a dead end. */
static void ensure_pixmap(gs_window_t *window, int w, int h)
{
    unsigned char req[16];

    if (window->pixmap_id != 0 && window->pixmap_w == w &&
        window->pixmap_h == h)
        return;

    if (window->pixmap_id != 0) {
        memset(req, 0, sizeof req);
        req[0] = X_FREE_PIXMAP;
        gs_win_put16(req + 2, 2);
        gs_win_put32(req + 4, window->pixmap_id);
        window->sequence++;
        gs_win_write_all(window, req, 8);
        window->pixmap_id = 0;
    }

    memset(req, 0, sizeof req);
    req[0] = X_CREATE_PIXMAP;
    req[1] = window->root_depth;
    gs_win_put16(req + 2, 4);
    gs_win_put32(req + 4, gs_win_next_id(window));
    gs_win_put32(req + 8, window->window_id);
    gs_win_put16(req + 12, (uint16_t)w);
    gs_win_put16(req + 14, (uint16_t)h);

    window->sequence++;
    if (gs_win_write_all(window, req, sizeof req) == GS_OK) {
        window->pixmap_id = gs_win_get32(req + 4);
        window->pixmap_w = w;
        window->pixmap_h = h;
    }
}

int gs_window_has_frame(const gs_window_t *window)
{
    return window != NULL && window->pixmap_id != 0;
}

int gs_window_present(gs_window_t *window)
{
    unsigned char req[28];

    if (window == NULL)
        return GS_ERR_ARG;
    if (window->broken)
        return GS_ERR_IO;
    if (window->pixmap_id == 0)
        return GS_OK;                 /* nothing buffered, nothing to show */

    memset(req, 0, sizeof req);
    req[0] = X_COPY_AREA;
    gs_win_put16(req + 2, 7);
    gs_win_put32(req + 4, window->pixmap_id);
    gs_win_put32(req + 8, window->window_id);
    gs_win_put32(req + 12, window->gc_id);
    gs_win_put16(req + 16, 0);        /* from 0,0 */
    gs_win_put16(req + 18, 0);
    gs_win_put16(req + 20, 0);        /* to 0,0 */
    gs_win_put16(req + 22, 0);
    gs_win_put16(req + 24, (uint16_t)window->pixmap_w);
    gs_win_put16(req + 26, (uint16_t)window->pixmap_h);

    window->sequence++;
    return gs_win_write_all(window, req, sizeof req);
}

/* Sends rows first through last of the canvas to the drawing target in
 * bands small enough for one request each. */
static int blit_rows(gs_window_t *window, const unsigned int *pixels,
                     int w, int first, int last)
{
    unsigned char header[24];
    unsigned char *band;
    size_t row_bytes;
    size_t usable;
    int rows_per_band;
    int y;
    int swap;

    /* Depth 24 travels as four bytes per pixel, so a row of w pixels is
     * already a multiple of four and needs no padding. */
    row_bytes = (size_t)w * 4u;

    usable = window->max_request_bytes > sizeof header
             ? window->max_request_bytes - sizeof header : 0;
    if (usable < row_bytes) {
        gs_log_error("window: a single row of %d pixels will not fit in one "
                     "request", w);
        return GS_ERR;
    }

    rows_per_band = (int)(usable / row_bytes);
    if (rows_per_band > last - first + 1)
        rows_per_band = last - first + 1;

    band = malloc(row_bytes * (size_t)rows_per_band);
    if (band == NULL)
        return GS_ERR_MEM;

    swap = window->image_byte_order != 0;

    for (y = first; y <= last; y += rows_per_band) {
        int rows = last - y + 1 < rows_per_band ? last - y + 1
                                                : rows_per_band;
        size_t data_len = row_bytes * (size_t)rows;
        int i;
        int count = w * rows;

        for (i = 0; i < count; i++) {
            unsigned int c = pixels[(size_t)y * (size_t)w + (size_t)i];
            unsigned char *p = band + (size_t)i * 4u;
            if (swap) {
                p[0] = 0;
                p[1] = (unsigned char)((c >> 16) & 0xff);
                p[2] = (unsigned char)((c >> 8) & 0xff);
                p[3] = (unsigned char)(c & 0xff);
            } else {
                p[0] = (unsigned char)(c & 0xff);
                p[1] = (unsigned char)((c >> 8) & 0xff);
                p[2] = (unsigned char)((c >> 16) & 0xff);
                p[3] = 0;
            }
        }

        memset(header, 0, sizeof header);
        header[0] = X_PUT_IMAGE;
        header[1] = 2;                   /* format 2 is ZPixmap */
        gs_win_put16(header + 2, (uint16_t)(6u + data_len / 4u));
        gs_win_put32(header + 4, draw_target(window));
        gs_win_put32(header + 8, window->gc_id);
        gs_win_put16(header + 12, (uint16_t)w);
        gs_win_put16(header + 14, (uint16_t)rows);
        gs_win_put16(header + 16, 0);       /* destination x */
        gs_win_put16(header + 18, (uint16_t)(int16_t)y);
        header[20] = 0;                  /* left pad, zero for ZPixmap */
        header[21] = window->root_depth;

        window->sequence++;
        if (gs_win_write_all(window, header, sizeof header) != GS_OK ||
            gs_win_write_all(window, band, data_len) != GS_OK) {
            free(band);
            return GS_ERR_IO;
        }
    }

    free(band);
    return GS_OK;
}

int gs_window_blit(gs_window_t *window, const unsigned int *pixels,
                   int w, int h)
{
    if (window == NULL || pixels == NULL || w <= 0 || h <= 0)
        return GS_ERR_ARG;
    if (window->broken)
        return GS_ERR_IO;

    ensure_pixmap(window, w, h);
    return blit_rows(window, pixels, w, 0, h - 1);
}

int gs_window_blit_rect(gs_window_t *window, const unsigned int *pixels,
                        int w, int h, int y, int rows_high)
{
    int last;

    if (window == NULL || pixels == NULL || w <= 0 || h <= 0)
        return GS_ERR_ARG;
    if (window->broken)
        return GS_ERR_IO;

    /* Without a frame of this size to patch, the whole picture goes. */
    if (window->pixmap_id == 0 || window->pixmap_w != w ||
        window->pixmap_h != h)
        return gs_window_blit(window, pixels, w, h);

    if (y < 0) {
        rows_high += y;
        y = 0;
    }
    last = y + rows_high - 1;
    if (last > h - 1)
        last = h - 1;
    if (rows_high <= 0 || y > last)
        return GS_OK;

    return blit_rows(window, pixels, w, y, last);
}


/* Set by whoever has something other than a window to answer. Watched by
 * the wait, never read from and never closed here. */
static int wake_fd = -1;

void gs_window_set_wake_fd(int fd)
{
    wake_fd = fd;
}

int gs_window_wait_any(gs_window_t **windows, int count, int *which,
                       gs_window_event_t *out, int timeout_ms)
{
    struct pollfd fds[9];           /* eight windows and the wake pipe */
    int watched;
    unsigned char packet[32];
    int i;

    if (windows == NULL || out == NULL || which == NULL || count <= 0 ||
        count > (int)(sizeof fds / sizeof fds[0]) - 1)
        return GS_ERR_ARG;

    memset(out, 0, sizeof *out);

    for (;;) {
        int alive = 0;

        /* A connection that has already failed is never polled again,
         * since poll would report it ready for ever and the caller would
         * spin on an error it cannot clear. */
        for (i = 0; i < count; i++)
            if (windows[i] != NULL && !windows[i]->broken)
                alive++;
        if (alive == 0)
            return GS_ERR_IO;

        /* Anything already queued goes first, so a burst is drained before
         * the program waits again. */
        for (i = 0; i < count; i++) {
            if (windows[i] == NULL || windows[i]->broken)
                continue;
            while (gs_win_queue_pop(windows[i], packet))
                if (gs_window_translate(windows[i], packet, out)) {
                    *which = i;
                    return 1;
                }
        }

        for (i = 0; i < count; i++) {
            int usable = windows[i] != NULL && !windows[i]->broken;

            fds[i].fd = usable ? windows[i]->fd : -1;
            fds[i].events = POLLIN;
            fds[i].revents = 0;
        }
        watched = count;
        if (wake_fd >= 0) {
            fds[watched].fd = wake_fd;
            fds[watched].events = POLLIN;
            fds[watched].revents = 0;
            watched++;
        }

        {
            int ready = poll(fds, (nfds_t)watched, timeout_ms);
            if (ready < 0) {
                /* A signal cut the wait short. Going round again would
                 * start the whole timeout over and never hand control
                 * back, so this reads as a wait that found nothing and
                 * the caller decides what to do next. */
                if (errno == EINTR)
                    return 0;
                gs_log_error("window: poll failed: %s", strerror(errno));
                return GS_ERR_IO;
            }
            if (ready == 0)
                return 0;
        }

        /* Something arrived on the extra descriptor, so the caller is
         * told at once rather than after the windows are drained. Nothing
         * is read from it here, since it belongs to the caller. */
        if (wake_fd >= 0 && (fds[count].revents & (POLLIN | POLLHUP)) != 0)
            return 0;

        for (i = 0; i < count; i++)
            if ((fds[i].revents & POLLIN) && windows[i] != NULL)
                if (gs_win_read_packet(windows[i], NULL) < 0) {
                    *which = i;
                    return GS_ERR_IO;
                }
    }
}
