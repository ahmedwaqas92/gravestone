/* window_internal.h
 *
 * Shared between the .c files inside src/lib/window/ only. Nothing outside
 * this directory may include it. Symbols here are not part of the public
 * interface, so they carry a gs_win_ prefix rather than gs_.
 */
#ifndef GS_LIB_WINDOW_INTERNAL_H
#define GS_LIB_WINDOW_INTERNAL_H

#include "window.h"

/* ---- the X11 half ---- */
/* The printable characters, from space to the tilde, and the tallest
 * shape the one font on offer draws. */
#include <stdint.h>

#define GS_WIN_GLYPH_FIRST  32
#define GS_WIN_GLYPH_COUNT  95
#define GS_WIN_GLYPH_ROWS   24
#define GS_WIN_FACE_ROWS    20      /* rows one Calibri shape stands in */

/* The letters the chat is written in, taken once from Calibri and written
 * into window_face.c, so no screen is asked for a font it lacks. */
extern const int gs_win_face_height;
extern const int gs_win_face_baseline;
extern const unsigned char gs_win_face_advance[GS_WIN_GLYPH_COUNT];
extern const uint32_t gs_win_face_bits[GS_WIN_GLYPH_COUNT][GS_WIN_FACE_ROWS];

#ifndef _WIN32

#include <stddef.h>
#include <stdint.h>

#include "window.h"

/* Request opcodes, from the X11 protocol specification. */
#define X_CREATE_WINDOW        1
#define X_DESTROY_WINDOW       4
#define X_MAP_WINDOW           8
#define X_UNMAP_WINDOW        10
#define X_INTERN_ATOM         16
#define X_GET_KEYBOARD_MAP   101
#define X_CHANGE_PROPERTY     18
#define X_CREATE_PIXMAP       53
#define X_FREE_PIXMAP         54
#define X_COPY_AREA           62
#define X_OPEN_FONT           45
#define X_QUERY_FONT          47
#define X_CONFIGURE_WINDOW    12
#define X_CREATE_GC           55
#define X_CHANGE_GC           56
#define X_POLY_FILL_RECTANGLE 70
#define X_POLY_TEXT8          74
#define X_PUT_IMAGE           72
#define X_TRANSLATE_COORDS    40
#define X_GET_INPUT_FOCUS     43
#define X_GET_IMAGE           73
#define X_CHANGE_WINDOW_ATTR   2
#define X_CREATE_GLYPH_CURSOR 94
#define X_FREE_CURSOR         95

/* The first byte of every packet the server sends back. */
#define X_REPLY_ERROR 0
#define X_REPLY_OK    1
#define X_REPLY_EVENT 2

/* Event codes. */
#define X_EVT_KEY_PRESS       2
#define X_EVT_BUTTON_PRESS    4
#define X_EVT_BUTTON_RELEASE  5
#define X_EVT_BUTTON_RELEASE  5
#define X_EVT_MOTION          6
#define X_EVT_EXPOSE         12
#define X_EVT_CONFIGURE      22
#define X_EVT_CLIENT_MESSAGE 33

/* Predefined atoms, fixed by the protocol so no lookup is needed. */
#define X_ATOM_ATOM    4
#define X_ATOM_STRING 31
#define X_ATOM_WM_HINTS 35
#define X_ATOM_WM_NAME 39
#define X_ATOM_WM_CLASS 67

/* The two halves of WM_CLASS. Every window this program opens carries the
 * same pair, so a desktop groups them together under one application. */
#define GS_WINDOW_INSTANCE "gravestone"
#define GS_WINDOW_CLASS    "Gravestone"
#define X_ATOM_WM_NORMAL_HINTS 40
#define X_ATOM_WM_SIZE_HINTS   41

#define WIN_EVENT_QUEUE_LEN 64

struct gs_window {
    int fd;

    uint32_t id_base;
    uint32_t id_mask;
    uint32_t id_next;

    uint32_t root;
    uint32_t root_visual;
    uint8_t  root_depth;

    /* Largest request the server accepts, in bytes, and whether it wants
     * pixel bytes most significant first. Both come out of the setup. */
    uint32_t max_request_bytes;
    uint8_t  image_byte_order;

    uint32_t window_id;
    uint32_t gc_id;

    /* The frame under construction. Drawing lands here and reaches the
     * window only when gs_window_present copies it across, so the screen
     * never holds a half finished frame. Zero until the first blit. */
    uint32_t pixmap_id;
    int      pixmap_w;
    int      pixmap_h;

    uint32_t atom_wm_protocols;
    uint32_t atom_wm_delete;
    /* Named so the window can ask to be grown to the whole screen. Two
     * atoms, because a manager grows a window sideways and downwards as
     * separate states. */
    uint32_t atom_net_wm_state;
    uint32_t atom_net_wm_max_vert;
    uint32_t atom_net_wm_max_horz;

    /* Cursor shapes, made once from the font every X server carries and
     * kept for the life of the window. Zero until first asked for. */
    uint32_t cursor_font;
    uint32_t cursor_id[GS_WINDOW_CURSOR_COUNT];
    int      cursor_now;

