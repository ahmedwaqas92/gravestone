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
/* The box that asks for the machine password once per drive. Wider than
 * the confirm box, since it has to say what the one line does before
 * anybody types anything into it. */
#define GS_UI_SECRET_W      440
#define GS_UI_SECRET_H      220
#define GS_UI_SECRET_DOTS    40

#define GS_UI_SPECS_W        560
#define GS_UI_SPECS_H        430
#define GS_UI_SPECS_START_W   64
#define GS_UI_SPECS_START_H   36
#define GS_UI_SPECS_STEPS     14
#define GS_UI_SPECS_FRAME_MS  18

/* How long the caret stays on, and how long it stays off. A typist gets
 * the caret held steady instead, since a shape flashing under the letter
 * being written reads as a stutter. The clock only starts again once the
 * keyboard has been quiet for this long. */
#define GS_UI_CARET_MS       530

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

/* The reading the model list was built from. Labels drawn against that
 * list ask this rather than reading the machine again, so a row can never
 * be called too large inside a panel headed by what fits. Comes back NULL
 * before the first refresh has succeeded. */
const gs_detect_report_t   *gs_ui_panes_machine(void);

/* Where a model of this size would run, in a few letters for a narrow
 * column. A model divided between the card and system memory carries the
 * share held on the card, as in "SPLIT 69%", since the part left in
 * system memory is read across the bus once for every word produced and
 * decides how slowly the answer arrives. Writes an empty string when the
 * machine is unknown. */
void gs_ui_where(long long bytes, const gs_detect_report_t *machine,
                 char *out, size_t cap);

/* The same answer dressed as a tag: the words, the colour of the letters,
 * the colour of the pill behind them, and how wide that pill has to be.
 * Where the pill sits is decided by whoever draws the row, since only
 * that caller knows the row.
 *
 * Both drawing passes ask this, so the pill and the words inside it
 * cannot disagree. Returns nought when the machine is unknown. */
int gs_ui_model_tag(long long bytes, const gs_detect_report_t *machine,
                    gs_ui_tag_t *out);

/* The space inside a pane before its words start, and the characters
 * kept at the right of a row for a byte count. "999.9 GB" is the longest
 * that ever prints. Both passes measure against these. */
#define GS_UI_PANE_PAD   10
#define GS_UI_SIZE_COLUMN 8

/* The mark at the end of a row that pulls a model in or takes one out.
 * It keeps clear of the bar down the right, since a person reaching for
 * a six pixel rail and missing it by two would otherwise start a
 * download. */
#define GS_UI_ROW_MARK_W   14
#define GS_UI_ROW_MARK_GAP  9

/* Where that mark sits inside a row, and the band it answers a press in.
 * Both drawing and the hit test ask this, so the mark cannot be drawn in
 * one place and pressed in another.
 *
 * The room for the bar is kept whether or not a bar is showing, so the
 * mark holds still as a list grows past its box. */
gs_ui_rect_t gs_ui_row_mark_rect(gs_ui_rect_t row);

/* The bar down the right of a list, and the grip inside it. Comes back
 * nought when the list already fits, which leaves both rectangles empty.
 *
 * Every list asks this, so the pass that fills the shape and the pass
 * that answers a click place the bar identically. */
int gs_ui_scrollbar(gs_ui_rect_t box, int total, int visible, int scroll,
                    gs_ui_rect_t *track, gs_ui_rect_t *thumb);

/* Takes hold of whichever bar the point lands on, and answers non zero
 * when one was taken. The list moves at once, and carries on following
 * the pointer until the button is let go.
 *
 * Every list in the interface is offered, so a caller asks once rather
 * than testing each panel itself. */
int gs_ui_bar_grab(gs_ui_state_t *state, int width, int height,
                   int x, int y);

/* Moves whichever bar is held to follow the pointer. Answers non zero
 * when something moved, which is the panel's cue to paint again. */
int gs_ui_bar_drag(gs_ui_state_t *state, int width, int height, int y);

/* Lets go. */
void gs_ui_bar_drop(gs_ui_state_t *state);

/* Whether a point lands on the bar of one of the three workspace panes.
 * Writes the row a press there would put at the top. Kept beside the bar
 * geometry rather than in the hit test, since only this file knows how a
 * bar is placed. */
int gs_ui_scroll_hit(const gs_ui_state_t *state, int width, int height,
                     int x, int y, int *row);

/* Which row a press at that height on the bar would put at the top. The
 * grip is taken hold of by its middle, so a press halfway down the rail
 * lands halfway down the list. */
int gs_ui_scroll_from(gs_ui_rect_t box, int total, int visible, int y);

/* One row of a workspace pane, in window pixels. Shared so the pass that
 * fills shapes and the pass that draws words place a row identically. */
gs_ui_rect_t gs_ui_pane_row_rect(gs_ui_rect_t pane, int slot);

/* Whether the window has room for the three panes at all. */
int gs_ui_panes_visible(int w, int h);

/* The tag for one row of a pane, placed inside that row. Both drawing
 * passes call this rather than each working the place out. */
int gs_ui_row_tag(gs_ui_rect_t pane, int slot, int index,
                  gs_ui_tag_t *out);

/* The tag for one visible row of the model list, placed inside that row.
 * Both drawing passes ask this, so the pill and the words inside it land
 * together. Returns nought for a row with no tag, and for a pane too
 * narrow to carry one without covering the model name. */
int gs_ui_model_tag_at(int width, int height, int slot, int index,
                       gs_ui_tag_t *out);

/* The column the byte count is written in, which every tag stays clear
 * of. Fixed in width, so the pills line up down the list. */
gs_ui_rect_t gs_ui_model_size_rect(int width, int height, int slot);

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

