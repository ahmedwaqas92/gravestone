#ifndef GS_UI_H
#define GS_UI_H

#include "gravestone.h"
#include "detect.h"
#include "window.h"

#include <stddef.h>

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

/* The chat panel down the left of the home screen, running the full
 * height between the margins. The composer holds the typed prompt with a
 * row of buttons under it, and the buttons keep clear of its edge. */
#define GS_UI_CHAT_SHARE      46      /* per cent of the width it takes */
#define GS_UI_CHAT_COMPOSER  104
#define GS_UI_CHAT_BUTTONS    42
#define GS_UI_CHAT_SIDE       26
#define GS_UI_CHAT_FOOT        9      /* clear space under the buttons */

/* The chat controls stand against the panel rather than blending into
 * it, so they carry a blue leaning face of their own. */
#define GS_UI_CHAT_FACE      0x00354A66u
#define GS_UI_CHAT_HOVER     0x00476488u
#define GS_UI_CHAT_EDGE      0x006E8CB8u
#define GS_UI_CHAT_INK       0x00EAF1FBu
#define GS_UI_CHAT_LIVE      0x004E7FB8u
#define GS_UI_CHAT_CURSOR    0x00BFD4EEu

/* The box listing the models on the disk, where the ones answering the
 * next prompt are ticked. */
#define GS_UI_FLEET_STEPS     8
#define GS_UI_FLEET_W       396
#define GS_UI_FLEET_ROW      22
#define GS_UI_FLEET_HEAD     52
#define GS_UI_FLEET_FOOT     26
#define GS_UI_FLEET_ROWS      8      /* rows shown before it scrolls */
#define GS_UI_CHOSEN_MAX    256      /* models a choice can be kept for */

/* What the plus accepts. */
#define GS_UI_ATTACH_TYPES \
    "xlsx xls pdf doc docx png jpg jpeg webp ppt pptx mov mp3 mp4"

#define GS_UI_ATTACH_MAX     8      /* files held against one prompt */
#define GS_UI_ATTACH_PATH  512

/* The panel holds one page of the history at a time. Every prompt gives
 * two lines, itself and whatever came back or the reason nothing did, and
 * a page can carry a line at each end for moving to the page beside it,
 * so a page of 63 prompts fills 128 lines exactly. */
#define GS_UI_HISTORY_MAX  128      /* lines the panel keeps in hand */
#define GS_UI_HISTORY_PAGE  63      /* prompts on one page */
/* Each line holds as much as the session keeps of one answer. It was 256
 * bytes, which cut every answer longer than a short paragraph on screen
 * while the whole of it sat in the database. */
#define GS_UI_HISTORY_TEXT 8192
#define GS_UI_RECORD_TEXT 8192      /* the record of one run */
#define GS_UI_RECORD_FULL 32768     /* the sheet, working out and all */
/* The chat is written in letters carried with the program rather than
 * asked of the screen, which offers one size and refuses every other.
 * They stand twenty pixels tall and each has its own width. */
#define GS_UI_BUBBLE_LINE   26      /* height of one line inside a bubble */
#define GS_UI_BUBBLE_HEAD   22      /* the name above the words */
/* The moment beside a name on a bubble, dimmer than the name so the name
 * still leads. */
#define GS_UI_BUBBLE_WHEN      0x00707784u
#define GS_UI_BUBBLE_WHEN_GAP  8

#define GS_UI_BUBBLE_PAD_X  16      /* clear space to the left and right */
#define GS_UI_BUBBLE_PAD_Y  12      /* and above and below */
#define GS_UI_BUBBLE_GAP    16      /* between one bubble and the next */
/* The strip right of the history that the scroll bar sits in, beside
 * the panel's own margin. The bar lands seven pixels clear of the widest
 * bubble. */
#define GS_UI_CHAT_BAR_ROOM   6
/* How far one notch of the wheel moves the history, three lines. */
#define GS_UI_CHAT_WHEEL     (3 * GS_UI_BUBBLE_LINE)
#define GS_UI_BUBBLE_SHARE  80      /* per cent of the width one may take */

/* The person writes on the right, the program answers on the left, which
 * is the shape every message panel uses. */
