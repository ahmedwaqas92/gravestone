/* window_x11.c
 *
 * Opening and closing a window, and the requests that set one up. The
 * socket work sits in window_x11_conn.c and the painting in
 * window_x11_draw.c.
 */
#include "window.h"
#include "window_internal.h"
#include "gravestone.h"
#include "log.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Every window, graphics context and pixmap needs an identifier the server
 * agrees to. The server hands out a base value and a mask saying which bits
 * the client may vary. */
uint32_t gs_win_next_id(gs_window_t *w)
{
    uint32_t id = w->id_base | (w->id_next & w->id_mask);
    w->id_next++;
    return id;
}

static int intern_atom(gs_window_t *w, const char *name, uint32_t *out)
{
    unsigned char header[8];
    unsigned char reply[32];
    unsigned char pad[3] = {0, 0, 0};
    size_t name_len = strlen(name);
    int padding = gs_win_pad4((int)name_len);
    uint16_t words = (uint16_t)(2 + (name_len + (size_t)padding) / 4);

    header[0] = X_INTERN_ATOM;
    header[1] = 0;                       /* create it when absent */
    gs_win_put16(header + 2, words);
    gs_win_put16(header + 4, (uint16_t)name_len);
    gs_win_put16(header + 6, 0);

    w->sequence++;
    if (gs_win_write_all(w, header, sizeof header) != GS_OK) return GS_ERR_IO;
    if (gs_win_write_all(w, name, name_len) != GS_OK) return GS_ERR_IO;
    if (padding > 0 && gs_win_write_all(w, pad, (size_t)padding) != GS_OK)
        return GS_ERR_IO;

    if (gs_win_await_reply(w, reply) != GS_OK)
        return GS_ERR;

    *out = gs_win_get32(reply + 8);
    gs_log_debug("window: atom %s = %u", name, *out);
    return GS_OK;
}

static int create_window(gs_window_t *w)
{
    unsigned char req[36];
    unsigned char tail[8];
    uint32_t mask;

    w->window_id = gs_win_next_id(w);

    /* Values follow the order of the bits in the mask. Background pixel is
     * bit 1, border pixel bit 3, event mask bit 11. */
    mask = 0x00000002u | 0x00000008u | 0x00000800u;

    memset(req, 0, sizeof req);
    req[0] = X_CREATE_WINDOW;
    req[1] = w->root_depth;
    gs_win_put16(req + 2, 11);              /* eight fixed words plus three */
    gs_win_put32(req + 4, w->window_id);
    gs_win_put32(req + 8, w->root);
    gs_win_put16(req + 12, 0);              /* x, the manager places it */
    gs_win_put16(req + 14, 0);              /* y */
    gs_win_put16(req + 16, (uint16_t)w->width);
    gs_win_put16(req + 18, (uint16_t)w->height);
    gs_win_put16(req + 20, 0);              /* border width */
    gs_win_put16(req + 22, 1);              /* class, 1 is InputOutput */
    gs_win_put32(req + 24, w->root_visual);
    gs_win_put32(req + 28, mask);
    gs_win_put32(req + 32, 0x00000000u);    /* background pixel, black */

    gs_win_put32(tail, 0x00000000u);        /* border pixel */
    /* KeyPress 0x1, ButtonPress 0x4, ButtonRelease 0x8,
     * PointerMotion 0x40, Exposure 0x8000, StructureNotify 0x20000.
     *
     * Release is asked for so a drag can end. A grip taken hold of and
     * never let go would follow the pointer for ever. */
    gs_win_put32(tail + 4, 0x00000001u | 0x00000004u | 0x00000008u |
                           0x00000040u | 0x00008000u | 0x00020000u);

    w->sequence++;
    if (gs_win_write_all(w, req, sizeof req) != GS_OK) return GS_ERR_IO;
    if (gs_win_write_all(w, tail, sizeof tail) != GS_OK) return GS_ERR_IO;
    return GS_OK;
}

