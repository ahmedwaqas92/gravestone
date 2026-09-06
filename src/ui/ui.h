#ifndef GS_UI_H
#define GS_UI_H

#include "gravestone.h"
#include "window.h"

/* Neutral and dark. The surface covers the whole window, so no darker
 * ground shows anywhere around it. */
#define GS_UI_SURFACE_TOP    0x0016161Au
#define GS_UI_SURFACE_BOTTOM 0x001F1F25u
#define GS_UI_ACCENT         0x008A8F9Au

/* The bar across the top, and the controls sitting in it. */
#define GS_UI_BAR_HEIGHT     92
#define GS_UI_BAR_FILL       0x001B1B21u
#define GS_UI_BAR_EDGE       0x00303039u

#define GS_UI_BTN_FACE       0x0026262Eu
#define GS_UI_BTN_FACE_HOVER 0x00333340u
#define GS_UI_BTN_FACE_DOWN  0x001D1D24u
#define GS_UI_BTN_EDGE       0x0044444Fu
#define GS_UI_BTN_LABEL      0x00D6DAE2u
#define GS_UI_BTN_OFF_FACE   0x001D1D22u
#define GS_UI_BTN_OFF_EDGE   0x0028282Eu
#define GS_UI_BTN_OFF_LABEL  0x005A5A64u

#define GS_UI_LED_ON         0x004ED37Fu
#define GS_UI_LED_OFF        0x00C4614Eu
#define GS_UI_STATUS_TEXT    0x009AA0AAu

/* Geometry, in pixels, measured from the window edge. */
#define GS_UI_SCANLINE_STEP    3
#define GS_UI_GRID_STEP       26
#define GS_UI_MARGIN          28
#define GS_UI_BTN_HEIGHT      34
#define GS_UI_BTN_GAP         12
#define GS_UI_BTN_RADIUS       5
#define GS_UI_MOUNT_WIDTH    198
#define GS_UI_SPECS_WIDTH    150

/* The working area under the bar, split into a chooser across the top and
 * three panes below it. */
#define GS_UI_PANE_GAP        14
#define GS_UI_CHOOSER_TOP     14
#define GS_UI_CHOOSER_HEIGHT  30
#define GS_UI_PANE_TOP        (GS_UI_BAR_HEIGHT + GS_UI_CHOOSER_TOP + \
                               GS_UI_CHOOSER_HEIGHT + 16)
#define GS_UI_ROW_HEIGHT      19
#define GS_UI_PANE_HEADER     24
#define GS_UI_DROP_ROWS       12

#define GS_UI_CONFIRM_STEPS   10
#define GS_UI_CONFIRM_W      380
#define GS_UI_CONFIRM_H      150

#define GS_UI_PANE_FILL   0x001A1A20u
#define GS_UI_PANE_EDGE   0x002E2E38u
#define GS_UI_ROW_PICKED  0x00303C4Au
#define GS_UI_ROW_HOVER   0x00262630u
#define GS_UI_HEADER_TEXT 0x008A8F9Au
#define GS_UI_ROW_TEXT    0x00C3C8D2u
#define GS_UI_ROW_DIM     0x00787E88u

#define GS_UI_TITLE       "Gravestone"
#define GS_UI_SPECS_TITLE "Gravestone: device"
#define GS_UI_MOUNT_LABEL "Mount Device to Gravestone"
#define GS_UI_SPECS_LABEL "View Device Specs"

typedef struct {
    int x;
    int y;
    int w;
    int h;
} gs_ui_rect_t;

/* Which control the pointer is over, or which one is held down. */
typedef enum {
    GS_UI_HIT_NONE = 0,
    GS_UI_HIT_MOUNT,
    GS_UI_HIT_SPECS,
    GS_UI_HIT_CHOOSER,     /* the bar that opens the list of categories */
    GS_UI_HIT_DROP_ROW,    /* a category inside that open list */
    GS_UI_HIT_LEFT_ROW,    /* a model that could run here */
    GS_UI_HIT_MID_ROW,     /* a disk models may live on */
    GS_UI_HIT_RIGHT_ROW,   /* a model already on that disk */
    GS_UI_HIT_RIGHT_REMOVE,/* the small x at the end of such a row */
    GS_UI_HIT_LEFT_ADD,    /* the small + at the end of a fit row */
    GS_UI_HIT_CONFIRM_OK,  /* the remove button on the confirm box */
    GS_UI_HIT_CONFIRM_NO   /* the cancel button beside it */
} gs_ui_hit_t;