#define GS_UI_BUBBLE_MINE   0x00335A8Cu
#define GS_UI_BUBBLE_THEIRS 0x00262A33u
#define GS_UI_BUBBLE_EDGE   0x003E4653u
#define GS_UI_BUBBLE_NAME   0x008A93A3u

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

/* The bar down the right of a list. The rail is the whole run of it and
 * the grip is the part that moves. */
#define GS_UI_SCROLL_RAIL  0x0022222Au
#define GS_UI_SCROLL_GRIP  0x00464653u
#define GS_UI_SCROLL_HELD  0x006A6A7Cu
#define GS_UI_SCROLL_WIDTH      6
#define GS_UI_SCROLL_INSET      3
#define GS_UI_SCROLL_MIN_THUMB 18

/* The height of one line in the record panel. The pass that fills shapes
 * has no window to ask for a font height, so the two passes agree on this
 * figure instead. */
#define GS_UI_RECORD_LINE      17

/* Where a model would run, worn as a tag on its row. Green for one the
 * card holds whole, since that answers at conversational speed. Amber for
 * one divided with system memory, since the part outside the card crosses
 * the bus once for every word. Blue for one the processor takes on its
 * own. Red for one too large for the machine.
 *
 * Each colour comes in a pair, the ink for the letters and the face for
 * the pill behind them. */
#define GS_UI_TAG_CARD_INK    0x006FD79Au
#define GS_UI_TAG_CARD_FACE   0x00203528u
#define GS_UI_TAG_SPLIT_INK   0x00E0A860u
#define GS_UI_TAG_SPLIT_FACE  0x00352B1Cu
#define GS_UI_TAG_CPU_INK     0x0078AEDCu
#define GS_UI_TAG_CPU_FACE    0x001E2B38u
#define GS_UI_TAG_NONE_INK    0x00D97A66u
#define GS_UI_TAG_NONE_FACE   0x00351F1Cu

/* The pill around a tag: the space either side of the words, and how far
 * it sits from the size beside it. */
#define GS_UI_TAG_PAD      6
#define GS_UI_TAG_GAP      8
#define GS_UI_TAG_HEIGHT  15


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

/* A tag on one row, worked out once and drawn twice. The shape goes into
 * the pixels and the words go through the window, so both passes ask for
 * the same answer rather than each working it out. */
/* Which list a grip belongs to. Nought is nothing held, so a state
 * straight out of memset holds nothing. */
/* What one line of the history is. */
typedef enum {
    GS_UI_LINE_SAID = 0,       /* something the person or Gravestone said */
    GS_UI_LINE_EARLIER,        /* pressed to load the page of older prompts */
    GS_UI_LINE_NEWER           /* pressed to go back to the newer page */
} gs_ui_line_t;

typedef enum {
    GS_UI_BAR_NONE = 0,
    GS_UI_BAR_MODELS,          /* the left pane of the workspace */
    GS_UI_BAR_LIBRARY,         /* the right pane of the workspace */
    GS_UI_BAR_CHAT,            /* the history on the home screen */
    GS_UI_BAR_FLEET,           /* the box of models a prompt goes to */
    GS_UI_BAR_RECORD           /* the record behind an answer */
} gs_ui_bar_t;

typedef struct {
    char         text[24];
    unsigned int ink;
    unsigned int face;
    gs_ui_rect_t box;
} gs_ui_tag_t;

