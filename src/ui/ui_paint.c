/* ui_paint.c
 *
 * Builds the canvas one pixel at a time into a buffer, which the window
 * module then sends across in a single image. Working in a buffer is what
 * allows a gradient that runs smoothly over hundreds of rows.
 *
 * Glyphs come from the server rather than from here, so the labels are
 * drawn afterwards by gs_ui_draw_labels.
 *
 * No floating point and no maths library.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "window.h"

#include <stdint.h>
#include <string.h>

static int chan_r(unsigned int c) { return (int)((c >> 16) & 0xffu); }
static int chan_g(unsigned int c) { return (int)((c >> 8) & 0xffu); }
static int chan_b(unsigned int c) { return (int)(c & 0xffu); }

static unsigned int rgb(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
}

static unsigned int blend(unsigned int a, unsigned int b, int alpha)
{
    if (alpha <= 0) return a;
    if (alpha >= 255) return b;
    return rgb(chan_r(a) + (chan_r(b) - chan_r(a)) * alpha / 255,
               chan_g(a) + (chan_g(b) - chan_g(a)) * alpha / 255,
               chan_b(a) + (chan_b(b) - chan_b(a)) * alpha / 255);
}

static unsigned int shade(unsigned int c, int num, int den)
{
    return rgb(chan_r(c) * num / den, chan_g(c) * num / den,
               chan_b(c) * num / den);
}

static void put(unsigned int *px, int w, int h, int x, int y,
                unsigned int colour, int alpha)
{
    size_t o;

    if (x < 0 || y < 0 || x >= w || y >= h || alpha <= 0)
        return;
    o = (size_t)y * (size_t)w + (size_t)x;
    px[o] = blend(px[o], colour, alpha);
}

void gs_ui_px_rect(unsigned int *px, int w, int h, gs_ui_rect_t r,
                   unsigned int colour)
{
    int x, y;

    for (y = r.y; y < r.y + r.h; y++)
        for (x = r.x; x < r.x + r.w; x++)
            put(px, w, h, x, y, colour, 255);
}

/* A rectangle with its corners taken off, drawn without a square root by
 * stepping the inset per row from a small table. */
static int corner_inset(int radius, int row)
{
    static const int table[8][8] = {
        {0},
        {0},
        {1, 0},
        {2, 1, 0},
        {2, 1, 0, 0},
        {3, 2, 1, 0, 0},
        {4, 2, 1, 1, 0, 0},
        {5, 3, 2, 1, 1, 0, 0}
    };

    if (radius < 2 || radius > 7 || row < 0 || row >= radius)
        return 0;
    return table[radius][row];
}

void gs_ui_px_rounded(unsigned int *px, int w, int h, gs_ui_rect_t r,
                      int radius, unsigned int face, unsigned int edge)
{
    int y;

    for (y = 0; y < r.h; y++) {
        int from_top = y;
        int from_bottom = r.h - 1 - y;
        int near = from_top < from_bottom ? from_top : from_bottom;
        int inset = corner_inset(radius, near);
        int x0 = r.x + inset;
        int x1 = r.x + r.w - 1 - inset;
        int x;

        for (x = x0; x <= x1; x++) {
            int border = (y == 0 || y == r.h - 1 || x == x0 || x == x1);
            put(px, w, h, x, r.y + y, border ? edge : face, 255);
        }
    }
}

gs_ui_rect_t gs_ui_mount_rect(int width)
{
    gs_ui_rect_t r;

    (void)width;
    r.x = GS_UI_MARGIN;
    r.y = (GS_UI_BAR_HEIGHT - GS_UI_BTN_HEIGHT) / 2;
    r.w = GS_UI_MOUNT_WIDTH;
    r.h = GS_UI_BTN_HEIGHT;
    return r;
}

gs_ui_rect_t gs_ui_specs_rect(int width)
{
    gs_ui_rect_t r = gs_ui_mount_rect(width);

    r.x += GS_UI_MOUNT_WIDTH + GS_UI_BTN_GAP;
    r.w = GS_UI_SPECS_WIDTH;
    return r;
}