static int set_title(gs_window_t *w, const char *title)
{
    unsigned char req[24];
    unsigned char pad[3] = {0, 0, 0};
    size_t len = strlen(title);
    int padding = gs_win_pad4((int)len);

    memset(req, 0, sizeof req);
    req[0] = X_CHANGE_PROPERTY;
    req[1] = 0;                          /* replace */
    gs_win_put16(req + 2, (uint16_t)(6 + (len + (size_t)padding) / 4));
    gs_win_put32(req + 4, w->window_id);
    gs_win_put32(req + 8, X_ATOM_WM_NAME);
    gs_win_put32(req + 12, X_ATOM_STRING);
    req[16] = 8;                         /* eight bits per item */
    gs_win_put32(req + 20, (uint32_t)len);

    w->sequence++;
    if (gs_win_write_all(w, req, sizeof req) != GS_OK) return GS_ERR_IO;
    if (gs_win_write_all(w, title, len) != GS_OK) return GS_ERR_IO;
    if (padding > 0 && gs_win_write_all(w, pad, (size_t)padding) != GS_OK)
        return GS_ERR_IO;
    return GS_OK;
}

/* WM_CLASS names the program to whoever is managing the window. It holds
 * two strings one after the other, each ending in a zero byte, the first
 * being the instance and the second the class.
 *
 * WSLg refuses to mirror a window onto the Windows desktop without it. Its
 * compositor asks each window for an application identifier, and a window
 * with no WM_CLASS has none to give, which shows in /mnt/wslg/weston.log
 * as "does not have appId, or not top level window". The window still
 * exists on the X server and still counts as mapped, and nothing appears
 * on screen beyond an icon on the taskbar.
 */
static int set_class(gs_window_t *w, const char *instance, const char *class)
{
    unsigned char req[24];
    unsigned char pad[3] = {0, 0, 0};
    size_t inst_len = strlen(instance) + 1;   /* the zero byte counts */
    size_t class_len = strlen(class) + 1;
    size_t len = inst_len + class_len;
    int padding = gs_win_pad4((int)len);

    memset(req, 0, sizeof req);
    req[0] = X_CHANGE_PROPERTY;
    req[1] = 0;                          /* replace */
    gs_win_put16(req + 2, (uint16_t)(6 + (len + (size_t)padding) / 4));
    gs_win_put32(req + 4, w->window_id);
    gs_win_put32(req + 8, X_ATOM_WM_CLASS);
    gs_win_put32(req + 12, X_ATOM_STRING);
    req[16] = 8;                         /* eight bits per item */
    gs_win_put32(req + 20, (uint32_t)len);

    w->sequence++;
    if (gs_win_write_all(w, req, sizeof req) != GS_OK) return GS_ERR_IO;
    if (gs_win_write_all(w, instance, inst_len) != GS_OK) return GS_ERR_IO;
    if (gs_win_write_all(w, class, class_len) != GS_OK) return GS_ERR_IO;
    if (padding > 0 && gs_win_write_all(w, pad, (size_t)padding) != GS_OK)
        return GS_ERR_IO;
    return GS_OK;
}

/* WM_HINTS tells the manager the window takes keyboard input and starts
 * in its normal state. A window without it can be treated as one that
 * never wants focus, and a window that never takes focus is never brought
 * to the front when it opens. */
static int set_wm_hints(gs_window_t *w)
{
    unsigned char req[24 + 36];

    memset(req, 0, sizeof req);
    req[0] = X_CHANGE_PROPERTY;
    req[1] = 0;                          /* replace */
    gs_win_put16(req + 2, 6 + 9);        /* header plus nine words */
    gs_win_put32(req + 4, w->window_id);
    gs_win_put32(req + 8, X_ATOM_WM_HINTS);
    gs_win_put32(req + 12, X_ATOM_WM_HINTS);
    req[16] = 32;                        /* thirty two bits per item */
    gs_win_put32(req + 20, 9);           /* nine items follow */
    gs_win_put32(req + 24, 0x1u | 0x2u); /* input and state are given */
    gs_win_put32(req + 28, 1);           /* takes keyboard input */
    gs_win_put32(req + 32, 1);           /* 1 is NormalState */

    return gs_win_send_request(w, req, sizeof req);
}

/* Marks the window as an ordinary application window. Compositors pick
 * decorations and taskbar treatment off this, and WSLg reads it when it
 * builds the matching window on the Windows desktop. */