/* Which control the pointer is over, or which one is held down. */
typedef enum {
    GS_UI_HIT_NONE = 0,
    GS_UI_HIT_MOUNT,
    GS_UI_HIT_SPECS,
    GS_UI_HIT_CHOOSER,     /* the bar that opens the list of categories */
    GS_UI_HIT_DROP_ROW,    /* a category inside that open list */
    GS_UI_HIT_SCROLL,      /* the bar down the right of a list */
    GS_UI_HIT_LEFT_ROW,    /* a model that could run here */
    GS_UI_HIT_MID_ROW,     /* a disk models may live on */
    GS_UI_HIT_RIGHT_ROW,   /* a model already on that disk */
    GS_UI_HIT_RIGHT_REMOVE,/* the small x at the end of such a row */
    GS_UI_HIT_LEFT_ADD,    /* the small + at the end of a fit row */
    GS_UI_HIT_SECRET_OK,   /* the set up button on the password box */
    GS_UI_HIT_SECRET_NO,   /* the not now button beside it */
    GS_UI_HIT_CONFIRM_OK,  /* the remove button on the confirm box */
    GS_UI_HIT_CONFIRM_NO,  /* the cancel button beside it */
    GS_UI_HIT_SETTINGS,    /* the gear in the corner of the home screen */
    GS_UI_HIT_BACK,        /* the arrow returning the workspace to home */
    GS_UI_HIT_CHAT_BOX,    /* the prompt box on the home screen */
    GS_UI_HIT_CHAT_SEND,   /* the arrow that would send the prompt */
    GS_UI_HIT_CHAT_ATTACH, /* the plus that would attach a file */
    GS_UI_HIT_FLEET,       /* the button naming how many models answer */
    GS_UI_HIT_FLEET_OPTION,/* one model inside the box it opens */
    GS_UI_HIT_FLEET_CLOSE, /* the cross in the corner of that box */
    GS_UI_HIT_FLEET_AWAY,  /* anywhere else while that box is open */
    GS_UI_HIT_NOTE,        /* the mark opening the record behind a line */
    GS_UI_HIT_CHAT_EARLIER,/* the line loading the older page of history */
    GS_UI_HIT_CHAT_NEWER,  /* the line going back to the newer page */
    GS_UI_HIT_RECORD_SHUT  /* the cross closing that record */
} gs_ui_hit_t;

/* Which screen the window is showing. Home is a bare canvas carrying
 * the settings gear, and the workspace holds the bar and the panes. */
typedef enum {
    GS_UI_SCREEN_HOME = 0,
    GS_UI_SCREEN_WORKSPACE
} gs_ui_screen_t;

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
    int  disk_picked;          /* the user chose the disk this session */
    int  category_sel;         /* 0 is All models */
    int  model_scroll;
    int  library_scroll;

    int  chooser_open;
    int  hover_row;            /* the row under the pointer, -1 for none */

    /* What has been typed into the search field. Every keystroke filters
     * the left pane at once. */
    char search[48];

    /* What has been typed into the prompt box on the home screen. */
    char prompt[1024];
    int  cursor_on;            /* the caret is showing this frame */

    /* Which models answer the next prompt, and the box that picks them.
     * The choice is by position in the library, kept for the session and
     * written to the database by name. */
    unsigned char model_chosen[GS_UI_CHOSEN_MAX];
    int  chosen_count;
    /* The conversation these prompts are being added to. Zero until the
     * first one is sent, since an empty conversation is worth nothing. */
    long long session_id;

    /* The record of the run that produced each line, opened by pressing
     * the note on a bubble. Empty for a line that has none. */
    int  record_open;          /* which line's record is showing, -1 for none */
    int  record_scroll;
    /* The answer behind each line, so its working out can be read from
     * the file when the record is opened rather than carried for every
     * line at once. Nought for a line with no answer behind it. */
    long long record_answer[GS_UI_HISTORY_MAX];
    /* The prompt behind each line, whichever side it is on. The record of
     * the run is read from the file by it when the mark is pressed, so no
     * line carries a copy. */
    long long record_turn[GS_UI_HISTORY_MAX];
    char record_by[GS_UI_HISTORY_MAX][96];
    /* The sheet as shown, put together when the record opens. One
     * buffer, since one record is open at a time and a model's working
     * out can run to several thousand characters. */
    char record_text[GS_UI_RECORD_FULL];

    /* What the panel shows above the box. Held here rather than read from
     * the file on every frame, since a frame is drawn many times a second
     * and the file changes only when something is sent. */
    char history[GS_UI_HISTORY_MAX][GS_UI_HISTORY_TEXT];
    /* One for what the person wrote, nought for what came back. */
    unsigned char history_mine[GS_UI_HISTORY_MAX];
    /* When each line was written, in seconds since the start of 1970, as
     * the store keeps it. Drawn beside the name on the bubble by the
     * clock of the machine this runs on. Nought for a line nobody
     * recorded a time against. */
    long long history_at[GS_UI_HISTORY_MAX];
    unsigned char history_kind[GS_UI_HISTORY_MAX];  /* one of gs_ui_line_t */
    int  history_count;
    /* How far the reader has scrolled back from the newest line, in
     * pixels, so nought keeps the newest against the box as answers
     * arrive. */
    int  history_back;
    /* Which page of the history is loaded, as how many of the newest
     * prompts it leaves out, and how many older ones lie beyond it. */
    int  history_skip;
    int  history_older;
    /* Changes whenever the lines change, so a measurement of them taken
     * earlier is known to be stale. Nought means never measured. */
    long long history_gen;

    /* Files chosen with the plus, waiting to go with the prompt. */
    char attached[GS_UI_ATTACH_MAX][GS_UI_ATTACH_PATH];
    int  attach_count;
    int  attach_open;          /* a file box is on screen right now */

    int  fleet_step;           /* 0 folded away, GS_UI_FLEET_STEPS open */
    int  fleet_dir;            /* 1 growing, -1 folding, 0 at rest */
    int  fleet_scroll;         /* first library row the box shows */

    /* Which list has its grip held, so the pointer moves that one until
     * the button is let go. Nought means nothing is held, which is what
     * a zeroed state says by itself. */
    int  drag_bar;             /* one of gs_ui_bar_t */

    /* The box that asks for the machine password, once per drive, so a
     * drive can be given a line in the system table of filesystems. What
     * is typed is NOT here. It lives in ui_secret.c alone, because this
     * state is handed whole to every drawing routine, and only the count
     * reaches the screen as dots. */
    int  secret_open;
    char secret_letter;
    int  secret_len;           /* how many characters, for the dots */
    int  secret_busy;          /* sudo is running, so nothing is taken */
    char secret_note[200];     /* what happened, safe to show */

    /* The confirm box that grows over the canvas before a model comes off
     * the disk. Step runs 0 to GS_UI_CONFIRM_STEPS, dir is 1 growing, -1
     * folding, 0 at rest, and row names the model being asked about. */
    int  confirm_step;
    int  confirm_dir;
    int  confirm_row;
    gs_ui_screen_t screen;
    int  settings_ready;       /* the file carries settings from before */
    char alert[160];           /* what is wrong, empty when nothing is */

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
/* The gear in the top right of the home screen, and the small circle
 * marking it when nothing has been configured yet. */
