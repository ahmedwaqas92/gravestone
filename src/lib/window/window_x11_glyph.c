/* window_x11_glyph.c
 *
 * Writing larger than the screen will draw it.
 *
 * The server this program runs under carries one font, six pixels wide
 * and thirteen tall, and refuses every larger size asked of it. Every
 * printable shape is therefore read back once when the window opens, kept
 * as one bit per pixel, and a larger letter is built out of the small one
 * here rather than asked for.
 *
 * Reading happens once. Everything after that is arithmetic on bits, so
 * writing a line costs one image sent and no questions asked.
 */
#include "window_internal.h"
#include "gravestone.h"
#include "log.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Where a mark lands. The frame while there is one, so nothing half
 * finished reaches the screen. */
static uint32_t target_of(struct gs_window *w)
{
    return w->pixmap_id != 0 ? w->pixmap_id : w->window_id;
}

/* Fills part of a given sheet, which the ordinary fill cannot do since it
 * always draws on the frame. */
static int fill_on(struct gs_window *w, uint32_t sheet, int x, int y,
                   int wide, int tall, uint32_t colour)
{
    unsigned char req[20];

    if (gs_win_set_foreground(w, colour) != GS_OK)
        return GS_ERR;
    memset(req, 0, sizeof req);
    req[0] = X_POLY_FILL_RECTANGLE;
    gs_win_put16(req + 2, 5);
    gs_win_put32(req + 4, sheet);
    gs_win_put32(req + 8, w->gc_id);
    gs_win_put16(req + 12, (uint16_t)(int16_t)x);
    gs_win_put16(req + 14, (uint16_t)(int16_t)y);
    gs_win_put16(req + 16, (uint16_t)wide);
    gs_win_put16(req + 18, (uint16_t)tall);
    return gs_win_send_request(w, req, sizeof req);
}

/* Writes on a given sheet, for the same reason. */
static int text_on(struct gs_window *w, uint32_t sheet, int x, int y,
                   const char *text, uint32_t colour)
{
    unsigned char req[16];
    unsigned char item[2];
    unsigned char pad[4] = {0, 0, 0, 0};
    size_t len = strlen(text);
    size_t bytes;
    int padding;

    if (len == 0 || len > 254)
        return GS_ERR_ARG;
    if (gs_win_set_foreground(w, colour) != GS_OK)
        return GS_ERR;

    bytes = 2 + len;
    padding = (int)((4u - (bytes % 4u)) % 4u);

    memset(req, 0, sizeof req);
    req[0] = X_POLY_TEXT8;
    gs_win_put16(req + 2, (uint16_t)(4 + (bytes + (size_t)padding) / 4));
    gs_win_put32(req + 4, sheet);
    gs_win_put32(req + 8, w->gc_id);
    gs_win_put16(req + 12, (uint16_t)(int16_t)x);
    gs_win_put16(req + 14, (uint16_t)(int16_t)y);

    item[0] = (unsigned char)len;
    item[1] = 0;

    w->sequence++;
    if (gs_win_write_all(w, req, sizeof req) != GS_OK) return GS_ERR_IO;
    if (gs_win_write_all(w, item, sizeof item) != GS_OK) return GS_ERR_IO;
    if (gs_win_write_all(w, text, len) != GS_OK) return GS_ERR_IO;
    if (padding > 0 && gs_win_write_all(w, pad, (size_t)padding) != GS_OK)
        return GS_ERR_IO;
    return GS_OK;
}

static void free_sheet(struct gs_window *w, uint32_t sheet)
{
    unsigned char req[8];

    memset(req, 0, sizeof req);
    req[0] = X_FREE_PIXMAP;
    gs_win_put16(req + 2, 2);
    gs_win_put32(req + 4, sheet);
    gs_win_send_request(w, req, sizeof req);
}

/* Sends a small picture to a place on the frame. The ordinary image path
 * always covers the whole width from the left edge, so a letter needs its
 * own. */
