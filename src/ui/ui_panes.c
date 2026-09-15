/* ui_panes.c
 *
 * The chooser and the three panes under the bar, and the arithmetic that
 * says where each of them sits. Geometry lives apart from the drawing so
 * a test can ask where a control is without a screen.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"

#include <stddef.h>
#include <string.h>

/* A rectangle that will not fit is handed back empty rather than hanging
 * over the edge, so a caller can draw whatever it is given without first
 * checking the window size. */
static gs_ui_rect_t clamp(gs_ui_rect_t r, int width, int height)
{
    if (r.w < 0)
        r.w = 0;
    if (r.h < 0)
        r.h = 0;
    if (r.x < 0 || r.y < 0 || r.x + r.w > width || r.y + r.h > height) {
        r.w = 0;
        r.h = 0;
    }
    return r;
}

#define GS_UI_BACK_SIDE 26
#define GS_UI_BACK_GAP  10

gs_ui_rect_t gs_ui_chooser_rect(int width, int height)
{
    gs_ui_rect_t r;

    r.x = GS_UI_MARGIN;
    r.y = GS_UI_BAR_HEIGHT + GS_UI_CHOOSER_TOP;
    /* The back arrow sits at the end of this row, so the chooser stops
     * short of it rather than running underneath. */
    r.w = width - 2 * GS_UI_MARGIN - GS_UI_BACK_SIDE - GS_UI_BACK_GAP;
    r.h = GS_UI_CHOOSER_HEIGHT;
    return clamp(r, width, height);
}

gs_ui_rect_t gs_ui_back_rect(int width, int height)
{
    gs_ui_rect_t r;

    r.w = GS_UI_BACK_SIDE;
    r.h = GS_UI_BACK_SIDE;
    r.x = width - GS_UI_MARGIN - GS_UI_BACK_SIDE;
    r.y = GS_UI_BAR_HEIGHT + GS_UI_CHOOSER_TOP +
          (GS_UI_CHOOSER_HEIGHT - GS_UI_BACK_SIDE) / 2;
    return clamp(r, width, height);
}

/* The three panes share the width left after the margins and two gaps.
 * The middle one is narrowest, since a mount point is short next to a
 * model name. */
static void pane_columns(int width, int *lx, int *lw, int *mx, int *mw,
                         int *rx, int *rw)
{
    int usable = width - 2 * GS_UI_MARGIN - 2 * GS_UI_PANE_GAP;
    int left, mid, right;

    if (usable < 3) {
        *lx = *mx = *rx = GS_UI_MARGIN;
        *lw = *mw = *rw = 0;
        return;
    }

    left = usable * 42 / 100;
    mid = usable * 22 / 100;
    right = usable - left - mid;

    *lx = GS_UI_MARGIN;
    *lw = left;
    *mx = *lx + left + GS_UI_PANE_GAP;
    *mw = mid;
    *rx = *mx + mid + GS_UI_PANE_GAP;
    *rw = right;
}

static gs_ui_rect_t pane_at(int width, int height, int which)
{
    gs_ui_rect_t r;
    int lx, lw, mx, mw, rx, rw;

    pane_columns(width, &lx, &lw, &mx, &mw, &rx, &rw);

    r.y = GS_UI_PANE_TOP;
    r.h = height - GS_UI_PANE_TOP - GS_UI_MARGIN;
    if (r.h < 0)
        r.h = 0;

    if (which == 0)      { r.x = lx; r.w = lw; }
    else if (which == 1) { r.x = mx; r.w = mw; }
    else                 { r.x = rx; r.w = rw; }
    return clamp(r, width, height);
}

gs_ui_rect_t gs_ui_left_rect(int width, int height)
{
    return pane_at(width, height, 0);
}

gs_ui_rect_t gs_ui_mid_rect(int width, int height)
{
    return pane_at(width, height, 1);
}

gs_ui_rect_t gs_ui_right_rect(int width, int height)
{
    return pane_at(width, height, 2);
}

/* The open list hangs below the chooser, tall enough for the rows it has
 * and never taller than the window can hold. */
gs_ui_rect_t gs_ui_drop_rect(int width, int height, int rows)
{
    gs_ui_rect_t c = gs_ui_chooser_rect(width, height);
    gs_ui_rect_t r;
    int room;

    if (rows > GS_UI_DROP_ROWS)
        rows = GS_UI_DROP_ROWS;
    if (rows < 0)
        rows = 0;

    r.x = c.x;
    r.y = c.y + c.h;
    r.w = c.w;
    r.h = rows * GS_UI_ROW_HEIGHT + 8;

    room = height - r.y - GS_UI_MARGIN;
    if (r.h > room)
        r.h = room > 0 ? room : 0;
    return clamp(r, width, height);
}