    uint32_t font_id;
    int      font_width;    /* every glyph is the same width in this font */
    int      font_ascent;   /* pixels above the baseline */
    int      font_descent;  /* pixels below it */

    /* Every printable shape, read back off the server once when the
     * window opens. One bit per pixel, one row per line of the shape,
     * with the lowest bit the leftmost pixel. Larger writing is built out
     * of these rather than asked of the server, which carries one size
     * and refuses every other. Zero rows until it has been read. */
    uint32_t glyph[GS_WIN_GLYPH_COUNT][GS_WIN_GLYPH_ROWS];
    int      glyph_ready;

    uint32_t sequence;      /* number the server gives the next request */
    uint32_t gc_foreground; /* mirrors the graphics context, saves traffic */

    int width;
    int height;
    int errors;
    int broken;
    int exposed;    /* the server has shown the window at least once */

    /* The plain meaning of each key, fetched once from the server. The
     * first column of the keyboard map is the unshifted one. */
    uint8_t  min_keycode;
    uint8_t  max_keycode;
    /* What each key means, in the two columns the keyboard map gives.
     * Column nought is the key on its own and column one is the key with
     * shift held, which is where a question mark lives. */
    uint32_t keysym0[256];
    uint32_t keysym1[256];

    unsigned char queue[WIN_EVENT_QUEUE_LEN][32];
    int queue_head;
    int queue_len;
};

/* Requests are built a field at a time so no struct layout or padding rule
 * can change what goes on the wire. */
void     gs_win_put16(unsigned char *p, uint16_t v);
void     gs_win_put32(unsigned char *p, uint32_t v);
uint16_t gs_win_get16(const unsigned char *p);
uint32_t gs_win_get32(const unsigned char *p);
int      gs_win_pad4(int n);

uint32_t gs_win_next_id(struct gs_window *w);
int gs_win_write_all(struct gs_window *w, const void *buf, size_t len);
int gs_win_read_all(struct gs_window *w, void *buf, size_t len);
int gs_win_send_request(struct gs_window *w, const void *buf, size_t len);

int gs_win_connect(int display_number);
int gs_win_display_number(void);
int gs_win_handshake(struct gs_window *w);

/* Returns X_REPLY_OK, X_REPLY_ERROR, X_REPLY_EVENT, or a negative code. */
int gs_win_read_packet(struct gs_window *w, unsigned char *reply_out);
int gs_win_await_reply(struct gs_window *w, unsigned char *reply_out);

/* Same, keeping the bytes that trail the reply. Anything past data_cap is
 * read off the socket and dropped, so the stream stays in step. */
int gs_win_await_reply_data(struct gs_window *w, unsigned char *reply_out,
                         unsigned char *data_out, size_t data_cap,
                         size_t *data_len);
int gs_win_queue_pop(struct gs_window *w, unsigned char *out);

int gs_win_set_foreground(struct gs_window *w, uint32_t colour);

/* Turns one queued packet into an event. Returns 1 when out was filled. */
/* Walks the table the server sends back and fills in what each key
 * means. Each seat carries per meanings in a row, and the first two are
 * the key alone and the key with shift held. A seat carrying only one
 * meaning shifts to the same thing.
 *
 * Split out from the fetch so the walk can be checked against a made up
 * table, since asking a real server means opening a window.
 */
/* Reads every printable shape off the server, once, so larger writing can
 * be built from them. Safe to call twice. */
int gs_win_read_glyphs(struct gs_window *w);

void gs_win_read_keymap(const unsigned char *data, size_t len, unsigned per,
                        unsigned count, unsigned first,
                        uint32_t *plain, uint32_t *upper);

int gs_window_translate(struct gs_window *w, const unsigned char *packet,
                        gs_window_event_t *out);

#endif /* not _WIN32 */

/* ---- the Windows half ---- */
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>

#define GS_WIN_QUEUE_LEN 64

/* Everything one Windows window carries. The frame is the picture every
 * mark lands on, kept off screen until it is presented. */
struct gs_window {
    HWND    hwnd;

    int     width;
    int     height;
    int     min_w;
    int     min_h;
    int     exposed;

    HDC     frame_dc;
    HBITMAP frame_bitmap;
    HBITMAP frame_old;
    unsigned int *frame_pixels;
    int     frame_w;
    int     frame_h;

    HCURSOR cursor;
    int     cursor_now;

    HFONT   font;
    int     font_width;
    int     font_ascent;
    int     font_descent;

    gs_window_event_t queue[GS_WIN_QUEUE_LEN];
    int     queue_head;
    int     queue_len;
};

/* The font the window draws with, opened when the window is made. */
int  gs_win_font_open(struct gs_window *w);
void gs_win_font_close(struct gs_window *w);

/* Lets go of the frame, which is Windows memory rather than ours. */
void gs_win_frame_release(struct gs_window *w);

/* Takes the oldest event off the queue. Returns 0 when it is empty. */
int  gs_win_queue_take(struct gs_window *w, gs_window_event_t *out);

#endif /* _WIN32 */

#endif