/* Settings that outlive the session, in ui_settings.c. Save writes the
 * chosen disk, category and model, together with an inventory of what
 * that disk held. Restore reads them back and checks the disk is still
 * attached, filling state->alert when it has gone. */
/* The setting naming the conversation this window was last in. */
#define GS_UI_KEY_CHAT "chat.session"

/* The Windows drives, watched on a thread of their own. begin starts the
 * thread, which puts drives back at once and every so often after. poll
 * takes what it found, refreshes the disks when anything changed, and
 * opens the setup box for a drive with no road to root. wait stops it. */
int  gs_ui_mount_begin(void);
int  gs_ui_mount_running(void);
int  gs_ui_mount_poll(gs_ui_state_t *state);
void gs_ui_mount_wait(void);

void gs_ui_settings_save(const gs_ui_state_t *state);

/* The list of chosen model names a save writes. Ticked models in the
 * library loaded now, plus every name in saved whose model is not in that
 * library, so a session started with the disk missing cannot erase the
 * choice. out is newline separated. */
void gs_ui_settings_merge_chosen(const gs_ui_state_t *state,
                                 const char *saved, char *out, size_t cap);
void gs_ui_settings_restore(gs_ui_state_t *state);
void gs_ui_settings_record_models(const gs_ui_state_t *state);

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

/* Waits for the worker to finish and lets go of it. The worker reads the
 * library table straight out of the array that holds it, so nothing may
 * empty that array or leave the program while the thread is still out.
 * Returns at once when no worker is running. */
void gs_ui_remove_wait(void);

/* Non zero while the worker is out, so nothing rewrites the library
 * table under it. */
int  gs_ui_remove_running(void);

/* Puts a file box on the screen and takes the answer. The box belongs to
 * the system rather than to this program, and it does not come back until
 * the person answers, so it runs on a thread of its own and the window
 * keeps painting meanwhile.
 *
 * Returns GS_OK once the thread is out, or an error when a box is already
 * open, when no more files fit, or when this machine has no box.
 */
int  gs_ui_attach_begin(gs_ui_state_t *state);

/* Non zero once the answer has been taken and the state updated. Call it
 * every time round the loop while state->attach_open is set. */
int  gs_ui_attach_poll(gs_ui_state_t *state);

/* Waits for the box to be answered and lets go of the thread. */
void gs_ui_attach_wait(void);

/* Non zero while the box is up on its thread. */
int  gs_ui_attach_running(void);

/* Forgets one chosen file. Out of range indexes are ignored. */
void gs_ui_attach_drop(gs_ui_state_t *state, int index);

/* Writes the prompt down as the next turn of the conversation, starting a
 * conversation when there is none yet, and empties the box. Nothing is
 * asked of any model here, since sending to models is the next thing to
 * be written.
 *
 * Returns GS_OK once the prompt is on record, or an error when the box is
 * empty or the file will not take it. */
int gs_ui_chat_send(gs_ui_state_t *state);

/* Reads the conversation back out of the file into what the panel shows.
 * Called after something is sent, and once when the program starts, so a
 * conversation carries on where it was left. */
void gs_ui_chat_reload(gs_ui_state_t *state);

/* Moves a run along and collects it once it finishes. Called every time
 * round the loop, and it does nothing while no run is going.
 *
 * Returns non zero while a run is in hand, which is the panel's cue to
 * paint again. */
int gs_ui_harness_poll(gs_ui_state_t *state);

/* The machine, read at most once every few seconds and held between
 * calls. Reading it starts the vendor tools and costs about a third of a
 * second, so a panel drawing a row for every model asks this rather than
 * gs_detect_read. Comes back NULL while no reading has succeeded.
 *
 * Called from the thread that draws, and from nowhere else. */
const gs_detect_report_t *gs_ui_machine(void);

/* Draws the record over the panel while one is open. The shape goes into
 * the pixels, the words go through the window, matching how the rest of
 * the panel is drawn. Both do nothing while no record is open. */
void gs_ui_record_compose(unsigned int *px, int w, int h,
                          const gs_ui_state_t *state);
void gs_ui_record_draw(gs_window_t *win, const gs_ui_state_t *state);

/* Adds one line from Gravestone to the panel. It carries no record, since
 * nothing behind it is in the file. Does nothing once the panel is full. */
void gs_ui_say(gs_ui_state_t *state, const char *text);

/* A number that changes each time it is asked for, stamped on the lines of
 * the history whenever they change, so a measurement of them taken before
 * is known to be stale. */
long long gs_ui_chat_generation(void);

/* Picks up the conversation last written to, so the panel opens on the
 * words the person left there. Does nothing when there is none. */
void gs_ui_chat_resume(gs_ui_state_t *state);

/* The box asking how many models answer, from ui_fleet.c. */
void gs_ui_fleet_compose(unsigned int *pixels, int w, int h,
                         const gs_ui_state_t *state);
void gs_ui_fleet_labels(gs_window_t *window, const gs_ui_state_t *state);

/* The chat panel on the home screen, from ui_chat.c. */
void gs_ui_chat_compose(unsigned int *pixels, int w, int h,
                        const gs_ui_state_t *state);
void gs_ui_chat_labels(gs_window_t *window, const gs_ui_state_t *state);

/* The home screen, a bare canvas with a settings gear, from ui_home.c. */
void gs_ui_home_compose(unsigned int *pixels, int w, int h,
                        const gs_ui_state_t *state);
void gs_ui_home_labels(gs_window_t *window, const gs_ui_state_t *state);

/* The confirm box drawn over everything, pixels first and glyphs after,
 * from ui_confirm.c. */
void gs_ui_secret_compose(unsigned int *pixels, int w, int h,
                          const gs_ui_state_t *state);
void gs_ui_secret_labels(gs_window_t *win, const gs_ui_state_t *state);

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