int gs_ui_pane_rows(gs_ui_rect_t pane)
{
    int room = pane.h - GS_UI_PANE_HEADER - 6;

    if (room <= 0)
        return 0;
    return room / GS_UI_ROW_HEIGHT;
}

int gs_ui_rect_contains(gs_ui_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* Turns a point inside a pane into the index of the row it lands on,
 * counting from the top of the list rather than the top of the pane. */
static int row_under(gs_ui_rect_t pane, int y, int scroll, int count)
{
    int local = y - pane.y - GS_UI_PANE_HEADER;
    int index;

    if (local < 0)
        return -1;
    index = scroll + local / GS_UI_ROW_HEIGHT;
    if (index < 0 || index >= count)
        return -1;
    return index;
}

/* Rows are addressed by index in the list, so the slot on screen is the
 * index minus how far the list is scrolled. A slot outside the pane gives
 * an empty rectangle. */
static gs_ui_rect_t slot_rect(gs_ui_rect_t pane, int slot, int visible)
{
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (slot < 0 || slot >= visible)
        return r;
    r.x = pane.x + 2;
    r.y = pane.y + GS_UI_PANE_HEADER + slot * GS_UI_ROW_HEIGHT;
    r.w = pane.w - 4;
    r.h = GS_UI_ROW_HEIGHT;
    return r;
}

static int glyph_width = 6;

void gs_ui_set_glyph_width(int width)
{
    if (width > 0 && width < 64)
        glyph_width = width;
}

int gs_ui_glyph_width(void)
{
    return glyph_width;
}

gs_window_cursor_t gs_ui_cursor_for(gs_ui_hit_t hit)
{
    switch (hit) {
    /* Somewhere words go, so the bar that shows where they will land. */
    case GS_UI_HIT_CHAT_BOX:
        return GS_WINDOW_CURSOR_TEXT;

    /* Everything a press does something to. */
    case GS_UI_HIT_MOUNT:
    case GS_UI_HIT_SPECS:
    case GS_UI_HIT_CHOOSER:
    case GS_UI_HIT_DROP_ROW:
    case GS_UI_HIT_SCROLL:
    case GS_UI_HIT_LEFT_ROW:
    case GS_UI_HIT_MID_ROW:
    case GS_UI_HIT_RIGHT_ROW:
    case GS_UI_HIT_RIGHT_REMOVE:
    case GS_UI_HIT_LEFT_ADD:
    case GS_UI_HIT_CONFIRM_OK:
    case GS_UI_HIT_CONFIRM_NO:
    case GS_UI_HIT_SETTINGS:
    case GS_UI_HIT_BACK:
    case GS_UI_HIT_CHAT_SEND:
    case GS_UI_HIT_CHAT_ATTACH:
    case GS_UI_HIT_FLEET:
    case GS_UI_HIT_FLEET_OPTION:
    case GS_UI_HIT_FLEET_CLOSE:
    case GS_UI_HIT_NOTE:
    case GS_UI_HIT_RECORD_SHUT:
        return GS_WINDOW_CURSOR_HAND;

    /* Bare canvas, and the space outside an open box, where a press only
     * puts that box away. */
    case GS_UI_HIT_NONE:
    case GS_UI_HIT_FLEET_AWAY:
    default:
        return GS_WINDOW_CURSOR_ARROW;
    }
}

gs_ui_rect_t gs_ui_search_rect(int width, int height)
{
    gs_ui_rect_t pane = gs_ui_left_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};
    /* Sixteen characters clears "MODELS THAT FIT" and the space after
     * it, whatever the font measures. */
    int label_w = 16 * glyph_width + 8;
    int count_w = 44;              /* room for a five digit count */

    if (pane.w <= 0 || pane.h <= 0)
        return r;
    r.x = pane.x + label_w;
    r.y = pane.y + 3;
    r.w = pane.w - label_w - count_w;
    r.h = GS_UI_PANE_HEADER - 9;
    if (r.w < 30) {
        r.w = 0;
        r.h = 0;
    }
    return r;
}

int gs_ui_search_type(gs_ui_state_t *state, int ch)
{
    size_t n;

    if (state == NULL)
        return 0;
    n = strlen(state->search);

    if (ch == 8) {
        if (n == 0)
            return 0;
        state->search[n - 1] = '\0';
        return 1;
    }
    if (ch < 0x20 || ch > 0x7e)
        return 0;
    if (n + 1 >= sizeof state->search)
        return 0;
    state->search[n] = (char)ch;
    state->search[n + 1] = '\0';
    return 1;
}