gs_ui_rect_t gs_ui_gear_rect(int width, int height);

/* The arrow at the right of the chooser row, which takes the workspace
 * back to the home screen. */
gs_ui_rect_t gs_ui_back_rect(int width, int height);

/* The chat panel and the parts of its composer. Every one comes back
 * empty at a size too small to hold it. */
gs_ui_rect_t gs_ui_chat_rect(int width, int height);
gs_ui_rect_t gs_ui_chat_composer_rect(int width, int height);
gs_ui_rect_t gs_ui_chat_send_rect(int width, int height);
gs_ui_rect_t gs_ui_chat_attach_rect(int width, int height);

/* The part of the panel that shows what has been asked, between the
 * heading and the box words are typed into. */
gs_ui_rect_t gs_ui_chat_history_rect(int width, int height);

/* The strip right of the history where its scroll bar sits, clear of
 * every bubble so a hand reaching for the bar never lands on a line. */
gs_ui_rect_t gs_ui_chat_bar_box(int width, int height);

/* How tall the whole loaded page stands, every bubble and gap counted,
 * and how far it can be scrolled back before the oldest line reaches the
 * top. */
int gs_ui_chat_content_height(const gs_ui_state_t *state, int width,
                              int height);
int gs_ui_chat_scroll_reach(const gs_ui_state_t *state, int width,
                            int height);

/* Moves the history by that many pixels, positive towards the newest.
 * Returns 1 when anything moved. */
int gs_ui_chat_scroll_by(gs_ui_state_t *state, int width, int height,
                         int pixels);

/* Reads the next wrapped line of text starting at *at, and moves *at past
 * it. One walk of the text reads every line, where asking for line n by
 * number walks every line before it again. Returns 0 when no lines are
 * left. */
int gs_ui_chat_next_line(const char *text, int room, size_t *at,
                         char *out, size_t cap);