static int set_window_type(gs_window_t *w)
{
    unsigned char req[28];
    uint32_t type_atom, normal_atom;

    if (intern_atom(w, "_NET_WM_WINDOW_TYPE", &type_atom) != GS_OK)
        return GS_ERR;
    if (intern_atom(w, "_NET_WM_WINDOW_TYPE_NORMAL", &normal_atom) != GS_OK)
        return GS_ERR;

    memset(req, 0, sizeof req);
    req[0] = X_CHANGE_PROPERTY;
    req[1] = 0;
    gs_win_put16(req + 2, 7);
    gs_win_put32(req + 4, w->window_id);
    gs_win_put32(req + 8, type_atom);
    gs_win_put32(req + 12, X_ATOM_ATOM);
    req[16] = 32;
    gs_win_put32(req + 20, 1);
    gs_win_put32(req + 24, normal_atom);

    return gs_win_send_request(w, req, sizeof req);
}

/* Without this the close button kills the connection outright instead of
 * telling the program about it. */
static int set_close_protocol(gs_window_t *w)
{
    unsigned char req[28];

    if (intern_atom(w, "WM_PROTOCOLS", &w->atom_wm_protocols) != GS_OK)
        return GS_ERR;
    /* A manager that does not know these leaves them at nothing, and the
     * request to grow then reports that it could not be made. */
    if (intern_atom(w, "_NET_WM_STATE", &w->atom_net_wm_state) != GS_OK)
        w->atom_net_wm_state = 0;
    if (intern_atom(w, "_NET_WM_STATE_MAXIMIZED_VERT",
                    &w->atom_net_wm_max_vert) != GS_OK)
        w->atom_net_wm_max_vert = 0;
    if (intern_atom(w, "_NET_WM_STATE_MAXIMIZED_HORZ",
                    &w->atom_net_wm_max_horz) != GS_OK)
        w->atom_net_wm_max_horz = 0;
    if (intern_atom(w, "WM_DELETE_WINDOW", &w->atom_wm_delete) != GS_OK)
        return GS_ERR;

    memset(req, 0, sizeof req);
    req[0] = X_CHANGE_PROPERTY;
    req[1] = 0;
    gs_win_put16(req + 2, 7);
    gs_win_put32(req + 4, w->window_id);
    gs_win_put32(req + 8, w->atom_wm_protocols);
    gs_win_put32(req + 12, X_ATOM_ATOM);
    req[16] = 32;                        /* thirty two bits per item */
    gs_win_put32(req + 20, 1);              /* one atom follows */
    gs_win_put32(req + 24, w->atom_wm_delete);

    return gs_win_send_request(w, req, sizeof req);
}

static int create_gc(gs_window_t *w)
{
    unsigned char req[24];

    w->gc_id = gs_win_next_id(w);

    memset(req, 0, sizeof req);
    req[0] = X_CREATE_GC;
    gs_win_put16(req + 2, 6);
    gs_win_put32(req + 4, w->gc_id);
    gs_win_put32(req + 8, w->window_id);
    /* foreground is bit 2, font is bit 14, and values follow bit order */
    gs_win_put32(req + 12, 0x00000004u | 0x00004000u);
    gs_win_put32(req + 16, 0x00000000u);
    gs_win_put32(req + 20, w->font_id);

    w->gc_foreground = 0x00000000u;
    return gs_win_send_request(w, req, sizeof req);
}

/* The server carries a handful of fixed width bitmap fonts. Loading one
 * avoids shipping glyph data of our own. */