/* Whether text at this rectangle would land under the open category
 * list. The pixels of the list are blitted before any text is drawn, so
 * a label painted afterwards would sit on top of the list rather than
 * under it, and the only way to keep the list in front is to not draw
 * what it covers. */
/* Whether two rectangles touch at all. */
static int overlaps(gs_ui_rect_t a, gs_ui_rect_t b)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0)
        return 0;
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

int gs_ui_text_covered(const gs_ui_state_t *state, int width, int height,
                       gs_ui_rect_t r)
{
    if (state == NULL || r.w <= 0 || r.h <= 0)
        return 0;

    /* Shapes are filled into the pixels and words are drawn through the
     * window afterwards, so a panel can never hide a word that was
     * already put on the glass. Anything falling under an open panel is
     * left undrawn instead. */
    if (state->chooser_open &&
        overlaps(r, gs_ui_drop_rect(width, height, state->category_count)))
        return 1;
    if (gs_ui_fleet_visible(state) &&
        overlaps(r, gs_ui_fleet_rect(state, width, height,
                                     state->fleet_step)))
        return 1;
    if (gs_ui_record_showing(state) &&
        overlaps(r, gs_ui_record_rect(width, height)))
        return 1;
    return 0;
}

gs_ui_rect_t gs_ui_hit_rect(const gs_ui_state_t *state, int width,
                            int height, gs_ui_hit_t hit, int row)
{
    static const gs_ui_state_t blank;
    gs_ui_rect_t none = {0, 0, 0, 0};
    gs_ui_rect_t pane;

    if (state == NULL)
        state = &blank;

    switch (hit) {
    case GS_UI_HIT_MOUNT:
        return gs_ui_mount_rect(width);
    case GS_UI_HIT_SPECS:
        return gs_ui_specs_rect(width);
    case GS_UI_HIT_CHOOSER:
        return gs_ui_chooser_rect(width, height);
    case GS_UI_HIT_DROP_ROW:
        return gs_ui_drop_rect(width, height, state->category_count);
    case GS_UI_HIT_LEFT_ROW:
    case GS_UI_HIT_LEFT_ADD:
        pane = gs_ui_left_rect(width, height);
        return slot_rect(pane, row - state->model_scroll,
                         gs_ui_pane_rows(pane));
    case GS_UI_HIT_MID_ROW:
        pane = gs_ui_mid_rect(width, height);
        return slot_rect(pane, row, gs_ui_pane_rows(pane));
    case GS_UI_HIT_RIGHT_ROW:
    case GS_UI_HIT_RIGHT_REMOVE:
        pane = gs_ui_right_rect(width, height);
        return slot_rect(pane, row - state->library_scroll,
                         gs_ui_pane_rows(pane));
    case GS_UI_HIT_SETTINGS:
        return gs_ui_gear_rect(width, height);
    case GS_UI_HIT_BACK:
        return gs_ui_back_rect(width, height);
    case GS_UI_HIT_CHAT_SEND:
        return gs_ui_chat_send_rect(width, height);
    case GS_UI_HIT_CHAT_ATTACH:
        return gs_ui_chat_attach_rect(width, height);
    case GS_UI_HIT_CHAT_BOX:
        return gs_ui_chat_composer_rect(width, height);
    case GS_UI_HIT_FLEET:
        return gs_ui_chat_fleet_rect(width, height);
    case GS_UI_HIT_FLEET_OPTION:
        return gs_ui_fleet_row_rect(state, width, height, row);
    case GS_UI_HIT_FLEET_CLOSE:
        return gs_ui_fleet_close_rect(state, width, height);
    case GS_UI_HIT_NOTE:
        return gs_ui_chat_note_rect(state, width, height, row);
    case GS_UI_HIT_CHAT_EARLIER:
    case GS_UI_HIT_CHAT_NEWER:
        return gs_ui_chat_bubble_rect(state, width, height, row);
    case GS_UI_HIT_RECORD_SHUT:
        return gs_ui_record_shut_rect(width, height);
    case GS_UI_HIT_CONFIRM_OK:
        return gs_ui_confirm_ok_rect(width, height);
    case GS_UI_HIT_CONFIRM_NO:
        return gs_ui_confirm_no_rect(width, height);
    default:
        return none;
    }
}