static unsigned int button_face(const gs_ui_state_t *state, gs_ui_hit_t which,
                                int enabled)
{
    if (!enabled)
        return GS_UI_BTN_OFF_FACE;
    if (state->pressed == which)
        return GS_UI_BTN_FACE_DOWN;
    if (state->hover == which)
        return GS_UI_BTN_FACE_HOVER;
    return GS_UI_BTN_FACE;
}

static void draw_bar(unsigned int *px, int w, int h,
                     const gs_ui_state_t *state)
{
    gs_ui_rect_t bar;
    gs_ui_rect_t mount = gs_ui_mount_rect(w);
    gs_ui_rect_t specs = gs_ui_specs_rect(w);
    int led_x = w - GS_UI_MARGIN - 8;
    int led_y = GS_UI_BAR_HEIGHT / 2;
    int dx, dy;

    if (h < GS_UI_BAR_HEIGHT + 8 || w < GS_UI_MARGIN * 2 + 80)
        return;

    bar.x = 0;
    bar.y = 0;
    bar.w = w;
    bar.h = GS_UI_BAR_HEIGHT;
    gs_ui_px_rect(px, w, h, bar, GS_UI_BAR_FILL);

    /* One line under the bar, marking it off from the work area. */
    for (dx = 0; dx < w; dx++)
        put(px, w, h, dx, GS_UI_BAR_HEIGHT - 1, GS_UI_BAR_EDGE, 255);

    if (mount.x + mount.w < w - GS_UI_MARGIN)
        gs_ui_px_rounded(px, w, h, mount, GS_UI_BTN_RADIUS,
                button_face(state, GS_UI_HIT_MOUNT, 1), GS_UI_BTN_EDGE);

    if (specs.x + specs.w < w - GS_UI_MARGIN)
        gs_ui_px_rounded(px, w, h, specs, GS_UI_BTN_RADIUS,
                button_face(state, GS_UI_HIT_SPECS, state->mounted),
                state->mounted ? GS_UI_BTN_EDGE : GS_UI_BTN_OFF_EDGE);

    /* A round lamp on the right, green once a device is recorded. */
    for (dy = -5; dy <= 5; dy++) {
        for (dx = -5; dx <= 5; dx++) {
            int d2 = dx * dx + dy * dy;
            unsigned int colour = state->mounted ? GS_UI_LED_ON
                                                 : GS_UI_LED_OFF;
            if (d2 <= 16)
                put(px, w, h, led_x + dx, led_y + dy, colour, 255);
            else if (d2 <= 36)
                put(px, w, h, led_x + dx, led_y + dy, colour,
                    (36 - d2) * 255 / 20 / 4);
        }
    }
}

void gs_ui_compose_surface(unsigned int *pixels, int w, int h)
{
    int x, y;

    for (y = 0; y < h; y++) {
        unsigned int row = blend(GS_UI_SURFACE_TOP, GS_UI_SURFACE_BOTTOM,
                                 h > 1 ? y * 255 / (h - 1) : 0);
        int scan = (y % GS_UI_SCANLINE_STEP) == 0;
        int grid_row = (y > 0 && y % GS_UI_GRID_STEP == 0);
        int ly = h > 1 ? y * 128 / h : 0;

        if (scan)
            row = shade(row, 92, 100);

        for (x = 0; x < w; x++) {
            unsigned int c = row;

            {
                int lx = (2 * x - w) * 128 / (w > 0 ? w : 1);
                int lift = 128 - (lx * lx + ly * ly * 4) / 128;
                if (lift > 0)
                    c = blend(c, GS_UI_ACCENT, lift * 12 / 128);
            }

            if (grid_row && x > 0 && x % GS_UI_GRID_STEP == 0)
                c = blend(c, GS_UI_ACCENT, 20);

            pixels[(size_t)y * (size_t)w + (size_t)x] = c;
        }
    }

}

void gs_ui_compose(unsigned int *pixels, int w, int h,
                   const gs_ui_state_t *state)
{
    static const gs_ui_state_t blank;

    if (state == NULL)
        state = &blank;

    gs_ui_compose_surface(pixels, w, h);
    draw_bar(pixels, w, h, state);
    gs_ui_panes_compose(pixels, w, h, state);
    gs_ui_confirm_compose(pixels, w, h, state);
}
