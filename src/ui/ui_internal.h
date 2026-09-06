/* Shared between the .c files inside src/ui/ only. */
#ifndef GS_UI_INTERNAL_H
#define GS_UI_INTERNAL_H

#include <stddef.h>
#include "ui.h"
#include "catalogue.h"
#include "detect.h"
#include "library.h"
#include "window.h"

/* The smaller panel that lists what was stored. It is driven from the same
 * loop as the main window, so the main window keeps answering while the
 * panel is open and while it animates. */
#define GS_UI_SPECS_W        560
#define GS_UI_SPECS_H        430
#define GS_UI_SPECS_START_W   64
#define GS_UI_SPECS_START_H   36
#define GS_UI_SPECS_STEPS     14
#define GS_UI_SPECS_FRAME_MS  18

/* Folding away moves several steps at once, since a panel on its way out
 * wants to be gone rather than admired. It runs all the way down to its
 * smallest size, so the jump from the last frame to nothing is too small
 * to notice. */
#define GS_UI_SPECS_CLOSE_STRIDE 4
#define GS_UI_SPECS_CLOSE_FLOOR  0
#define GS_UI_SPECS_MAX_ROWS  24

typedef struct {
    char key[32];
    char value[224];
} gs_ui_row_t;

typedef struct {
    gs_window_t *window;
    gs_ui_row_t  rows[GS_UI_SPECS_MAX_ROWS];
    int          count;
    int          step;        /* 0 is folded away, STEPS is fully open */
    int          direction;   /* 1 opening, -1 closing, 0 at rest */
    int          want_w;      /* the size the last frame asked for */
    int          want_h;
    int          centre_x;    /* the point the panel grows and folds about */
    int          centre_y;
    int          centred;     /* zero when no centre could be worked out */
} gs_ui_specs_t;

/* The lists the three panes show. They are held here rather than copied
 * into the state, since a repaint runs many times over the same rows and
 * copying a hundred model names on every frame would be waste.
 *
 * Refresh reads the machine, loads the catalogue, and picks a disk. Pick
 * walks the chosen disk for models already sitting on it. */
void gs_ui_panes_refresh(gs_ui_state_t *state,
                         const gs_detect_report_t *machine);
void gs_ui_panes_pick_disk(gs_ui_state_t *state, int index);

/* Narrows the left pane to one kind of model. Index 0 keeps them all. */
void gs_ui_panes_pick_category(gs_ui_state_t *state, int index);

/* Finds the catalogue row a model on disk corresponds to, switches the
 * chooser to that row's category, and selects it in the left pane. The
 * name matches exactly, or the part before the colon matches and the
 * sizes agree byte for byte, since both sides read the same registry.
 * Returns GS_OK on a match and GS_ERR when the model is unknown, leaving
 * the state untouched. */
int gs_ui_panes_locate(gs_ui_state_t *state,
                       const gs_library_entry_t *owned);

int         gs_ui_panes_category_count(void);
const char *gs_ui_panes_category_name(int index);
int         gs_ui_panes_category_total(int index);

int gs_ui_panes_model_count(void);
int gs_ui_panes_disk_count(void);

/* Entry for a row, or NULL when the index is out of range. */
const gs_catalogue_entry_t *gs_ui_panes_model(int index);
const gs_detect_disk_t     *gs_ui_panes_disk(int index);

/* Draws the chooser, the three panes and the open list into the buffer. */
void gs_ui_panes_compose(unsigned int *pixels, int w, int h,
                         const gs_ui_state_t *state);

/* Draws the text that sits inside them, after the image has landed. */
void gs_ui_panes_labels(gs_window_t *window, const gs_ui_state_t *state);

/* The actions in ui_actions.c that the event loop dispatches to. */
void gs_ui_refresh_state(gs_ui_state_t *state);
void gs_ui_do_mount(gs_ui_state_t *state);
int  gs_ui_scroll_under(gs_ui_state_t *state, gs_window_t *window, int x,
                        int y, int by);
/* Starts the removal on a thread of its own, so the window keeps
 * animating while ollama works. Returns GS_OK once the thread is away,
 * and refuses a second start while one is running. */
int  gs_ui_remove_begin(gs_ui_state_t *state);

/* Starts a pull on the same worker thread, aimed at the chosen disk.
 * The same poll answers for both directions of disk work. */
int  gs_ui_add_begin(gs_ui_state_t *state);

/* The measured percentage of the pull now running, from the growth of
 * the store's partial files against the row's known size. Minus one
 * outside a pull or before anything has landed. Clamped to 99 while the
 * work runs, since only completion earns the last point. */
int  gs_ui_add_progress(void);

/* The clamping arithmetic alone, for feeding poison in tests. */
int  gs_ui_progress_percent(long long partial, long long expected);

/* Asks whether the thread has finished. Returns 1 exactly once, after
 * writing the outcome into the state and walking the disk again. */
int  gs_ui_remove_poll(gs_ui_state_t *state);

/* The confirm box drawn over everything, pixels first and glyphs after,
 * from ui_confirm.c. */
void gs_ui_confirm_compose(unsigned int *pixels, int w, int h,
                           const gs_ui_state_t *state);
void gs_ui_confirm_labels(gs_window_t *window, const gs_ui_state_t *state);

/* Pixel work shared with ui_paint.c. A rectangle with square corners, and
 * one with its corners taken off. */
void gs_ui_px_rect(unsigned int *pixels, int w, int h, gs_ui_rect_t r,
                   unsigned int colour);
void gs_ui_px_rounded(unsigned int *pixels, int w, int h, gs_ui_rect_t r,
                      int radius, unsigned int face, unsigned int edge);

/* Composes the canvas into pixels, which holds w times h entries of
 * 0x00RRGGBB laid out row by row. */
void gs_ui_compose(unsigned int *pixels, int w, int h,
                   const gs_ui_state_t *state);

/* The ground under everything: gradient, scanlines and grid, with no bar
 * and no panes. Kept apart so it can be checked on its own. */
void gs_ui_compose_surface(unsigned int *pixels, int w, int h);

/* Draws the labels and status text over an already blitted canvas, since
 * glyphs come from the server rather than from our own pixels. */
void gs_ui_draw_labels(gs_window_t *window, const gs_ui_state_t *state);

/* Fills rows from what the store holds. Returns how many were written. */
int  gs_ui_specs_gather(gs_ui_row_t *rows, int cap);

/* Opens the panel at its smallest and starts it growing, centred on the
 * window passed in. */
int  gs_ui_specs_open(gs_ui_specs_t *panel, gs_window_t *centre_on);

/* Starts it folding away. Reaching zero is reported by gs_ui_specs_spent. */
void gs_ui_specs_begin_close(gs_ui_specs_t *panel);

/* True when the window has caught up with the size the last frame asked
 * for. Sending the next frame before then piles requests up behind the
 * window manager, which shows as the panel lagging its own animation. */
int  gs_ui_specs_settled(const gs_ui_specs_t *panel);

/* Advances one frame. Returns 1 while there is more to do. */
int  gs_ui_specs_advance(gs_ui_specs_t *panel);

int  gs_ui_specs_animating(const gs_ui_specs_t *panel);
int  gs_ui_specs_spent(const gs_ui_specs_t *panel);

void gs_ui_specs_paint(gs_ui_specs_t *panel);
void gs_ui_specs_destroy(gs_ui_specs_t *panel);

#endif
