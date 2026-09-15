/* window.h
 *
 * A window on the screen, with nothing platform specific showing through.
 * The X11 version lives in window_x11.c and the Windows one in
 * window_win32.c. Neither touches any other directory.
 *
 * Colours are 0x00RRGGBB.
 */
#ifndef GS_LIB_WINDOW_H
#define GS_LIB_WINDOW_H

#include <stddef.h>

typedef struct gs_window gs_window_t;

typedef enum {
    GS_WINDOW_EVENT_NONE = 0,
    GS_WINDOW_EVENT_EXPOSE,   /* the window needs repainting */
    GS_WINDOW_EVENT_RESIZE,   /* width and height carry the new size */
    GS_WINDOW_EVENT_KEY,      /* key carries the raw keycode */
    GS_WINDOW_EVENT_CLICK,    /* x and y carry where, button carries which */
    GS_WINDOW_EVENT_RELEASE,  /* the same button was let go of */
    GS_WINDOW_EVENT_MOVE,     /* the pointer moved to x and y */
    GS_WINDOW_EVENT_CLOSE     /* the user asked to close it */
} gs_window_event_kind_t;

typedef struct {
    gs_window_event_kind_t kind;
    int width;
    int height;
    int key;
    int ch;      /* the key's plain character, 0 when it has none.
                    Backspace arrives as 8, Escape as 27 and Return as
                    10, since all three carry those values in plain text
                    everywhere. */
    int mods;    /* which holding keys were down, see GS_WINDOW_MOD_ */
    int x;
    int y;
    int button;               /* 1 is the left one */
} gs_window_event_t;

/* The keys held down while another was pressed. Shift already picks the
 * character, so a caller reads it to tell one press of a key apart from
 * another, Return from Shift and Return for instance. */
#define GS_WINDOW_MOD_SHIFT   0x01
#define GS_WINDOW_MOD_CONTROL 0x02
#define GS_WINDOW_MOD_ALT     0x04

/* True when a window could be opened right now. A compositor under load
 * refuses connections for a moment, and telling that apart from a real
 * failure matters to anything deciding whether to carry on. */
int gs_window_display_available(void);

gs_window_t *gs_window_open(const char *title, int width, int height);
void         gs_window_close(gs_window_t *window);

/* Waits up to timeout_ms for an event. A negative timeout waits forever.
 * Returns 1 when out was filled, 0 on timeout, GS_ERR when the connection
 * broke. */
int  gs_window_wait_event(gs_window_t *window, gs_window_event_t *out,
                          int timeout_ms);

void gs_window_fill(gs_window_t *window, int x, int y, int w, int h,
                    unsigned int colour);

/* Copies a whole image onto the window in one go. pixels holds w times h
 * entries of 0x00RRGGBB, laid out row by row. Large images are split into
 * horizontal bands, since a server caps how much one request may carry. */
int  gs_window_blit(gs_window_t *window, const unsigned int *pixels,
                    int w, int h);

/* Sends only the rows from y for rows_high rows out of the same full
 * canvas, for a change that touched one strip of the picture. Falls back
 * to sending everything when no frame of this size exists yet. */
int  gs_window_blit_rect(gs_window_t *window, const unsigned int *pixels,
                         int w, int h, int y, int rows_high);

/* Copies the finished frame onto the window in one operation. Drawing
 * lands on an off screen picture the X server keeps, so nothing shows
 * until this runs. */
int  gs_window_present(gs_window_t *window);

/* True once a finished frame exists, so an exposure can be answered by
 * presenting it again rather than by drawing everything from scratch. */
int  gs_window_has_frame(const gs_window_t *window);
void gs_window_flush(gs_window_t *window);

/* Draws text with the baseline at y. The window carries one fixed width
 * font, loaded when it opens. */
void gs_window_text(gs_window_t *window, int x, int y, const char *text,
                    unsigned int colour);

/* Metrics of that font, so a caller can place and size things. */
int gs_window_text_width(const gs_window_t *window, const char *text);
int gs_window_font_height(const gs_window_t *window);
int gs_window_font_ascent(const gs_window_t *window);

/* Tells the window manager what size this window wants, how small it may
 * usefully go, and where it would like to sit. Without this a manager is
 * free to pick both, which is why a window can come up far smaller than it
 * asked for, or somewhere it was never put. Pass a negative x to leave the
 * position to the manager. */
int gs_window_set_hints(gs_window_t *window, int x, int y,
                        int want_w, int want_h, int min_w, int min_h);

/* Where the top left corner of this window sits on the screen. */
int gs_window_position(gs_window_t *window, int *x, int *y);

/* Moves and resizes in one request, so a window growing about a fixed
 * centre never lands anywhere in between. */