static int load_font(gs_window_t *w)
{
    static const char *candidates[] = { "6x13", "fixed",
        "-misc-fixed-medium-r-semicondensed--13-100-100-100-c-60-iso8859-1" };
    unsigned char req[12];
    unsigned char pad[3] = {0, 0, 0};
    unsigned char reply[32];
    unsigned char extra[256];
    size_t got = 0;
    size_t i;

    for (i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        const char *name = candidates[i];
        size_t len = strlen(name);
        int padding = gs_win_pad4((int)len);

        w->font_id = gs_win_next_id(w);
        memset(req, 0, sizeof req);
        req[0] = X_OPEN_FONT;
        gs_win_put16(req + 2, (uint16_t)(3 + (len + (size_t)padding) / 4));
        gs_win_put32(req + 4, w->font_id);
        gs_win_put16(req + 8, (uint16_t)len);

        w->sequence++;
        if (gs_win_write_all(w, req, sizeof req) != GS_OK) return GS_ERR_IO;
        if (gs_win_write_all(w, name, len) != GS_OK) return GS_ERR_IO;
        if (padding > 0 &&
            gs_win_write_all(w, pad, (size_t)padding) != GS_OK)
            return GS_ERR_IO;

        /* QueryFont answers with the metrics and tells us whether the
         * font opened at all, since OpenFont itself says nothing. */
        memset(req, 0, 8);
        req[0] = X_QUERY_FONT;
        gs_win_put16(req + 2, 2);
        gs_win_put32(req + 4, w->font_id);
        w->sequence++;
        if (gs_win_write_all(w, req, 8) != GS_OK) return GS_ERR_IO;

        if (gs_win_await_reply_data(w, reply, extra, sizeof extra,
                                    &got) != GS_OK) {
            gs_log_debug("window: font %s did not open", name);
            continue;
        }

        /* Character width sits inside max-bounds, which starts at byte 24
         * of the reply, and the width field is four bytes into it. */
        w->font_width = (int)(int16_t)gs_win_get16(reply + 28);
        if (got >= 24) {
            w->font_ascent  = (int)(int16_t)gs_win_get16(extra + 20);
            w->font_descent = (int)(int16_t)gs_win_get16(extra + 22);
        }
        if (w->font_width <= 0)  w->font_width = 6;
        if (w->font_ascent <= 0) w->font_ascent = 11;
        if (w->font_descent < 0) w->font_descent = 2;

        gs_log_debug("window: font %s is %d wide, %d up, %d down", name,
                     w->font_width, w->font_ascent, w->font_descent);
        return GS_OK;
    }

    gs_log_error("window: the server offered no usable font");
    return GS_ERR;
}

/* Puts the window at the front of the stack. A window opened underneath
 * whatever the user was already looking at reads as a window that never
 * opened, which is what happens under WSLg with a maximised editor in the
 * way. */
static int raise_window(gs_window_t *w)
{
    unsigned char req[16];

    memset(req, 0, sizeof req);
    req[0] = X_CONFIGURE_WINDOW;
    gs_win_put16(req + 2, 4);            /* three fixed words plus one value */
    gs_win_put32(req + 4, w->window_id);
    gs_win_put16(req + 8, 0x0040u);      /* stack mode */
    gs_win_put16(req + 10, 0);
    gs_win_put32(req + 12, 0);           /* 0 is Above */

    return gs_win_send_request(w, req, sizeof req);
}

/* Asks the server what each key means. Keycodes are seat numbers with no
 * meaning of their own, and the keyboard map is the table that turns seat
 * 38 into the letter a. Each seat carries several meanings in a row, and
 * the first two are the key alone and the key with shift held, so both
 * are kept. Keeping only the first is what makes shift and the oblique
 * give an oblique rather than a question mark. */
static int fetch_keymap(gs_window_t *w)
{
    unsigned char req[8];
    unsigned char reply[32];
    unsigned char data[8192];
    size_t len = 0;
    unsigned per, count;

    if (w->max_keycode <= w->min_keycode)
        return GS_OK;
    count = (unsigned)w->max_keycode - w->min_keycode + 1u;

    memset(req, 0, sizeof req);
    req[0] = X_GET_KEYBOARD_MAP;
    gs_win_put16(req + 2, 2);
    req[4] = w->min_keycode;
    req[5] = (unsigned char)count;

    w->sequence++;
    if (gs_win_write_all(w, req, sizeof req) != GS_OK)
        return GS_ERR_IO;
    if (gs_win_await_reply_data(w, reply, data, sizeof data, &len) != GS_OK)
        return GS_ERR;

    per = reply[1];
    if (per == 0)
        return GS_OK;
    gs_win_read_keymap(data, len, per, count, w->min_keycode,
                       w->keysym0, w->keysym1);
    return GS_OK;
}

void gs_win_read_keymap(const unsigned char *data, size_t len, unsigned per,
                        unsigned count, unsigned first,
                        uint32_t *plain, uint32_t *upper)
{
    unsigned i;

    if (data == NULL || plain == NULL || upper == NULL || per == 0)
        return;

    for (i = 0; i < count && first + i < 256 &&
                (size_t)(i * per + 1) * 4u <= len; i++) {
        const unsigned char *row = data + (size_t)i * per * 4u;

        plain[first + i] = gs_win_get32(row);
        /* A seat with only one meaning shifts to the same thing. */
        if (per >= 2 && (size_t)(i * per + 2) * 4u <= len)
            upper[first + i] = gs_win_get32(row + 4);
        else
            upper[first + i] = plain[first + i];
    }
}

