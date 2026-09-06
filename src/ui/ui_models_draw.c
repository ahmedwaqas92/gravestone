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

#define PANE_PAD 10

static int panes_visible(int w, int h)
{
    return w >= GS_UI_MARGIN * 2 + 240 &&
           h >= GS_UI_PANE_TOP + GS_UI_PANE_HEADER + GS_UI_ROW_HEIGHT;
}

/* One row of a pane, in window pixels. */
static gs_ui_rect_t row_rect(gs_ui_rect_t pane, int slot)
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
            gs_ui_px_rect(px, w, h, row_rect(pane, slot), GS_UI_ROW_PICKED);
        else if (state->hover == mine && state->hover_row == index)
            gs_ui_px_rect(px, w, h, row_rect(pane, slot), GS_UI_ROW_HOVER);
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

void gs_ui_panes_compose(unsigned int *px, int w, int h,
                         const gs_ui_state_t *state)
{
    gs_ui_rect_t chooser, left, mid, right, drop;

    if (state == NULL || !panes_visible(w, h))
        return;

    chooser = gs_ui_chooser_rect(w, h);
    gs_ui_px_rounded(px, w, h, chooser, 4,
                     state->hover == GS_UI_HIT_CHOOSER ? GS_UI_BTN_FACE_HOVER
                                                       : GS_UI_BTN_FACE,
                     GS_UI_BTN_EDGE);
    draw_arrow(px, w, h, chooser.x + chooser.w - 16,
               chooser.y + chooser.h / 2, state->chooser_open);

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

/* ---- text ---- */

/* Copies as much of in as fits inside max pixels, marking a cut with two
 * dots. The server owns the font, so the width has to be asked for rather
 * than worked out from the character count. */
static void fit_text(gs_window_t *win, const char *in, int max, char *out,
                     size_t cap)
{
    size_t n;

    out[0] = '\0';
    if (in == NULL || max <= 0 || cap < 4)
        return;

    gs_str_copy(out, cap, in);
    if (gs_window_text_width(win, out) <= max)
        return;

    /* Take one character off the end and stand two dots where the cut was,
     * until what is left measures small enough. The characters that the
     * dots covered are put back before the next character comes off, so
     * the string is always the original shortened rather than a string
     * that has had dots eaten into it. */
    for (n = strlen(out); n > 2; n--) {
        out[n - 1] = '\0';
        out[n - 3] = '.';
        out[n - 2] = '.';
        if (gs_window_text_width(win, out) <= max)
            return;
        out[n - 3] = in[n - 3];
        out[n - 2] = in[n - 2];
    }
    out[0] = '\0';
}

/* A row's repository reads library/<model>, and the file column already
 * carries <model>:<tag>, so the part before the slash buys nothing. */
static const char *short_name(const char *repository)
{
    const char *slash = strrchr(repository, '/');

    return slash != NULL ? slash + 1 : repository;
}

static int baseline(gs_ui_rect_t r, gs_window_t *win)
{
    return r.y + (r.h + gs_window_font_ascent(win)) / 2 - 1;
}

/* Draws a row as a name on the left and a short value on the right, with
 * the name cut back to whatever width the value leaves. */
static void draw_row(gs_window_t *win, gs_ui_rect_t r, const char *name,
                     const char *value, unsigned int name_colour)
{
    char cut[192];
    int y = baseline(r, win);
    int value_w = value != NULL ? gs_window_text_width(win, value) : 0;
    int room = r.w - 2 * PANE_PAD - value_w - (value_w > 0 ? 10 : 0);

    fit_text(win, name, room, cut, sizeof cut);
    if (cut[0] != '\0')
        gs_window_text(win, r.x + PANE_PAD, y, cut, name_colour);
    if (value_w > 0 && value_w < r.w - 2 * PANE_PAD)
        gs_window_text(win, r.x + r.w - PANE_PAD - value_w, y, value,
                       GS_UI_ROW_DIM);
}

static void draw_header(gs_window_t *win, const gs_ui_state_t *state,
                        gs_ui_rect_t pane, const char *text, int count)
{
    char tally[16];
    gs_ui_rect_t head = pane;

    head.h = GS_UI_PANE_HEADER - 4;
    if (gs_ui_text_covered(state, gs_window_width(win),
                           gs_window_height(win), head))
        return;
    snprintf(tally, sizeof tally, "%d", count);
    draw_row(win, head, text, tally, GS_UI_HEADER_TEXT);
}

static void model_line(const gs_catalogue_entry_t *entry, char *out,
                       size_t cap)
{
    if (entry->quantisation[0] != '\0')
        snprintf(out, cap, "%s  %s", short_name(entry->repository),
                 entry->quantisation);
    else
        gs_str_copy(out, cap, short_name(entry->repository));
}

void gs_ui_panes_labels(gs_window_t *win, const gs_ui_state_t *state)
{
    int w, h, rows, slot;
    gs_ui_rect_t chooser, left, mid, right, r;
    char line[192], size[32];

    if (win == NULL || state == NULL)
        return;
    w = gs_window_width(win);
    h = gs_window_height(win);
    if (!panes_visible(w, h))
        return;
    if (gs_ui_confirm_visible(state))
        return;

    /* The bar across the top names the category the left pane is showing
     * and how many models are in it. */
    chooser = gs_ui_chooser_rect(w, h);
    snprintf(line, sizeof line, "%s",
             gs_ui_panes_category_name(state->category_sel));
    snprintf(size, sizeof size, "%d", state->model_count);
    r = chooser;
    r.w -= 22;
    draw_row(win, r, line, size, GS_UI_BTN_LABEL);

    left = gs_ui_left_rect(w, h);
    mid = gs_ui_mid_rect(w, h);
    right = gs_ui_right_rect(w, h);

    draw_header(win, state, left, "MODELS THAT FIT", state->model_count);

    /* The search text sits inside its box, dim guidance when empty, and
     * cut back rather than allowed to spill over the count. */
    {
        gs_ui_rect_t box = gs_ui_search_rect(w, h);

        if (box.w > 0 && !gs_ui_text_covered(state, w, h, box)) {
            char cut[64];
            int have = state->search[0] != '\0';
            int y = baseline(box, win);

            fit_text(win, have ? state->search : "type to search",
                     box.w - 10, cut, sizeof cut);
            if (cut[0] != '\0')
                gs_window_text(win, box.x + 5, y, cut,
                               have ? GS_UI_ROW_TEXT : GS_UI_ROW_DIM);
        }
    }
    draw_header(win, state, mid, "DISKS", state->disk_count);
    draw_header(win, state, right, "ON THIS DISK", state->library_count);

    rows = gs_ui_pane_rows(left);
    for (slot = 0; slot < rows; slot++) {
        int index = state->model_scroll + slot;
        const gs_catalogue_entry_t *entry = gs_ui_panes_model(index);

        if (entry == NULL)
            break;
        if (gs_ui_text_covered(state, w, h, row_rect(left, slot)))
            continue;
        model_line(entry, line, sizeof line);
        gs_str_bytes(entry->bytes, size, sizeof size);
        r = row_rect(left, slot);
        r.w -= 18;                /* room for the + that pulls it */
        draw_row(win, r, line, size,
                 index == state->model_sel ? GS_UI_BTN_LABEL
                                           : GS_UI_ROW_TEXT);
        gs_window_text(win, left.x + left.w - 14, baseline(r, win), "+",
                       state->hover == GS_UI_HIT_LEFT_ADD &&
                       state->hover_row == index ? 0x0060C080u
                                                 : GS_UI_ROW_DIM);
    }

    rows = gs_ui_pane_rows(mid);
    for (slot = 0; slot < rows; slot++) {
        const gs_detect_disk_t *disk = gs_ui_panes_disk(slot);

        if (disk == NULL)
            break;
        if (gs_ui_text_covered(state, w, h, row_rect(mid, slot)))
            continue;
        gs_str_bytes(disk->free_bytes, size, sizeof size);
        draw_row(win, row_rect(mid, slot), disk->mount, size,
                 slot == state->disk_sel ? GS_UI_BTN_LABEL : GS_UI_ROW_TEXT);
    }

    rows = gs_ui_pane_rows(right);
    for (slot = 0; slot < rows; slot++) {
        int index = state->library_scroll + slot;
        const gs_library_entry_t *entry = gs_library_at(index);

        if (entry == NULL)
            break;
        if (gs_ui_text_covered(state, w, h, row_rect(right, slot)))
            continue;
        gs_str_bytes(entry->bytes, size, sizeof size);
        r = row_rect(right, slot);
        r.w -= 18;                /* room for the x that removes it */
        draw_row(win, r, entry->name, size, GS_UI_ROW_TEXT);
        gs_window_text(win, right.x + right.w - 14, baseline(r, win), "x",
                       state->hover == GS_UI_HIT_RIGHT_REMOVE &&
                       state->hover_row == index ? 0x00D07060u
                                                 : GS_UI_ROW_DIM);
    }

    /* The open list sits over the panes, so its rows are drawn last. */
    if (state->chooser_open) {
        gs_ui_rect_t drop = gs_ui_drop_rect(w, h, state->category_count);

        for (slot = 0; slot < state->category_count; slot++) {
            r.x = drop.x;
            r.y = drop.y + 4 + slot * GS_UI_ROW_HEIGHT;
            r.w = drop.w;
            r.h = GS_UI_ROW_HEIGHT;
            if (r.y + r.h > drop.y + drop.h)
                break;
            snprintf(size, sizeof size, "%d",
                     gs_ui_panes_category_total(slot));
            draw_row(win, r, gs_ui_panes_category_name(slot), size,
                     slot == state->category_sel ? GS_UI_BTN_LABEL
                                                 : GS_UI_ROW_TEXT);
        }
    }
}
