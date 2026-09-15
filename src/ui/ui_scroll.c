/* ui_scroll.c
 *
 * The bar down the right of a list, saying how far through it the reader
 * has come and how much is left.
 *
 * A list longer than its box shows nothing about its own size, so a
 * person twenty rows into two thousand cannot tell that from twenty rows
 * into thirty. The bar answers both at a glance.
 *
 * Every list in the interface asks this one place for its bar, so the
 * pass that fills the shape and the pass that answers a click cannot
 * place it differently.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"

#include <string.h>

int gs_ui_scrollbar(gs_ui_rect_t box, int total, int visible, int scroll,
                    gs_ui_rect_t *track, gs_ui_rect_t *thumb)
{
    gs_ui_rect_t rail;
    gs_ui_rect_t grip;
    int span;
    int reach;

    if (track != NULL)
        memset(track, 0, sizeof *track);
    if (thumb != NULL)
        memset(thumb, 0, sizeof *thumb);

    /* A list that already fits carries no bar, since a bar filling its
     * whole track tells a reader nothing. */
    if (total <= 0 || visible <= 0 || total <= visible)
        return 0;
    if (box.w < GS_UI_SCROLL_WIDTH + 8 || box.h < GS_UI_SCROLL_MIN_THUMB + 8)
        return 0;

    rail.w = GS_UI_SCROLL_WIDTH;
    rail.x = box.x + box.w - GS_UI_SCROLL_WIDTH - GS_UI_SCROLL_INSET;
    rail.y = box.y + GS_UI_SCROLL_INSET;
    rail.h = box.h - 2 * GS_UI_SCROLL_INSET;
    if (rail.h <= 0)
        return 0;

    /* The grip is as long a share of the rail as the visible rows are of
     * the whole list, held to a length a person can still see and take
     * hold of on a list of two thousand. */
    grip.w = rail.w;
    grip.x = rail.x;
    grip.h = rail.h * visible / total;
    if (grip.h < GS_UI_SCROLL_MIN_THUMB)
        grip.h = GS_UI_SCROLL_MIN_THUMB;
    if (grip.h > rail.h)
        grip.h = rail.h;

    /* Where it sits, from the first row shown against the last row that
     * can be first. */
    reach = total - visible;
    if (scroll < 0)
        scroll = 0;
    if (scroll > reach)
        scroll = reach;
    span = rail.h - grip.h;
    grip.y = rail.y + (reach > 0 ? span * scroll / reach : 0);

    if (track != NULL)
        *track = rail;
    if (thumb != NULL)
        *thumb = grip;
    return 1;
}

int gs_ui_scroll_from(gs_ui_rect_t box, int total, int visible, int y)
{
    gs_ui_rect_t rail;
    gs_ui_rect_t grip;
    int span;
    int reach;
    int at;

    if (!gs_ui_scrollbar(box, total, visible, 0, &rail, &grip))
        return 0;

    /* The point taken hold of sits at the middle of the grip, so a press
     * halfway down the rail lands halfway down the list. */
    span = rail.h - grip.h;
    reach = total - visible;
    if (span <= 0 || reach <= 0)
        return 0;

    at = y - rail.y - grip.h / 2;
    if (at < 0)
        at = 0;
    if (at > span)
        at = span;
    return at * reach / span;
}

int gs_ui_scroll_hit(const gs_ui_state_t *state, int width, int height,
                     int x, int y, int *row)
{
    int k;

    if (state == NULL)
        return 0;

    /* The three panes in the order they sit across the window. The row
     * that a press would put at the top comes back as the row, so the
     * caller moves the list without working the arithmetic again. */
    for (k = 0; k < 3; k++) {
        gs_ui_rect_t pane;
        gs_ui_rect_t list;
        gs_ui_rect_t rail;
        int total;
        int seats;

        if (k == 0) {
            pane = gs_ui_left_rect(width, height);
            total = state->model_count;
        } else if (k == 1) {
            pane = gs_ui_mid_rect(width, height);
            total = state->disk_count;
        } else {
            pane = gs_ui_right_rect(width, height);
            total = state->library_count;
        }

        seats = gs_ui_pane_rows(pane);
        list = pane;
        list.y += GS_UI_PANE_HEADER;
        list.h -= GS_UI_PANE_HEADER;

        if (!gs_ui_scrollbar(list, total, seats, 0, &rail, NULL))
            continue;
        if (!gs_ui_rect_contains(rail, x, y))
            continue;
        if (row != NULL)
            *row = gs_ui_scroll_from(list, total, seats, y);
        return 1;
    }
    return 0;
}

gs_ui_rect_t gs_ui_row_mark_rect(gs_ui_rect_t row)
{
    gs_ui_rect_t r = row;
    int bar = GS_UI_SCROLL_WIDTH + 2 * GS_UI_SCROLL_INSET;

    /* Right to left: the bar against the edge, then a gap wide enough
     * that a slip of the hand lands on neither, then the mark. */
    r.w = GS_UI_ROW_MARK_W;
    r.x = row.x + row.w - bar - GS_UI_ROW_MARK_GAP - GS_UI_ROW_MARK_W;
    if (r.x < row.x) {
        r.w = 0;
        r.x = row.x;
    }
    return r;
}

/* Where one list keeps its bar, and how long it is. Every list in the
 * interface is described here, so taking hold of a grip and dragging it
 * ask the same question rather than each panel answering for itself. */
