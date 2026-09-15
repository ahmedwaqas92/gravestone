/* window_x11_state.c
 *
 * The shape the pointer takes over the window, and whether the window
 * fills the screen. Both are asked of the server rather than drawn, and
 * both were split out of window_x11.c to keep that file inside the size
 * this project holds a file to.
 */
#include "window_internal.h"
#include "gravestone.h"
#include "log.h"

#include <stdint.h>
#include <string.h>

/* Every X server ships a font named "cursor" holding the standard pointer
 * shapes, and a shape is picked out of it by number. These three are the
 * numbers for an arrow, a text bar and a pointing hand. Each shape takes
 * two glyphs, the picture and its mask, and the mask is always the next
 * glyph along. */
static const uint16_t cursor_glyph[GS_WINDOW_CURSOR_COUNT] = {
    68,     /* left_ptr, the ordinary arrow */
    152,    /* xterm, the bar shown where words are typed */
    60      /* hand2, the pointing hand */
};

static int open_cursor_font(gs_window_t *w)
{
    static const char name[] = "cursor";
    unsigned char req[12];
    unsigned char pad[3] = {0, 0, 0};
    size_t len = sizeof name - 1;
    int padding = gs_win_pad4((int)len);

    if (w->cursor_font != 0)
        return GS_OK;

    w->cursor_font = gs_win_next_id(w);
    memset(req, 0, sizeof req);
    req[0] = X_OPEN_FONT;
    gs_win_put16(req + 2, (uint16_t)(3 + (len + (size_t)padding) / 4));
    gs_win_put32(req + 4, w->cursor_font);
    gs_win_put16(req + 8, (uint16_t)len);

    w->sequence++;
    if (gs_win_write_all(w, req, sizeof req) != GS_OK ||
        gs_win_write_all(w, name, len) != GS_OK ||
        (padding > 0 && gs_win_write_all(w, pad, (size_t)padding) != GS_OK)) {
        w->cursor_font = 0;
        return GS_ERR_IO;
    }
    return GS_OK;
}

/* Makes one shape out of the font. The picture is black on white, which
 * is what every other program on a screen uses. */
static int make_cursor(gs_window_t *w, gs_window_cursor_t shape)
{
    unsigned char req[32];
    uint32_t id;

    if (w->cursor_id[shape] != 0)
        return GS_OK;
    if (open_cursor_font(w) != GS_OK)
        return GS_ERR_IO;

    id = gs_win_next_id(w);
    memset(req, 0, sizeof req);
    req[0] = X_CREATE_GLYPH_CURSOR;
    gs_win_put16(req + 2, 8);
    gs_win_put32(req + 4, id);
    gs_win_put32(req + 8, w->cursor_font);   /* the picture comes from here */
    gs_win_put32(req + 12, w->cursor_font);  /* and so does its mask */
    gs_win_put16(req + 16, cursor_glyph[shape]);
    gs_win_put16(req + 18, (uint16_t)(cursor_glyph[shape] + 1));
    gs_win_put16(req + 20, 0);               /* foreground red */
    gs_win_put16(req + 22, 0);               /* green */
    gs_win_put16(req + 24, 0);               /* blue, so black */
    gs_win_put16(req + 26, 0xffff);          /* background red */
    gs_win_put16(req + 28, 0xffff);          /* green */
    gs_win_put16(req + 30, 0xffff);          /* blue, so white */

    w->sequence++;
    if (gs_win_write_all(w, req, sizeof req) != GS_OK)
        return GS_ERR_IO;
    w->cursor_id[shape] = id;
    return GS_OK;
}

int gs_window_set_cursor(gs_window_t *window, gs_window_cursor_t shape)
{
    unsigned char req[16];

    if (window == NULL || shape < 0 || shape >= GS_WINDOW_CURSOR_COUNT)
        return GS_ERR_ARG;
    if (window->broken)
        return GS_ERR_IO;
    /* Asking for the shape already showing costs nothing, so a caller may
     * do this on every movement of the pointer. */
    if (window->cursor_now == (int)shape)
        return GS_OK;
    if (make_cursor(window, shape) != GS_OK)
        return GS_ERR_IO;

    memset(req, 0, sizeof req);
    req[0] = X_CHANGE_WINDOW_ATTR;
    gs_win_put16(req + 2, 4);
    gs_win_put32(req + 4, window->window_id);
    gs_win_put32(req + 8, 0x00004000u);      /* the cursor bit of the mask */
    gs_win_put32(req + 12, window->cursor_id[shape]);

    window->sequence++;
    if (gs_win_write_all(window, req, sizeof req) != GS_OK)
        return GS_ERR_IO;
    window->cursor_now = (int)shape;
    return GS_OK;
}

int gs_window_maximise(gs_window_t *window, int on)
{
    unsigned char req[24];
    unsigned char value[8];

    if (window == NULL)
        return GS_ERR_ARG;
    if (window->broken || window->atom_net_wm_state == 0 ||
        window->atom_net_wm_max_vert == 0 || window->atom_net_wm_max_horz == 0)
        return GS_ERR_IO;

    /* The window carries a list of the states it is in, and a manager
     * reads that list. Writing both grown states into it asks for the
     * whole working area, which leaves the frame and its buttons alone.
     * An empty list asks for the ordinary size back. */
    memset(req, 0, sizeof req);
    req[0] = X_CHANGE_PROPERTY;
    req[1] = 0;                              /* replace what is there */
    gs_win_put16(req + 2, (uint16_t)(6 + (on ? 2 : 0)));
    gs_win_put32(req + 4, window->window_id);
    gs_win_put32(req + 8, window->atom_net_wm_state);
    gs_win_put32(req + 12, 4);               /* the type is itself an atom */
    req[16] = 32;                            /* thirty two bits each */
    gs_win_put32(req + 20, on ? 2u : 0u);    /* how many are in the list */

    window->sequence++;
    if (gs_win_write_all(window, req, sizeof req) != GS_OK)
        return GS_ERR_IO;
    if (on) {
        gs_win_put32(value, window->atom_net_wm_max_vert);
        gs_win_put32(value + 4, window->atom_net_wm_max_horz);
        if (gs_win_write_all(window, value, sizeof value) != GS_OK)
            return GS_ERR_IO;
    }
    return GS_OK;
}