static int map_window(gs_window_t *w)
{
    unsigned char req[8];

    memset(req, 0, sizeof req);
    req[0] = X_MAP_WINDOW;
    gs_win_put16(req + 2, 2);
    gs_win_put32(req + 4, w->window_id);
    if (gs_win_send_request(w, req, sizeof req) != GS_OK)
        return GS_ERR_IO;

    /* Raising happens after the map, since the stack position of a window
     * nobody has shown yet means nothing. */
    return raise_window(w);
}

/* The size hints are eighteen numbers in a fixed order, laid down by the
 * conventions manual. Only the flags, the position, the size, the smallest
 * useful size and the base size are filled in. */
int gs_window_set_hints(gs_window_t *window, int x, int y,
                        int want_w, int want_h, int min_w, int min_h)
{
    unsigned char req[24];
    unsigned char hints[18 * 4];
    uint32_t flags;

    if (window == NULL || want_w <= 0 || want_h <= 0)
        return GS_ERR_ARG;

    /* PSize is bit 3, PMinSize bit 4, PBaseSize bit 8, and PPosition bit 2
     * alongside USPosition bit 0, which together tell a manager the place
     * was chosen deliberately. */
    flags = 8u | 16u | 256u;
    if (x >= 0 && y >= 0)
        flags |= 1u | 4u;

    memset(hints, 0, sizeof hints);
    gs_win_put32(hints + 0 * 4, flags);
    gs_win_put32(hints + 1 * 4, (uint32_t)(x >= 0 ? x : 0));
    gs_win_put32(hints + 2 * 4, (uint32_t)(y >= 0 ? y : 0));
    gs_win_put32(hints + 3 * 4, (uint32_t)want_w);
    gs_win_put32(hints + 4 * 4, (uint32_t)want_h);
    gs_win_put32(hints + 5 * 4, (uint32_t)(min_w > 0 ? min_w : 1));
    gs_win_put32(hints + 6 * 4, (uint32_t)(min_h > 0 ? min_h : 1));
    gs_win_put32(hints + 15 * 4, (uint32_t)want_w);
    gs_win_put32(hints + 16 * 4, (uint32_t)want_h);

    memset(req, 0, sizeof req);
    req[0] = X_CHANGE_PROPERTY;
    req[1] = 0;                          /* replace */
    gs_win_put16(req + 2, (uint16_t)(6 + sizeof hints / 4));
    gs_win_put32(req + 4, window->window_id);
    gs_win_put32(req + 8, X_ATOM_WM_NORMAL_HINTS);
    gs_win_put32(req + 12, X_ATOM_WM_SIZE_HINTS);
    req[16] = 32;                        /* thirty two bits per item */
    gs_win_put32(req + 20, 18);          /* eighteen of them */

    window->sequence++;
    if (gs_win_write_all(window, req, sizeof req) != GS_OK)
        return GS_ERR_IO;
    return gs_win_write_all(window, hints, sizeof hints);
}

/* The window manager wraps a frame around each window, so a window knows
 * only where it sits inside that frame. Asking the server to translate its
 * own corner into root coordinates gives the answer on screen. */
int gs_window_position(gs_window_t *window, int *x, int *y)
{
    unsigned char req[16];
    unsigned char reply[32];

    if (window == NULL || x == NULL || y == NULL)
        return GS_ERR_ARG;
    if (window->broken)
        return GS_ERR_IO;

    memset(req, 0, sizeof req);
    req[0] = X_TRANSLATE_COORDS;
    gs_win_put16(req + 2, 4);
    gs_win_put32(req + 4, window->window_id);
    gs_win_put32(req + 8, window->root);
    gs_win_put16(req + 12, 0);
    gs_win_put16(req + 14, 0);

    window->sequence++;
    if (gs_win_write_all(window, req, sizeof req) != GS_OK)
        return GS_ERR_IO;
    if (gs_win_await_reply(window, reply) != GS_OK)
        return GS_ERR;

    *x = (int)(int16_t)gs_win_get16(reply + 12);
    *y = (int)(int16_t)gs_win_get16(reply + 14);
    return GS_OK;
}

