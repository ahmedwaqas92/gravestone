/* ui_models_text.c
 *
 * The words on the workspace panes. Glyphs go through the window rather
 * than into the pixel buffer, so they are drawn in a pass of their own
 * after the shapes are down.
 *
 * The shapes, and where a tag sits inside its row, are worked out in
 * ui_models_draw.c. Both passes ask that file for the same placement, so
 * a pill and the words inside it cannot land apart.
 *
 * Split from that file because it went over the length one file is
 * allowed. The two share through ui_internal.h.
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

int gs_ui_model_tag_at(int width, int height, int slot, int index,
                       gs_ui_tag_t *out)
{
    return gs_ui_row_tag(gs_ui_left_rect(width, height), slot, index, out);
}

/* Where the byte count for a row is written, which the tag must stay
 * clear of. Both are worked out from the same column width, so a long
 * name can never push one into the other. */
gs_ui_rect_t gs_ui_model_size_rect(int width, int height, int slot)
{
    gs_ui_rect_t row = gs_ui_pane_row_rect(gs_ui_left_rect(width, height), slot);
    int glyph = gs_ui_glyph_width();

    row.w = gs_ui_row_mark_rect(row).x - row.x;
    row.x = row.x + row.w - GS_UI_PANE_PAD - GS_UI_SIZE_COLUMN * glyph;
    row.w = GS_UI_SIZE_COLUMN * glyph;
    return row;
}

static int baseline(gs_ui_rect_t r, gs_window_t *win)
{
    return r.y + (r.h + gs_window_font_ascent(win)) / 2 - 1;
}

/* Draws a row as a name on the left and a short value on the right, with
 * the name cut back to whatever width the value leaves. */
static void draw_row(gs_window_t *win, gs_ui_rect_t r, const char *name,
                     const char *value, unsigned int name_colour,
                     int reserve)
{
    char cut[192];
    int y = baseline(r, win);
    int value_w = value != NULL ? gs_window_text_width(win, value) : 0;
    int room = r.w - 2 * GS_UI_PANE_PAD - value_w - (value_w > 0 ? 10 : 0)
             - reserve;

    fit_text(win, name, room, cut, sizeof cut);
    if (cut[0] != '\0')
        gs_window_text(win, r.x + GS_UI_PANE_PAD, y, cut, name_colour);
    if (value_w > 0 && value_w < r.w - 2 * GS_UI_PANE_PAD)
        gs_window_text(win, r.x + r.w - GS_UI_PANE_PAD - value_w, y, value,
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
    draw_row(win, head, text, tally, GS_UI_HEADER_TEXT, 0);
}

static void model_line(const gs_catalogue_entry_t *entry, char *out,
                       size_t cap)
{
    /* The name Ollama pulls by, which already carries the size and the
     * quantisation, as in zephyr:7b-alpha-q3_K_M. The repository on its
     * own collapses a dozen variants onto one line, so a list of them
     * reads as the same row printed over and over. */
    if (entry->file[0] != '\0')
        gs_str_copy(out, cap, entry->file);
    else
        gs_str_copy(out, cap, short_name(entry->repository));
}

void gs_ui_panes_labels(gs_window_t *win, const gs_ui_state_t *state)
{
    int w, h, rows, slot;
    gs_ui_rect_t chooser, left, mid, right, r, full, mark;
    char line[192], size[64];

    if (win == NULL || state == NULL)
        return;
    w = gs_window_width(win);
    h = gs_window_height(win);
    if (!gs_ui_panes_visible(w, h))
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
    draw_row(win, r, line, size, GS_UI_BTN_LABEL, 0);

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
        if (gs_ui_text_covered(state, w, h, gs_ui_pane_row_rect(left, slot)))
            continue;
        model_line(entry, line, sizeof line);
        /* Where the model could run, beside its size. A person choosing
         * one to fetch wants to know whether it would sit on the card,
         * on the processor, or in either, before spending the download.
         * The machine is held between repaints rather than read here. */
        gs_str_bytes(entry->bytes, size, sizeof size);
        full = gs_ui_pane_row_rect(left, slot);
        mark = gs_ui_row_mark_rect(full);
        r = full;
        r.w = mark.x - full.x;      /* the words stop before the mark */

        /* The tag sits between the name and the size. The row keeps its
         * width so the size stays pinned to the right edge, and the tag
         * is passed as space already spoken for, which shortens the name
         * alone. */
        {
            gs_ui_tag_t tag;
            int taken = 0;

            if (gs_ui_row_tag(left, slot, index, &tag)) {
                gs_window_text(win, tag.box.x + GS_UI_TAG_PAD,
                               baseline(tag.box, win), tag.text, tag.ink);
                taken = tag.box.w + GS_UI_TAG_GAP;
            }
            draw_row(win, r, line, size,
                     index == state->model_sel ? GS_UI_BTN_LABEL
                                               : GS_UI_ROW_TEXT, taken);
        }
        gs_window_text(win, mark.x + 4, baseline(r, win), "+",
                       state->hover == GS_UI_HIT_LEFT_ADD &&
                       state->hover_row == index ? 0x0060C080u
                                                 : GS_UI_ROW_DIM);
    }

    rows = gs_ui_pane_rows(mid);
    for (slot = 0; slot < rows; slot++) {
        const gs_detect_disk_t *disk = gs_ui_panes_disk(slot);

        if (disk == NULL)
            break;
        if (gs_ui_text_covered(state, w, h, gs_ui_pane_row_rect(mid, slot)))
            continue;
        gs_str_bytes(disk->free_bytes, size, sizeof size);
        draw_row(win, gs_ui_pane_row_rect(mid, slot), disk->mount, size,
                 slot == state->disk_sel ? GS_UI_BTN_LABEL : GS_UI_ROW_TEXT,
                 0);
    }

    rows = gs_ui_pane_rows(right);
    for (slot = 0; slot < rows; slot++) {
        int index = state->library_scroll + slot;
        const gs_library_entry_t *entry = gs_library_at(index);

        if (entry == NULL)
            break;
        if (gs_ui_text_covered(state, w, h, gs_ui_pane_row_rect(right, slot)))
            continue;
        gs_str_bytes(entry->bytes, size, sizeof size);
        full = gs_ui_pane_row_rect(right, slot);
        mark = gs_ui_row_mark_rect(full);
        r = full;
        r.w = mark.x - full.x;      /* the words stop before the mark */
        draw_row(win, r, entry->name, size, GS_UI_ROW_TEXT, 0);
        gs_window_text(win, mark.x + 4, baseline(r, win), "x",
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
                                                 : GS_UI_ROW_TEXT, 0);
        }
    }
}
