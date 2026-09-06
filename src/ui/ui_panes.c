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

gs_ui_rect_t gs_ui_chooser_rect(int width, int height)
{
    gs_ui_rect_t r;

    r.x = GS_UI_MARGIN;
    r.y = GS_UI_BAR_HEIGHT + GS_UI_CHOOSER_TOP;
    r.w = width - 2 * GS_UI_MARGIN;
    r.h = GS_UI_CHOOSER_HEIGHT;
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

gs_ui_rect_t gs_ui_search_rect(int width, int height)
{
    gs_ui_rect_t pane = gs_ui_left_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};
    int label_w = 16 * 6 + 8;      /* MODELS THAT FIT in the 6x13 font */
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
int gs_ui_text_covered(const gs_ui_state_t *state, int width, int height,
                       gs_ui_rect_t r)
{
    gs_ui_rect_t drop;

    if (state == NULL || !state->chooser_open)
        return 0;
    drop = gs_ui_drop_rect(width, height, state->category_count);
    if (drop.w <= 0 || drop.h <= 0 || r.w <= 0 || r.h <= 0)
        return 0;
    return r.x < drop.x + drop.w && drop.x < r.x + r.w &&
           r.y < drop.y + drop.h && drop.y < r.y + r.h;
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
            index = (y - r.y - 4) / GS_UI_ROW_HEIGHT;

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

    if (gs_ui_rect_contains(gs_ui_chooser_rect(width, height), x, y))
        return GS_UI_HIT_CHOOSER;

    r = gs_ui_left_rect(width, height);
    if (gs_ui_rect_contains(r, x, y)) {
        index = row_under(r, y, state->model_scroll, state->model_count);
        if (index < 0)
            return GS_UI_HIT_NONE;
        if (row != NULL)
            *row = index;
        /* The last stretch of the row is the small + that pulls it. */
        if (x >= r.x + r.w - 20)
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
        /* The last stretch of the row is the small x that removes it. */
        if (x >= r.x + r.w - 20)
            return GS_UI_HIT_RIGHT_REMOVE;
        return GS_UI_HIT_RIGHT_ROW;
    }

    return GS_UI_HIT_NONE;
}