int gs_window_place(gs_window_t *window, int x, int y, int w, int h);

/* Asks the window manager to change the size. The new size arrives back
 * as a resize event rather than taking effect at once. */
int gs_window_resize(gs_window_t *window, int w, int h);

/* Waits on several windows at once. Sets *which to the index of the window
 * the event came from. Same return values as gs_window_wait_event. */
int gs_window_wait_any(gs_window_t **windows, int count, int *which,
                       gs_window_event_t *out, int timeout_ms);

/* The shape the pointer takes over one part of the window. A person
 * reading a picture of a pointer knows what a control does before
 * pressing it, so the shape follows what is underneath. */
typedef enum {
    GS_WINDOW_CURSOR_ARROW = 0,  /* the ordinary pointer */
    GS_WINDOW_CURSOR_TEXT,       /* a bar, over somewhere words are typed */
    GS_WINDOW_CURSOR_HAND,       /* a hand, over something that can be pressed */
    GS_WINDOW_CURSOR_COUNT
} gs_window_cursor_t;

/* Sets the shape the pointer takes anywhere over this window. Asking for
 * the shape it already has costs nothing, so a caller may set it on every
 * pointer movement. Returns GS_OK when the request went out. */
int gs_window_set_cursor(gs_window_t *window, gs_window_cursor_t shape);

/* Writes text at several times its ordinary size.
 *
 * The screen this program runs under carries one font, six pixels wide
 * and thirteen tall, and refuses every larger size asked of it. Every
 * shape in that font is read back once when the window opens, so a larger
 * letter is built here out of the small one rather than asked for.
 *
 * A letter is drawn as a block of coloured pixels, so the colour behind
 * it has to be given as well. Scale one gives the ordinary size. Scale is
 * held between one and four.
 *
 * Returns GS_OK when the writing went out. */
/* Writes in the letters the chat is written in, which are carried with
 * the program rather than asked of the screen.
 *
 * The letters have differing widths, so a caller wanting to know how much
 * room a line takes asks gs_window_face_width rather than counting
 * characters. The colour behind has to be given, since a letter is drawn
 * as a block of pixels with no way to see through it.
 *
 * Returns GS_OK when the writing went out. */
int gs_window_face_text(gs_window_t *window, int x, int y, const char *text,
                        unsigned int colour, unsigned int behind);

/* How many pixels wide that writing is, and how tall one line stands.
 * The width of a line cut short at n characters is measured by passing a
 * length rather than a terminated string. */
int gs_window_face_width(const char *text);
int gs_window_face_width_n(const char *text, size_t len);
int gs_window_face_height(void);
int gs_window_face_ascent(void);

int gs_window_text_scaled(gs_window_t *window, int x, int y,
                          const char *text, unsigned int colour,
                          unsigned int behind, int scale);

/* How wide and how tall that writing is, so a caller can leave room for
 * it. Both come back as nought when the window carries no shapes. */
int gs_window_text_scaled_width(const gs_window_t *window, const char *text,
                                int scale);
int gs_window_text_scaled_height(const gs_window_t *window, int scale);

/* Asks the window manager to grow the window to the whole working area,
 * or to put it back. The frame stays, so the buttons that shrink, grow
 * and close the window are still along its edge.
 *
 * The new size arrives back as a resize event rather than taking effect
 * at once, the same way an ordinary resize does. A manager that does not
 * answer leaves the window as it was, which is why nothing here waits for
 * a reply. */
int gs_window_maximise(gs_window_t *window, int on);

/* Non zero while the window can still be drawn on and asked about. A
 * server that drops a client leaves the window standing but useless, and
 * everything asked of it from that point fails for the one reason. */
int gs_window_connected(const gs_window_t *window);

/* Gives the wait one more descriptor to watch alongside the windows. A
 * program that has to answer something other than a window, a stop signal
 * for instance, hands the reading end of a pipe here, and the wait then
 * returns 0 the moment a byte arrives on it instead of sitting still.
 * Pass -1 to go back to watching windows alone. The descriptor is never
 * read from or closed here, so whoever opened it still owns it, and a
 * caller that leaves a byte sitting on it will find every later wait
 * returning at once. */
void gs_window_set_wake_fd(int fd);

/* Reads one pixel back off the window. Returns GS_OK when out was set.
 * The window has to be on screen and unobscured for the answer to mean
 * anything, which the protocol leaves undefined otherwise. */
int  gs_window_read_pixel(gs_window_t *window, int x, int y,
                          unsigned int *out);

int  gs_window_width(const gs_window_t *window);
int  gs_window_height(const gs_window_t *window);

/* Non zero once the server has reported a protocol error on this
 * connection. Used by the tests. */
int  gs_window_error_count(const gs_window_t *window);

#endif