static int bar_of(const gs_ui_state_t *state, int which, int width,
                  int height, gs_ui_rect_t *box, int *total, int *visible,
                  int **scroll)
{
    static int ignored;

    switch (which) {
    case GS_UI_BAR_MODELS: {
        gs_ui_rect_t pane = gs_ui_left_rect(width, height);

        if (state->screen != GS_UI_SCREEN_WORKSPACE)
            return 0;
        *box = pane;
        box->y += GS_UI_PANE_HEADER;
        box->h -= GS_UI_PANE_HEADER;
        *total = state->model_count;
        *visible = gs_ui_pane_rows(pane);
        *scroll = (int *)&state->model_scroll;
        return 1;
    }
    case GS_UI_BAR_LIBRARY: {
        gs_ui_rect_t pane = gs_ui_right_rect(width, height);

        if (state->screen != GS_UI_SCREEN_WORKSPACE)
            return 0;
        *box = pane;
        box->y += GS_UI_PANE_HEADER;
        box->h -= GS_UI_PANE_HEADER;
        *total = state->library_count;
        *visible = gs_ui_pane_rows(pane);
        *scroll = (int *)&state->library_scroll;
        return 1;
    }
    case GS_UI_BAR_CHAT: {
        gs_ui_rect_t seen = gs_ui_chat_history_rect(width, height);

        /* The box of models and the record both cover the history, so its
         * bar is out of reach while either is up. */
        if (state->screen != GS_UI_SCREEN_HOME ||
            gs_ui_fleet_visible(state) || gs_ui_record_showing(state))
            return 0;
        *box = gs_ui_chat_bar_box(width, height);
        if (box->w <= 0 || seen.h <= 0)
            return 0;
        /* Measured in pixels rather than rows, since a bubble can be any
         * height. The position is carried from the newest end, so the
         * caller turns it round rather than writing through a pointer. */
        *total = gs_ui_chat_content_height(state, width, height);
        *visible = seen.h;
        *scroll = &ignored;
        return 1;
    }
    case GS_UI_BAR_FLEET: {
        if (!gs_ui_fleet_visible(state) ||
            state->fleet_step != GS_UI_FLEET_STEPS)
            return 0;
        *box = gs_ui_fleet_rect(state, width, height, state->fleet_step);
        if (box->w <= 0)
            return 0;
        box->y += GS_UI_FLEET_HEAD;
        box->h -= GS_UI_FLEET_HEAD + GS_UI_FLEET_FOOT;
        *total = state->library_count;
        *visible = gs_ui_fleet_rows(state);
        *scroll = (int *)&state->fleet_scroll;
        return 1;
    }
    case GS_UI_BAR_RECORD: {
        int rows;

        if (!gs_ui_record_showing(state))
            return 0;
        *box = gs_ui_record_rect(width, height);
        if (box->w <= 0)
            return 0;
        rows = (box->h - 40) / GS_UI_RECORD_LINE;
        if (rows <= 0)
            return 0;
        *total = rows + gs_ui_record_scroll_limit(state, width, height,
                                                  GS_UI_RECORD_LINE);
        *visible = rows;
        *scroll = (int *)&state->record_scroll;
        return 1;
    }
    default:
        *scroll = &ignored;
        return 0;
    }
}

/* Puts a list where a press at y on its rail says it should be. Every list
 * counts from its first row, and the history counts back from its newest
 * line, so its position is turned round on the way in. */
static void place(gs_ui_state_t *state, int which, gs_ui_rect_t box,
                  int total, int visible, int *scroll, int y)
{
    int want = gs_ui_scroll_from(box, total, visible, y);

    if (which == GS_UI_BAR_CHAT)
        state->history_back = (total - visible) - want;
    else
        *scroll = want;
}

int gs_ui_bar_grab(gs_ui_state_t *state, int width, int height,
                   int x, int y)
{
    int which;

    if (state == NULL)
        return 0;
    state->drag_bar = GS_UI_BAR_NONE;

    /* The panels are asked in the order they sit above one another, so a
     * bar under an open panel is never taken by mistake. */
    for (which = GS_UI_BAR_RECORD; which >= GS_UI_BAR_MODELS; which--) {
        gs_ui_rect_t box;
        gs_ui_rect_t rail;
        int total = 0;
        int visible = 0;
        int *scroll = NULL;

        if (!bar_of(state, which, width, height, &box, &total, &visible,
                    &scroll))
            continue;
        if (!gs_ui_scrollbar(box, total, visible, 0, &rail, NULL))
            continue;
        if (!gs_ui_rect_contains(rail, x, y))
            continue;

        state->drag_bar = which;
        place(state, which, box, total, visible, scroll, y);
        return 1;
    }
    return 0;
}

int gs_ui_bar_drag(gs_ui_state_t *state, int width, int height, int y)
{
    gs_ui_rect_t box;
    int total = 0;
    int visible = 0;
    int *scroll = NULL;
    int want;

    if (state == NULL || state->drag_bar == GS_UI_BAR_NONE)
        return 0;
    if (!bar_of(state, state->drag_bar, width, height, &box, &total,
                &visible, &scroll)) {
        state->drag_bar = GS_UI_BAR_NONE;
        return 0;
    }

    /* The pointer is followed wherever it goes, including outside the
     * rail, since a hand that strays sideways while dragging still means
     * to be dragging. */
    want = gs_ui_scroll_from(box, total, visible, y);
    if (state->drag_bar == GS_UI_BAR_CHAT) {
        int back = (total - visible) - want;

        if (back == state->history_back)
            return 0;
        state->history_back = back;
        return 1;
    }
    if (want == *scroll)
        return 0;
    *scroll = want;
    return 1;
}

void gs_ui_bar_drop(gs_ui_state_t *state)
{
    if (state != NULL)
        state->drag_bar = GS_UI_BAR_NONE;
}