gs_ui_hit_t gs_ui_hit_test(const gs_ui_state_t *state, int width, int height,
                           int x, int y, int *row)
{
    static const gs_ui_state_t blank;
    gs_ui_rect_t r;
    int index;

    if (row != NULL)
        *row = -1;
    if (state == NULL)
        state = &blank;

    /* A point outside the window belongs to no control. */
    if (x < 0 || y < 0 || x >= width || y >= height)
        return GS_UI_HIT_NONE;

    /* The password box sits over everything, so nothing behind it can be
     * reached while it is open. A stray click lands on nothing rather
     * than on whatever happens to be underneath. */
    if (gs_ui_secret_visible(state)) {
        if (!state->secret_busy &&
            gs_ui_rect_contains(gs_ui_secret_ok_rect(width, height), x, y))
            return GS_UI_HIT_SECRET_OK;
        if (gs_ui_rect_contains(gs_ui_secret_no_rect(width, height), x, y))
            return GS_UI_HIT_SECRET_NO;
        return GS_UI_HIT_NONE;
    }

    /* The home screen carries the gear and the chat panel. */
    if (state->screen == GS_UI_SCREEN_HOME) {
        /* The open count box covers the panel under it. */
        /* The record covers the chat while it is open, so nothing under
         * it answers until it is shut. */
        if (gs_ui_record_showing(state)) {
            if (gs_ui_rect_contains(gs_ui_record_shut_rect(width, height),
                                    x, y))
                return GS_UI_HIT_RECORD_SHUT;
            if (gs_ui_rect_contains(gs_ui_record_rect(width, height), x, y))
                return GS_UI_HIT_NONE;
            return GS_UI_HIT_RECORD_SHUT;   /* anywhere outside shuts it */
        }

        /* The mark opening the record of how a line was arrived at, and
         * the lines that move between pages of the history. Both answer
         * with the box of models up, since pressing either shuts that box
         * first, but never through the box itself. The box sits on the
         * bottom of the history where the newest lines are, and a press
         * meant for one of its models used to land on a mark or a page
         * line hidden underneath. */
        for (index = 0; index < state->history_count &&
                        !(gs_ui_fleet_visible(state) &&
                          gs_ui_rect_contains(
                              gs_ui_fleet_rect(state, width, height,
                                               state->fleet_step), x, y));
             index++) {
            if (gs_ui_rect_contains(
                    gs_ui_chat_note_rect(state, width, height, index),
                    x, y)) {
                if (row != NULL)
                    *row = index;
                return GS_UI_HIT_NOTE;
            }
            if (state->history_kind[index] != GS_UI_LINE_SAID &&
                gs_ui_rect_contains(
                    gs_ui_chat_bubble_rect(state, width, height, index),
                    x, y) &&
                gs_ui_rect_contains(gs_ui_chat_history_rect(width, height),
                                    x, y)) {
                if (row != NULL)
                    *row = index;
                return state->history_kind[index] == GS_UI_LINE_EARLIER
                           ? GS_UI_HIT_CHAT_EARLIER : GS_UI_HIT_CHAT_NEWER;
            }
        }

        if (gs_ui_fleet_visible(state)) {
            if (state->fleet_step == GS_UI_FLEET_STEPS) {
                if (gs_ui_rect_contains(
                        gs_ui_fleet_close_rect(state, width, height), x, y))
                    return GS_UI_HIT_FLEET_CLOSE;
                for (index = 0; index < state->library_count &&
                                index < GS_UI_CHOSEN_MAX; index++)
                    if (gs_ui_rect_contains(
                            gs_ui_fleet_row_rect(state, width, height,
                                                 index), x, y)) {
                        if (row != NULL)
                            *row = index;
                        return GS_UI_HIT_FLEET_OPTION;
                    }
            }
            /* Inside the box but on none of its controls, so nothing
             * happens. Anywhere else shuts it, which is what a person
             * expects of a small box that opened over the work. */
            if (gs_ui_rect_contains(
                    gs_ui_fleet_rect(state, width, height,
                                     state->fleet_step), x, y))
                return GS_UI_HIT_NONE;
            return GS_UI_HIT_FLEET_AWAY;
        }
        if (gs_ui_rect_contains(gs_ui_gear_rect(width, height), x, y))
            return GS_UI_HIT_SETTINGS;
        if (gs_ui_rect_contains(gs_ui_chat_fleet_rect(width, height), x, y))
            return GS_UI_HIT_FLEET;
        if (gs_ui_rect_contains(gs_ui_chat_send_rect(width, height), x, y))
            return GS_UI_HIT_CHAT_SEND;
        if (gs_ui_rect_contains(gs_ui_chat_attach_rect(width, height), x, y))
            return GS_UI_HIT_CHAT_ATTACH;
        if (gs_ui_rect_contains(gs_ui_chat_composer_rect(width, height),
                                x, y))
            return GS_UI_HIT_CHAT_BOX;
        return GS_UI_HIT_NONE;
    }

    /* The confirm box blocks the whole canvas while it is up, and its two
     * buttons only answer once it has finished growing. */
    if (gs_ui_confirm_visible(state)) {
        if (state->confirm_busy)
            return GS_UI_HIT_NONE;
        if (state->confirm_step == GS_UI_CONFIRM_STEPS) {
            if (gs_ui_rect_contains(gs_ui_confirm_ok_rect(width, height),
                                    x, y))
                return GS_UI_HIT_CONFIRM_OK;
            if (gs_ui_rect_contains(gs_ui_confirm_no_rect(width, height),
                                    x, y))
                return GS_UI_HIT_CONFIRM_NO;
        }
        return GS_UI_HIT_NONE;
    }

    /* An open list sits over everything under it, so it is asked first.
     * It holds categories, which are few enough to never scroll. */
    if (state->chooser_open) {
        r = gs_ui_drop_rect(width, height, state->category_count);
        if (gs_ui_rect_contains(r, x, y)) {
            /* The list is cut to whatever the window leaves under the
             * chooser, so an entry the paint could not reach must not
             * answer a press either. */
            int seats = (r.h - 8) / GS_UI_ROW_HEIGHT;

            index = (y - r.y - 4) / GS_UI_ROW_HEIGHT;
            if (index >= seats)
                return GS_UI_HIT_NONE;

            if (index >= 0 && index < state->category_count) {
                if (row != NULL)
                    *row = index;
                return GS_UI_HIT_DROP_ROW;
            }
            return GS_UI_HIT_NONE;
        }
    }

    /* A control that is too wide for the window is not drawn, and a
     * control nobody can see must not answer a click either. */
    r = gs_ui_mount_rect(width);
    if (r.x + r.w < width - GS_UI_MARGIN && gs_ui_rect_contains(r, x, y))
        return GS_UI_HIT_MOUNT;

    /* The specs control only answers once something is stored, so a click
     * on a disabled control lands nowhere. */
    r = gs_ui_specs_rect(width);
    if (state->mounted && r.x + r.w < width - GS_UI_MARGIN &&
        gs_ui_rect_contains(r, x, y))
        return GS_UI_HIT_SPECS;

    if (gs_ui_rect_contains(gs_ui_back_rect(width, height), x, y))
        return GS_UI_HIT_BACK;

    if (gs_ui_rect_contains(gs_ui_chooser_rect(width, height), x, y))
        return GS_UI_HIT_CHOOSER;

    /* The bar down the right of a pane answers before the rows do, since
     * it sits over the last few pixels of every one of them. */
    if (gs_ui_scroll_hit(state, width, height, x, y, row))
        return GS_UI_HIT_SCROLL;

    r = gs_ui_left_rect(width, height);
    if (gs_ui_rect_contains(r, x, y)) {
        index = row_under(r, y, state->model_scroll, state->model_count);
        if (index < 0)
            return GS_UI_HIT_NONE;
        if (row != NULL)
            *row = index;
        /* The mark near the end of the row is the small + that pulls it,
         * sitting clear of the bar so a hand reaching for the bar and
         * missing starts no download. */
        if (gs_ui_rect_contains(gs_ui_row_mark_rect(r), x, y))
            return GS_UI_HIT_LEFT_ADD;
        return GS_UI_HIT_LEFT_ROW;
    }

    r = gs_ui_mid_rect(width, height);
    if (gs_ui_rect_contains(r, x, y)) {
        index = row_under(r, y, 0, state->disk_count);
        if (index < 0)
            return GS_UI_HIT_NONE;
        if (row != NULL)
            *row = index;
        return GS_UI_HIT_MID_ROW;
    }

    r = gs_ui_right_rect(width, height);
    if (gs_ui_rect_contains(r, x, y)) {
        index = row_under(r, y, state->library_scroll, state->library_count);
        if (index < 0)
            return GS_UI_HIT_NONE;
        if (row != NULL)
            *row = index;
        /* The mark near the end of the row is the small x that removes
         * it, sitting clear of the bar for the same reason. */
        if (gs_ui_rect_contains(gs_ui_row_mark_rect(r), x, y))
            return GS_UI_HIT_RIGHT_REMOVE;
        return GS_UI_HIT_RIGHT_ROW;
    }

    return GS_UI_HIT_NONE;
}