/* How many wrapped lines at the top of the prompt the composer leaves
 * out, so the caret at the end stays in the last line the box shows. A
 * prompt ending in a break counts the empty line the caret stands on. */
int gs_ui_chat_composer_skip(const char *text, int room, int lines);

/* The bubble one line of the conversation sits in, with the name above it
 * counted in. Entry nought is the oldest kept. Comes back empty when the
 * entry does not exist or has been scrolled past the top.
 *
 * Both passes that draw the panel ask this, so the words and the shape
 * behind them cannot drift apart. */
/* The mark on a bubble that opens the record behind it. Comes back empty
 * for a line with no record, which is every line the person wrote. */
gs_ui_rect_t gs_ui_chat_note_rect(const gs_ui_state_t *state, int width,
                                  int height, int index);

/* The panel the record is shown in, covering the chat while it is up. */
gs_ui_rect_t gs_ui_record_rect(int width, int height);
gs_ui_rect_t gs_ui_record_shut_rect(int width, int height);

/* Whether a record is up on screen. A zeroed state carries nought in
 * record_open, and nought is a real line number, so the count is asked as
 * well. Every path that draws the record or answers a click on it asks
 * this one question, so the panel cannot be up for the mouse and down for
 * the eye. */
int gs_ui_record_showing(const gs_ui_state_t *state);

/* The furthest the record may be scrolled, counted in wrapped lines, so
 * the wheel stops with the last line on screen. The height of one line
 * comes from the window, which this file does not hold. Answers nought
 * when the whole record already fits. */
/* Opens the record behind one line. The working out the model wrote
 * before its answer is read from the file and put first, since that is
 * what a person opening the record wants to see, and how the run went
 * follows it. Shutting it empties the sheet. */
void gs_ui_record_open(gs_ui_state_t *state, int row);
void gs_ui_record_shut(gs_ui_state_t *state);

int gs_ui_record_scroll_limit(const gs_ui_state_t *state, int width,
                              int height, int line_height);

gs_ui_rect_t gs_ui_chat_bubble_rect(const gs_ui_state_t *state, int width,
                                    int height, int index);

/* How many lines one entry takes at the given width. */
int gs_ui_chat_bubble_lines(const gs_ui_state_t *state, int width,
                            int height, int index);

/* How many pixels of writing fit across a bubble. Letters differ in
 * width, so this is measured in pixels rather than counted in
 * characters. */
int gs_ui_chat_bubble_columns(int width, int height);
gs_ui_rect_t gs_ui_chat_fleet_rect(int width, int height);

/* The box that picks the models, at this step of its growth, and one of
 * its rows once it is open. The row index counts from the top of the
 * library rather than the top of the box. */
gs_ui_rect_t gs_ui_fleet_rect(const gs_ui_state_t *state, int width,
                              int height, int step);
gs_ui_rect_t gs_ui_fleet_row_rect(const gs_ui_state_t *state, int width,
                                  int height, int index);

/* How many rows the box shows at once, given how many models exist. */
int gs_ui_fleet_rows(const gs_ui_state_t *state);

/* Turns one model on or off, and counts what is chosen. */
void gs_ui_fleet_toggle(gs_ui_state_t *state, int index);

/* How many of the ticked models can be held at once, and what they would
 * take. Models run together, so their needs add up against memory and
 * graphics memory as one pool.
 *
 * Writes the total the ticked set would take into needed, and the pool it
 * is measured against into have, either of which may be NULL. Returns how
 * many of them fit. */
int gs_ui_fleet_capacity(const gs_ui_state_t *state,
                         const gs_detect_report_t *machine,
                         long long *needed, long long *have);
int  gs_ui_fleet_chosen(const gs_ui_state_t *state, int index);

/* The cross that shuts the box, in its top right corner. Comes back
 * empty while the box is closed or still growing, since a mark that moves
 * under the pointer is worse than no mark. */
gs_ui_rect_t gs_ui_fleet_close_rect(const gs_ui_state_t *state, int width,
                                    int height);

int gs_ui_fleet_visible(const gs_ui_state_t *state);
int gs_ui_fleet_animating(const gs_ui_state_t *state);
int gs_ui_fleet_advance(gs_ui_state_t *state);

