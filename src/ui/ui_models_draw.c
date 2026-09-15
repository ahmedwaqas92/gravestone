/* ui_models_draw.c
 *
 * Drawing for the chooser and the three panes. The lists themselves live
 * in ui_models.c, which this file only reads through the gs_ui_panes_
 * calls, so nothing here knows how a list is built.
 *
 * Pixels go into the buffer first and glyphs follow, since the X server
 * owns the font and draws text over the image after it lands.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "catalogue.h"
#include "detect.h"
#include "library.h"
#include "str.h"
#include "window.h"

#include <stdio.h>
#include <string.h>


int gs_ui_panes_visible(int w, int h)
{
    return w >= GS_UI_MARGIN * 2 + 240 &&
           h >= GS_UI_PANE_TOP + GS_UI_PANE_HEADER + GS_UI_ROW_HEIGHT;
}

/* One row of a pane, in window pixels. */
gs_ui_rect_t gs_ui_pane_row_rect(gs_ui_rect_t pane, int slot)
{
    gs_ui_rect_t r;

    r.x = pane.x + 2;
    r.y = pane.y + GS_UI_PANE_HEADER + slot * GS_UI_ROW_HEIGHT;
    r.w = pane.w - 4;
    r.h = GS_UI_ROW_HEIGHT;
    return r;
}

static void shade_rows(unsigned int *px, int w, int h, gs_ui_rect_t pane,
                       const gs_ui_state_t *state, gs_ui_hit_t mine,
                       int scroll, int count, int selected)
{
    int rows = gs_ui_pane_rows(pane);
    int slot;

    for (slot = 0; slot < rows; slot++) {
        int index = scroll + slot;

        if (index >= count)
            break;
        if (index == selected)
            gs_ui_px_rect(px, w, h, gs_ui_pane_row_rect(pane, slot), GS_UI_ROW_PICKED);
        else if (state->hover == mine && state->hover_row == index)
            gs_ui_px_rect(px, w, h, gs_ui_pane_row_rect(pane, slot), GS_UI_ROW_HOVER);
    }
}

static void draw_pane(unsigned int *px, int w, int h, gs_ui_rect_t pane)
{
    int x;

    if (pane.w < 40 || pane.h < GS_UI_PANE_HEADER)
        return;
    gs_ui_px_rounded(px, w, h, pane, 4, GS_UI_PANE_FILL, GS_UI_PANE_EDGE);

    /* A rule under the header, so the count reads apart from the rows. */
    for (x = pane.x + 6; x < pane.x + pane.w - 6; x++)
        gs_ui_px_rect(px, w, h,
                      (gs_ui_rect_t){x, pane.y + GS_UI_PANE_HEADER - 5, 1, 1},
                      GS_UI_PANE_EDGE);
}

/* The small triangle on the right of the chooser, pointing the way the
 * list will move. */
static void draw_arrow(unsigned int *px, int w, int h, int cx, int cy,
                       int open)
{
    int row;

    for (row = 0; row < 4; row++) {
        int span = open ? row : 3 - row;
        int y = open ? cy + 2 - row : cy - 2 + row;
        int dx;

        for (dx = -span; dx <= span; dx++)
            gs_ui_px_rect(px, w, h, (gs_ui_rect_t){cx + dx, y, 1, 1},
                          GS_UI_BTN_LABEL);
    }
}

/* The tag for one row of the model list, with the pill placed inside the
 * row. Both drawing passes call this, so the shape and the words land in
 * the same place.
 *
 * The size sits at the right edge of the words, the tag to the left of
 * it, and both stop before the mark that pulls a model. Returns nought when
 * the row has no tag, or when the row is too narrow to carry one without
 * covering the model name. */
int gs_ui_row_tag(gs_ui_rect_t pane, int slot, int index,
                   gs_ui_tag_t *out)
{
    const gs_catalogue_entry_t *entry = gs_ui_panes_model(index);
    const gs_detect_report_t *machine = gs_ui_panes_machine();
    gs_ui_rect_t row = gs_ui_pane_row_rect(pane, slot);
    int glyph = gs_ui_glyph_width();

    if (entry == NULL || glyph <= 0)
        return 0;
    if (!gs_ui_model_tag(entry->bytes, machine, out))
        return 0;

    /* A fixed column for the size, wide enough for the longest byte count
     * this ever prints. Measuring the actual string instead would move
     * every tag by a character or two, so the pills would sit ragged down
     * the list and a short size would let its row's tag drift right into
     * it. */
    /* The tag stops where the mark begins, so the two never touch. */
    row.w = gs_ui_row_mark_rect(row).x - row.x;
    out->box.x = row.x + row.w - GS_UI_PANE_PAD - GS_UI_SIZE_COLUMN * glyph
               - GS_UI_TAG_GAP - out->box.w;
    out->box.y = row.y + (row.h - out->box.h) / 2;

    /* A pane too narrow to hold the tag and still show part of a name
     * drops the tag rather than covering the name with it. */
    if (out->box.x < row.x + GS_UI_PANE_PAD + 4 * glyph)
        return 0;
    return 1;
}