int gs_window_display_available(void)
{
    int display;
    int fd;

    /* A window opening takes the screen from whoever is working on it.
     * Setting GS_TEST_NO_WINDOW says no screen is available, which every
     * test already knows how to answer, so a whole run goes past without
     * anything appearing. Checked here rather than in each test, since a
     * test added later would otherwise have to remember. */
    if (getenv("GS_TEST_NO_WINDOW") != NULL)
        return 0;

    display = gs_win_display_number();
    if (display < 0)
        return 0;
    fd = gs_win_connect(display);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

/* Nothing opens while windows are turned off, so a test that forgets to
 * ask cannot put one on the screen either. */
static int windows_are_off(void)
{
    return getenv("GS_TEST_NO_WINDOW") != NULL;
}

gs_window_t *gs_window_open(const char *title, int width, int height)
{
    gs_window_t *w;
    int display;

    if (title == NULL || width <= 0 || height <= 0) {
        gs_log_error("window: bad arguments to gs_window_open");
        return NULL;
    }
    if (width > 32767 || height > 32767) {
        gs_log_error("window: %dx%d is past the 32767 the protocol allows",
                     width, height);
        return NULL;
    }

    if (windows_are_off()) {
        gs_log_debug("window: windows are turned off, opening nothing");
        return NULL;
    }

    display = gs_win_display_number();
    if (display < 0)
        return NULL;

    w = calloc(1, sizeof *w);
    if (w == NULL)
        return NULL;

    w->width = width;
    w->height = height;
    w->id_next = 1;
    /* Zero is the arrow, so a fresh window would skip the first request
     * for it. Starting outside the range makes the first ask always go
     * out, whichever shape it is for. */
    w->cursor_now = -1;

    w->fd = gs_win_connect(display);
    if (w->fd < 0) {
        free(w);
        return NULL;
    }

    if (gs_win_handshake(w) != GS_OK)      goto fail;
    if (create_window(w) != GS_OK)      goto fail;
    if (set_title(w, title) != GS_OK)   goto fail;
    if (set_class(w, GS_WINDOW_INSTANCE, GS_WINDOW_CLASS) != GS_OK)
        goto fail;
    if (set_wm_hints(w) != GS_OK)       goto fail;
    if (set_window_type(w) != GS_OK)    goto fail;
    if (set_close_protocol(w) != GS_OK) goto fail;
    /* Asked for before the window is shown, since a manager reads the
     * hints when it maps the window and not afterwards. */
    if (gs_window_set_hints(w, -1, -1, width, height, 240, 120) != GS_OK)
        goto fail;
    if (load_font(w) != GS_OK)          goto fail;
    if (create_gc(w) != GS_OK)          goto fail;
    if (fetch_keymap(w) != GS_OK)       goto fail;
    if (map_window(w) != GS_OK)         goto fail;

    gs_log_info("window: opened %dx%d as \"%s\"", width, height, title);
    return w;

fail:
    close(w->fd);
    free(w);
    return NULL;
}

void gs_window_close(gs_window_t *window)
{
    unsigned char req[8];

    if (window == NULL)
        return;

    /* A window destroyed before the compositor finished putting it on
     * screen leaves WSLg holding a mirror of it for ever, since the
     * destroy notice lands on a record still under construction. Waiting
     * for the first exposure means the compositor has acknowledged the
     * window, so its destruction lands on something whole. Half a second
     * bounds the wait for compositors that never answer. */
    if (window->fd >= 0 && !window->broken && !window->exposed) {
        gs_window_event_t drain;
        int rounds;

        for (rounds = 0; rounds < 10 && !window->exposed; rounds++)
            gs_window_wait_event(window, &drain, 50);
        if (!window->exposed)
            gs_log_debug("window: closing without ever being shown");
    }

    /* Dropping the connection alone leaves a window manager holding a
     * frame it was never told to let go of, so the window is taken off
     * the screen and destroyed before the socket closes. */
    if (window->fd >= 0 && !window->broken) {
        /* The frame pixmap is server memory, so it goes first. */
        if (window->pixmap_id != 0) {
            memset(req, 0, sizeof req);
            req[0] = X_FREE_PIXMAP;
            gs_win_put16(req + 2, 2);
            gs_win_put32(req + 4, window->pixmap_id);
            gs_win_send_request(window, req, sizeof req);
            window->pixmap_id = 0;
        }

        memset(req, 0, sizeof req);
        req[0] = X_UNMAP_WINDOW;
        gs_win_put16(req + 2, 2);
        gs_win_put32(req + 4, window->window_id);
        gs_win_send_request(window, req, sizeof req);

        memset(req, 0, sizeof req);
        req[0] = X_DESTROY_WINDOW;
        gs_win_put16(req + 2, 2);
        gs_win_put32(req + 4, window->window_id);
        gs_win_send_request(window, req, sizeof req);
    }

    /* Asking a question the server must answer forces it to work through
     * everything queued first, so the window is gone before the socket
     * closes rather than at some point after. */
    if (window->fd >= 0 && !window->broken) {
        unsigned char reply[32];

        memset(req, 0, sizeof req);
        req[0] = X_GET_INPUT_FOCUS;
        gs_win_put16(req + 2, 1);
        window->sequence++;
        if (gs_win_write_all(window, req, 4) == GS_OK)
            gs_win_await_reply(window, reply);
    }

    if (window->fd >= 0)
        close(window->fd);
    free(window);
}

int gs_window_width(const gs_window_t *window)
{
    return window != NULL ? window->width : 0;
}

int gs_window_height(const gs_window_t *window)
{
    return window != NULL ? window->height : 0;
}

int gs_window_connected(const gs_window_t *window)
{
    return window != NULL && !window->broken && window->fd >= 0;
}

int gs_window_error_count(const gs_window_t *window)
{
    return window != NULL ? window->errors : 0;
}

int gs_window_font_height(const gs_window_t *window)
{
    return window != NULL ? window->font_ascent + window->font_descent : 0;
}

int gs_window_font_ascent(const gs_window_t *window)
{
    return window != NULL ? window->font_ascent : 0;
}

int gs_window_text_width(const gs_window_t *window, const char *text)
{
    if (window == NULL || text == NULL)
        return 0;
    return (int)strlen(text) * window->font_width;
}

int gs_window_resize(gs_window_t *window, int w, int h)
{
    unsigned char req[20];

    if (window == NULL || w <= 0 || h <= 0)
        return GS_ERR_ARG;
    if (w > 32767 || h > 32767)
        return GS_ERR_ARG;

    /* Every entry in the value list is four bytes wide, including width
     * and height, which carry only sixteen bits of meaning. The fixed part
     * is three words and each value adds one. */
    memset(req, 0, sizeof req);
    req[0] = X_CONFIGURE_WINDOW;
    gs_win_put16(req + 2, 5);
    gs_win_put32(req + 4, window->window_id);
    gs_win_put16(req + 8, 0x0004u | 0x0008u);   /* width and height */
    gs_win_put16(req + 10, 0);                  /* unused */
    gs_win_put32(req + 12, (uint32_t)w);
    gs_win_put32(req + 16, (uint32_t)h);

    return gs_win_send_request(window, req, sizeof req);
}

int gs_window_place(gs_window_t *window, int x, int y, int w, int h)
{
    unsigned char req[28];

    if (window == NULL || w <= 0 || h <= 0)
        return GS_ERR_ARG;
    if (w > 32767 || h > 32767)
        return GS_ERR_ARG;
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767)
        return GS_ERR_ARG;

    /* Four values in the order of their mask bits, x, y, width, height. */
    memset(req, 0, sizeof req);
    req[0] = X_CONFIGURE_WINDOW;
    gs_win_put16(req + 2, 7);
    gs_win_put32(req + 4, window->window_id);
    gs_win_put16(req + 8, 0x0001u | 0x0002u | 0x0004u | 0x0008u);
    gs_win_put16(req + 10, 0);
    gs_win_put32(req + 12, (uint32_t)(int32_t)x);
    gs_win_put32(req + 16, (uint32_t)(int32_t)y);
    gs_win_put32(req + 20, (uint32_t)w);
    gs_win_put32(req + 24, (uint32_t)h);

    return gs_win_send_request(window, req, sizeof req);
}