/* Feeds one typed character into the prompt. Printable characters are
 * appended, 8 removes the last one, and anything else is ignored.
 * Returns 1 when the text changed. */
int gs_ui_chat_type(gs_ui_state_t *state, int ch);

/* Writes one line of a prompt as it will be shown, given how many pixels
 * of writing fit across the box. A line ends where the writer put a
 * break, or where the room runs out, whichever comes first, and a line
 * that would end inside a word pulls back to the space before it.
 *
 * Returns 1 when a line was written, 0 once there are no more. The break
 * itself is never written, since it is where the line ends rather than
 * something to draw. */
int gs_ui_chat_line(const char *text, int room, int index,
                    char *out, size_t cap);

/* The confirm box at this step of its growth, centred on the canvas, and
 * the two buttons inside it once it is fully open. */
gs_ui_rect_t gs_ui_confirm_rect(int width, int height, int step);
gs_ui_rect_t gs_ui_confirm_ok_rect(int width, int height);
gs_ui_rect_t gs_ui_confirm_no_rect(int width, int height);

/* Non zero while the box is on screen at all, and while it is moving. */
/* The password box. Nothing here ever hands the characters back, and
 * the only way they leave ui_secret.c is down a pipe into sudo. */
gs_ui_rect_t gs_ui_secret_rect(int width, int height);
gs_ui_rect_t gs_ui_secret_field_rect(int width, int height);
gs_ui_rect_t gs_ui_secret_ok_rect(int width, int height);
gs_ui_rect_t gs_ui_secret_no_rect(int width, int height);

int  gs_ui_secret_visible(const gs_ui_state_t *state);
void gs_ui_secret_open(gs_ui_state_t *state, char letter);
void gs_ui_secret_close(gs_ui_state_t *state);
void gs_ui_secret_forget(void);
int  gs_ui_secret_type(gs_ui_state_t *state, int ch);
int  gs_ui_secret_length(void);

/* Writes the line into the system table. Returns GS_OK when the table
 * now grants the right, and wipes what was typed either way. */
int  gs_ui_secret_apply(gs_ui_state_t *state);

int gs_ui_confirm_visible(const gs_ui_state_t *state);
int gs_ui_confirm_animating(const gs_ui_state_t *state);

/* Advances the box one frame. Returns 1 while there is more to draw. */
int gs_ui_confirm_advance(gs_ui_state_t *state);

/* The search field inside the left pane's header, sitting after the
 * MODELS THAT FIT label and stopping short of the count. */
gs_ui_rect_t gs_ui_search_rect(int width, int height);

/* The pointer shape that belongs over one control. A bar where words are
 * typed, a hand over anything that can be pressed, and the ordinary arrow
 * everywhere else. */
gs_window_cursor_t gs_ui_cursor_for(gs_ui_hit_t hit);

/* How wide one character is in the window's font. The layout is worked
 * out from a width and a height with no window in hand, so the figure is
 * told to this module once when the window opens and read back from
 * here. Six is what the X11 font measures, and it stands until a window
 * says otherwise. */
void gs_ui_set_glyph_width(int width);
int  gs_ui_glyph_width(void);

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

/* True when a typed character should close the window. Only Escape
 * does, since this application exists to take typed prompts and must
 * never treat an ordinary letter as a command. Escape carries 27 in
 * plain text, which every platform agrees on, so no platform's own key
 * numbering reaches this far. */
int gs_ui_key_closes(int ch);

/* True when a key pressed in the prompt box should send what is written
 * rather than add to it. Return on its own sends. Shift and return adds
 * a line break, since a question worth asking often needs more than one
 * line. mods carries GS_WINDOW_MOD_ bits. */
int gs_ui_key_sends(int ch, int mods);

/* How long the event loop may wait before it comes round again. planned
 * is what the animation running on screen asked for, in milliseconds,
 * with -1 meaning wait until something happens. paint_owed says a frame
 * is already due. A frame already due is drawn at once, so typing is
 * never held back by a clock belonging to something else. */
int gs_ui_wait_ms(int planned, int paint_owed);

int  gs_ui_paint(gs_window_t *window, const gs_ui_state_t *state);
void gs_ui_release(void);

extern const gs_module gs_ui_module;

#endif