void gs_ui_panes_compose(unsigned int *px, int w, int h,
                         const gs_ui_state_t *state)
{
    gs_ui_rect_t chooser, left, mid, right, drop;

    if (state == NULL || !gs_ui_panes_visible(w, h))
        return;

    chooser = gs_ui_chooser_rect(w, h);
    gs_ui_px_rounded(px, w, h, chooser, 4,
                     state->hover == GS_UI_HIT_CHOOSER ? GS_UI_BTN_FACE_HOVER
                                                       : GS_UI_BTN_FACE,
                     GS_UI_BTN_EDGE);
    draw_arrow(px, w, h, chooser.x + chooser.w - 16,
               chooser.y + chooser.h / 2, state->chooser_open);

    /* The way back to the home screen, an arrow pointing left. */
    {
        gs_ui_rect_t back = gs_ui_back_rect(w, h);

        if (back.w > 0) {
            int cx = back.x + back.w / 2 + 2;
            int cy = back.y + back.h / 2;
            unsigned int ink = state->hover == GS_UI_HIT_BACK
                                   ? GS_UI_BTN_LABEL : GS_UI_ROW_DIM;
            int step;

            gs_ui_px_rounded(px, w, h, back, 4,
                             state->hover == GS_UI_HIT_BACK
                                 ? GS_UI_BTN_FACE_HOVER : GS_UI_BTN_FACE,
                             GS_UI_BTN_EDGE);
            for (step = 0; step < 6; step++) {
                int reach = step < 3 ? step : 5 - step;

                gs_ui_px_rect(px, w, h,
                              (gs_ui_rect_t){cx - 5 + step, cy - reach,
                                             1, reach * 2 + 1}, ink);
            }
            gs_ui_px_rect(px, w, h, (gs_ui_rect_t){cx - 1, cy - 1, 6, 3},
                          ink);
        }
    }

    left = gs_ui_left_rect(w, h);
    mid = gs_ui_mid_rect(w, h);
    right = gs_ui_right_rect(w, h);

    draw_pane(px, w, h, left);
    draw_pane(px, w, h, mid);
    draw_pane(px, w, h, right);

    /* The search field, a sunken box in the left pane's header. */
    {
        gs_ui_rect_t box = gs_ui_search_rect(w, h);

        if (box.w > 0)
            gs_ui_px_rounded(px, w, h, box, 4, GS_UI_BTN_FACE_DOWN,
                             GS_UI_PANE_EDGE);
    }

    shade_rows(px, w, h, left, state, GS_UI_HIT_LEFT_ROW, state->model_scroll,
               state->model_count, state->model_sel);
    shade_rows(px, w, h, mid, state, GS_UI_HIT_MID_ROW, 0,
               state->disk_count, state->disk_sel);
    shade_rows(px, w, h, right, state, GS_UI_HIT_RIGHT_ROW,
               state->library_scroll, state->library_count, -1);

    /* A bar down the right of each pane, so a reader twenty rows into two
     * thousand can tell that from twenty rows into thirty. */
    {
        static const struct { int which; } order[3] = { {0}, {1}, {2} };
        int k;

        for (k = 0; k < 3; k++) {
            gs_ui_rect_t pane;
            gs_ui_rect_t list;
            gs_ui_rect_t rail, grip;
            int total, scroll;

            if (order[k].which == 0) {
                pane = left;
                total = state->model_count;
                scroll = state->model_scroll;
            } else if (order[k].which == 1) {
                pane = mid;
                total = state->disk_count;
                scroll = 0;
            } else {
                pane = right;
                total = state->library_count;
                scroll = state->library_scroll;
            }

            /* The bar runs beside the rows, clear of the header above
             * them. */
            list = pane;
            list.y += GS_UI_PANE_HEADER;
            list.h -= GS_UI_PANE_HEADER;
            if (!gs_ui_scrollbar(list, total, gs_ui_pane_rows(pane), scroll,
                                 &rail, &grip))
                continue;
            gs_ui_px_rounded(px, w, h, rail, 2, GS_UI_SCROLL_RAIL,
                             GS_UI_SCROLL_RAIL);
            gs_ui_px_rounded(px, w, h, grip, 2, GS_UI_SCROLL_GRIP,
                             GS_UI_SCROLL_GRIP);
        }
    }

    /* The pill behind each tag, filled here and lettered in the pass that
     * has a window to draw glyphs through. */
    {
        int rows = gs_ui_pane_rows(left);
        int slot;

        for (slot = 0; slot < rows; slot++) {
            gs_ui_tag_t tag;

            if (gs_ui_row_tag(left, slot, state->model_scroll + slot, &tag))
                gs_ui_px_rounded(px, w, h, tag.box, 3, tag.face, tag.face);
        }
    }

    /* The open list covers the panes, so it goes on last. It holds the
     * categories, which fit without scrolling. */
    if (state->chooser_open) {
        int slot;

        drop = gs_ui_drop_rect(w, h, state->category_count);
        gs_ui_px_rounded(px, w, h, drop, 4, GS_UI_BTN_FACE, GS_UI_BTN_EDGE);

        for (slot = 0; slot < state->category_count; slot++) {
            gs_ui_rect_t r;

            r.x = drop.x + 2;
            r.y = drop.y + 4 + slot * GS_UI_ROW_HEIGHT;
            r.w = drop.w - 4;
            r.h = GS_UI_ROW_HEIGHT;
            if (r.y + r.h > drop.y + drop.h)
                break;
            if (slot == state->category_sel)
                gs_ui_px_rect(px, w, h, r, GS_UI_ROW_PICKED);
            else if (state->hover == GS_UI_HIT_DROP_ROW &&
                     state->hover_row == slot)
                gs_ui_px_rect(px, w, h, r, GS_UI_ROW_HOVER);
        }
    }
}