typedef struct {
    int  mounted;              /* a snapshot exists for this install */
    int  changed;              /* the last mount saw different hardware */
    gs_ui_hit_t hover;
    gs_ui_hit_t pressed;
    char status[64];
    char detail[256];

    /* What the panes have to show, counted rather than copied, since the
     * text is read straight from the catalogue when it is drawn. */
    int  model_count;          /* models in the chosen category that fit */
    int  disk_count;
    int  library_count;        /* models already on the chosen disk */
    int  category_count;       /* every category, including All models */

    int  model_sel;            /* -1 when nothing is chosen */
    int  disk_sel;
    int  category_sel;         /* 0 is All models */
    int  model_scroll;
    int  library_scroll;

    int  chooser_open;
    int  hover_row;            /* the row under the pointer, -1 for none */

    /* What has been typed into the search field. Every keystroke filters
     * the left pane at once. */
    char search[48];

    /* The confirm box that grows over the canvas before a model comes off
     * the disk. Step runs 0 to GS_UI_CONFIRM_STEPS, dir is 1 growing, -1
     * folding, 0 at rest, and row names the model being asked about. */
    int  confirm_step;
    int  confirm_dir;
    int  confirm_row;
    int  confirm_busy;         /* the disk work is running on its thread */
    int  confirm_add;          /* 1 asks to add, 0 asks to remove */
    int  spin_phase;           /* which dot of the spinner leads */
    int  busy_percent;         /* measured download percent, -1 unknown */
} gs_ui_state_t;

gs_ui_rect_t gs_ui_mount_rect(int width);
gs_ui_rect_t gs_ui_specs_rect(int width);

/* The chooser across the top of the working area, and the three panes
 * under it. Each takes the whole window size, since they stretch. */
gs_ui_rect_t gs_ui_chooser_rect(int width, int height);
gs_ui_rect_t gs_ui_left_rect(int width, int height);
gs_ui_rect_t gs_ui_mid_rect(int width, int height);
gs_ui_rect_t gs_ui_right_rect(int width, int height);
gs_ui_rect_t gs_ui_drop_rect(int width, int height, int rows);

/* How many rows a pane can show at its current height. */
int gs_ui_pane_rows(gs_ui_rect_t pane);

int          gs_ui_rect_contains(gs_ui_rect_t r, int x, int y);

/* Which control the point lands on. When it lands on a row, the index of
 * that row is written to row, counting from the top of the list rather
 * than from the top of the pane. */
/* The confirm box at this step of its growth, centred on the canvas, and
 * the two buttons inside it once it is fully open. */
gs_ui_rect_t gs_ui_confirm_rect(int width, int height, int step);
gs_ui_rect_t gs_ui_confirm_ok_rect(int width, int height);
gs_ui_rect_t gs_ui_confirm_no_rect(int width, int height);

/* Non zero while the box is on screen at all, and while it is moving. */
int gs_ui_confirm_visible(const gs_ui_state_t *state);
int gs_ui_confirm_animating(const gs_ui_state_t *state);

/* Advances the box one frame. Returns 1 while there is more to draw. */
int gs_ui_confirm_advance(gs_ui_state_t *state);

/* The search field inside the left pane's header, sitting after the
 * MODELS THAT FIT label and stopping short of the count. */
gs_ui_rect_t gs_ui_search_rect(int width, int height);

/* Feeds one typed character into the search. Printable characters are
 * appended, 8 removes the last one, and anything else is ignored.
 * Returns 1 when the text changed and the pane needs filtering again. */
int gs_ui_search_type(gs_ui_state_t *state, int ch);

/* The rectangle a control covers, for repainting just the strip it sits
 * in when only its hover shade changed. A hit of NONE gives an empty
 * rectangle. */
gs_ui_rect_t gs_ui_hit_rect(const gs_ui_state_t *state, int width,
                            int height, gs_ui_hit_t hit, int row);

/* True when text at this rectangle would sit under the open category
 * list, which covers whatever it overlaps. */
int gs_ui_text_covered(const gs_ui_state_t *state, int width, int height,
                       gs_ui_rect_t r);

gs_ui_hit_t  gs_ui_hit_test(const gs_ui_state_t *state, int width,
                            int height, int x, int y, int *row);

/* True when a keycode should close the window. Only Escape does, since
 * this application exists to take typed prompts and must never treat an
 * ordinary letter as a command. */
int gs_ui_key_closes(int keycode);

int  gs_ui_paint(gs_window_t *window, const gs_ui_state_t *state);
void gs_ui_release(void);

extern const gs_module gs_ui_module;

#endif