static int put_small(struct gs_window *w, const unsigned int *pixels,
                     int wide, int tall, int x, int y)
{
    unsigned char header[24];
    unsigned char *band;
    size_t data_len = (size_t)wide * 4u;
    int swap = w->image_byte_order != 0;
    int row;
    int i;

    band = malloc(data_len);
    if (band == NULL)
        return GS_ERR_MEM;

    for (row = 0; row < tall; row++) {
        for (i = 0; i < wide; i++) {
            unsigned int c = pixels[(size_t)row * (size_t)wide + (size_t)i];
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
        gs_win_put32(header + 4, target_of(w));
        gs_win_put32(header + 8, w->gc_id);
        gs_win_put16(header + 12, (uint16_t)wide);
        gs_win_put16(header + 14, 1);
        gs_win_put16(header + 16, (uint16_t)(int16_t)x);
        gs_win_put16(header + 18, (uint16_t)(int16_t)(y + row));
        header[20] = 0;
        header[21] = w->root_depth;

        w->sequence++;
        if (gs_win_write_all(w, header, sizeof header) != GS_OK ||
            gs_win_write_all(w, band, data_len) != GS_OK) {
            free(band);
            return GS_ERR_IO;
        }
    }
    free(band);
    return GS_OK;
}

/* One shape at a time would be ninety five questions, so the whole set is
 * drawn in a row and read back in one go. */
int gs_win_read_glyphs(struct gs_window *w)
{
    char row[GS_WIN_GLYPH_COUNT + 1];
    unsigned char req[28];
    unsigned char reply[32];
    unsigned char *pixels = NULL;
    uint32_t scratch;
    size_t got = 0;
    size_t need;
    int width, height;
    int i, x, y;

    if (w == NULL || w->broken || w->font_width <= 0)
        return GS_ERR;
    if (w->glyph_ready)
        return GS_OK;

    height = w->font_ascent + w->font_descent;
    if (height <= 0 || height > GS_WIN_GLYPH_ROWS ||
        w->font_width > 32)
        return GS_ERR;
    width = GS_WIN_GLYPH_COUNT * w->font_width;

    for (i = 0; i < GS_WIN_GLYPH_COUNT; i++)
        row[i] = (char)(GS_WIN_GLYPH_FIRST + i);
    row[GS_WIN_GLYPH_COUNT] = '\0';

    /* A scratch sheet of the server's own, so nothing of the window is
     * disturbed while the shapes are drawn and read. */
    scratch = gs_win_next_id(w);
    memset(req, 0, 16);
    req[0] = X_CREATE_PIXMAP;
    req[1] = w->root_depth;
    gs_win_put16(req + 2, 4);
    gs_win_put32(req + 4, scratch);
    gs_win_put32(req + 8, w->window_id);
    gs_win_put16(req + 12, (uint16_t)width);
    gs_win_put16(req + 14, (uint16_t)height);
    w->sequence++;
    if (gs_win_write_all(w, req, 16) != GS_OK)
        return GS_ERR_IO;

    /* Black behind, white in front, so a set bit reads as ink. */
    if (fill_on(w, scratch, 0, 0, width, height, 0x00000000u) != GS_OK ||
        text_on(w, scratch, 0, w->font_ascent, row, 0x00ffffffu) != GS_OK) {
        free_sheet(w, scratch);
        return GS_ERR_IO;
    }

    need = (size_t)width * (size_t)height * 4u;
    pixels = malloc(need);
    if (pixels == NULL) {
        free_sheet(w, scratch);
        return GS_ERR_MEM;
    }

    memset(req, 0, 20);
    req[0] = X_GET_IMAGE;
    req[1] = 2;                             /* format 2 is ZPixmap */
    gs_win_put16(req + 2, 5);
    gs_win_put32(req + 4, scratch);
    gs_win_put16(req + 8, 0);
    gs_win_put16(req + 10, 0);
    gs_win_put16(req + 12, (uint16_t)width);
    gs_win_put16(req + 14, (uint16_t)height);
    gs_win_put32(req + 16, 0xffffffffu);
    w->sequence++;
    if (gs_win_write_all(w, req, 20) != GS_OK ||
        gs_win_await_reply_data(w, reply, pixels, need, &got) != GS_OK) {
        free(pixels);
        free_sheet(w, scratch);
        return GS_ERR;
    }
    free_sheet(w, scratch);

    if (got < need) {
        free(pixels);
        return GS_ERR;
    }

    /* A pixel that is not black is part of a shape. One bit each, with
     * the lowest bit the leftmost pixel of the row. */
    memset(w->glyph, 0, sizeof w->glyph);
    for (i = 0; i < GS_WIN_GLYPH_COUNT; i++)
        for (y = 0; y < height; y++) {
            uint32_t bits = 0;

            for (x = 0; x < w->font_width; x++) {
                size_t at = ((size_t)y * (size_t)width +
                             (size_t)(i * w->font_width + x)) * 4u;

                if ((gs_win_get32(pixels + at) & 0x00ffffffu) != 0)
                    bits |= 1u << x;
            }
            w->glyph[i][y] = bits;
        }

    free(pixels);
    w->glyph_ready = 1;
    gs_log_debug("window: read %d shapes, each %d by %d",
                 GS_WIN_GLYPH_COUNT, w->font_width, height);
    return GS_OK;
}

int gs_window_text_scaled_width(const gs_window_t *window, const char *text,
                                int scale)
{
    if (window == NULL || text == NULL || scale < 1)
        return 0;
    if (scale > 4)
        scale = 4;
    return (int)strlen(text) * window->font_width * scale;
}

int gs_window_text_scaled_height(const gs_window_t *window, int scale)
{
    if (window == NULL || scale < 1)
        return 0;
    if (scale > 4)
        scale = 4;
    return (window->font_ascent + window->font_descent) * scale;
}

int gs_window_text_scaled(gs_window_t *window, int x, int y,
                          const char *text, unsigned int colour,
                          unsigned int behind, int scale)
{
    unsigned int *image;
    size_t len;
    int height;
    int wide, tall;
    int rc;
    size_t i;
    int gx, gy, sx, sy;

    if (window == NULL || text == NULL || scale < 1)
        return GS_ERR_ARG;
    if (scale > 4)
        scale = 4;
    if (window->broken)
        return GS_ERR_IO;
    if (!window->glyph_ready && gs_win_read_glyphs(window) != GS_OK)
        return GS_ERR;

    len = strlen(text);
    if (len == 0)
        return GS_OK;
    if (len > 512)
        len = 512;

    height = window->font_ascent + window->font_descent;
    wide = (int)len * window->font_width * scale;
    tall = height * scale;
    if (wide <= 0 || tall <= 0)
        return GS_ERR_ARG;

    image = malloc((size_t)wide * (size_t)tall * sizeof *image);
    if (image == NULL)
        return GS_ERR_MEM;
    for (i = 0; i < (size_t)wide * (size_t)tall; i++)
        image[i] = behind;

    /* Every pixel of the small shape becomes a square of pixels, which is
     * what makes the writing larger. */
    for (i = 0; i < len; i++) {
        int code = (unsigned char)text[i] - GS_WIN_GLYPH_FIRST;

        if (code < 0 || code >= GS_WIN_GLYPH_COUNT)
            continue;
        for (gy = 0; gy < height; gy++) {
            uint32_t bits = window->glyph[code][gy];

            for (gx = 0; gx < window->font_width; gx++) {
                if ((bits & (1u << gx)) == 0)
                    continue;
                for (sy = 0; sy < scale; sy++)
                    for (sx = 0; sx < scale; sx++) {
                        int px = ((int)i * window->font_width + gx) * scale + sx;
                        int py = gy * scale + sy;

                        image[(size_t)py * (size_t)wide + (size_t)px] = colour;
                    }
            }
        }
    }

    /* The caller gives the baseline, the same as ordinary writing does. */
    rc = put_small(window, image, wide, tall, x,
                   y - window->font_ascent * scale);
    free(image);
    return rc;
}

int gs_window_face_text(gs_window_t *window, int x, int y, const char *text,
                        unsigned int colour, unsigned int behind)
{
    unsigned int *image;
    size_t len;
    int wide, tall;
    int rc;
    size_t i;
    int pen = 0;
    int gx, gy;

    if (window == NULL || text == NULL)
        return GS_ERR_ARG;
    if (window->broken)
        return GS_ERR_IO;

    len = strlen(text);
    if (len == 0)
        return GS_OK;
    if (len > 512)
        len = 512;

    wide = gs_window_face_width_n(text, len);
    tall = gs_win_face_height;
    if (wide <= 0 || tall <= 0)
        return GS_OK;

    image = malloc((size_t)wide * (size_t)tall * sizeof *image);
    if (image == NULL)
        return GS_ERR_MEM;
    for (i = 0; i < (size_t)wide * (size_t)tall; i++)
        image[i] = behind;

    /* Each letter is laid down where the pen has reached, and the pen
     * then moves by that letter's own width, which is what makes the
     * writing look like a real face rather than a grid. */
    for (i = 0; i < len; i++) {
        int code = (unsigned char)text[i] - GS_WIN_GLYPH_FIRST;

        if (code < 0 || code >= GS_WIN_GLYPH_COUNT)
            continue;
        for (gy = 0; gy < tall; gy++) {
            uint32_t bits = gs_win_face_bits[code][gy];

            for (gx = 0; gx < 32; gx++) {
                int px = pen + gx;

                if ((bits & (1u << gx)) == 0)
                    continue;
                if (px < 0 || px >= wide)
                    continue;
                image[(size_t)gy * (size_t)wide + (size_t)px] = colour;
            }
        }
        pen += gs_win_face_advance[code];
    }

    /* The caller gives the baseline, the same as ordinary writing does. */
    rc = put_small(window, image, wide, tall, x, y - gs_win_face_baseline);
    free(image);
    return rc;
}
