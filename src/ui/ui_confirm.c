/* ui_confirm.c
 *
 * The box that grows over the canvas before a model comes off the disk,
 * in the same motion as the specs panel, and folds away when answered.
 * It lives inside the main window rather than in a window of its own, so
 * closing it can never leave a mirror behind on the desktop.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "catalogue.h"
#include "library.h"
#include "str.h"
#include "window.h"

#include <stdio.h>
#include <string.h>

int gs_ui_confirm_visible(const gs_ui_state_t *state)
{
    return state != NULL && (state->confirm_step > 0 ||
                             state->confirm_dir != 0);
}

int gs_ui_confirm_animating(const gs_ui_state_t *state)
{
    return state != NULL && state->confirm_dir != 0;
}

int gs_ui_confirm_advance(gs_ui_state_t *state)
{
    if (state == NULL || state->confirm_dir == 0)
        return 0;

    state->confirm_step += state->confirm_dir;
    if (state->confirm_step >= GS_UI_CONFIRM_STEPS) {
        state->confirm_step = GS_UI_CONFIRM_STEPS;
        state->confirm_dir = 0;
        return 0;
    }
    if (state->confirm_step <= 0) {
        state->confirm_step = 0;
        state->confirm_dir = 0;
        state->confirm_row = -1;
        return 0;
    }
    return 1;
}

gs_ui_rect_t gs_ui_confirm_rect(int width, int height, int step)
{
    gs_ui_rect_t r = {0, 0, 0, 0};
    int bw, bh;

    if (step <= 0)
        return r;
    if (step > GS_UI_CONFIRM_STEPS)
        step = GS_UI_CONFIRM_STEPS;

    bw = 60 + (GS_UI_CONFIRM_W - 60) * step / GS_UI_CONFIRM_STEPS;
    bh = 30 + (GS_UI_CONFIRM_H - 30) * step / GS_UI_CONFIRM_STEPS;
    if (bw > width - 8)
        bw = width - 8;
    if (bh > height - 8)
        bh = height - 8;
    if (bw <= 0 || bh <= 0)
        return r;

    r.x = (width - bw) / 2;
    r.y = (height - bh) / 2;
    r.w = bw;
    r.h = bh;
    return r;
}

gs_ui_rect_t gs_ui_confirm_ok_rect(int width, int height)
{
    gs_ui_rect_t box = gs_ui_confirm_rect(width, height,
                                          GS_UI_CONFIRM_STEPS);
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (box.w <= 0)
        return r;
    r.w = 92;
    r.h = 26;
    r.x = box.x + box.w - r.w - 14;
    r.y = box.y + box.h - r.h - 12;
    return r;
}

gs_ui_rect_t gs_ui_confirm_no_rect(int width, int height)
{
    gs_ui_rect_t ok = gs_ui_confirm_ok_rect(width, height);
    gs_ui_rect_t r = ok;

    if (ok.w <= 0)
        return r;
    r.w = 76;
    r.x = ok.x - r.w - 10;
    return r;
}

void gs_ui_confirm_compose(unsigned int *px, int w, int h,
                           const gs_ui_state_t *state)
{
    gs_ui_rect_t box;

    size_t i, n;

    if (!gs_ui_confirm_visible(state))
        return;

    /* Everything under the box drops to half brightness, so the box is
     * unmistakably the only thing awake. Halving each channel is one
     * shift and one mask per pixel. */
    n = (size_t)w * (size_t)h;
    for (i = 0; i < n; i++)
        px[i] = (px[i] >> 1) & 0x007f7f7fu;

    box = gs_ui_confirm_rect(w, h, state->confirm_step);
    if (box.w <= 0)
        return;

    gs_ui_px_rounded(px, w, h, box, 5, GS_UI_BAR_FILL, GS_UI_BTN_EDGE);

    if (state->confirm_step != GS_UI_CONFIRM_STEPS)
        return;

    if (state->confirm_busy) {
        /* Twelve dots on a circle, the bright one walking round. The
         * circle is drawn from a table rather than trigonometry, since
         * the project uses no maths library. Positions are 24ths of the
         * radius for a circle of twelve. */
        static const int dot_x[12] = {0, 12, 21, 24, 21, 12, 0, -12, -21,
                                      -24, -21, -12};
        static const int dot_y[12] = {-24, -21, -12, 0, 12, 21, 24, 21,
                                      12, 0, -12, -21};
        int cx = box.x + box.w / 2;
        int cy = box.y + box.h - 34;
        int d;

        for (d = 0; d < 12; d++) {
            int age = (d - state->spin_phase % 12 + 12) % 12;
            int lit = 255 - age * 18;
            gs_ui_rect_t dot;

            dot.x = cx + dot_x[d] * 2 / 3 - 2;
            dot.y = cy + dot_y[d] * 2 / 3 - 2;
            dot.w = 4;
            dot.h = 4;
            gs_ui_px_rect(px, w, h, dot,
                          ((unsigned int)lit << 16) |
                          ((unsigned int)lit << 8) | (unsigned int)lit);
        }

        /* The measured bar under the spinner, drawn only when a percent
         * exists, which means a pull with partial bytes on the disk. */
        if (state->busy_percent >= 0 && state->busy_percent <= 99 &&
            box.w > 60) {
            gs_ui_rect_t track, fill;

            track.x = box.x + 16;
            track.y = box.y + box.h - 12;
            track.w = box.w - 32;
            track.h = 6;
            gs_ui_px_rounded(px, w, h, track, 2, GS_UI_BTN_FACE_DOWN,
                             GS_UI_PANE_EDGE);
            fill = track;
            fill.w = track.w * state->busy_percent / 100;
            if (fill.w > 2)
                gs_ui_px_rounded(px, w, h, fill, 2, GS_UI_ACCENT,
                                 GS_UI_ACCENT);
        }
        return;
    }

    {
        gs_ui_rect_t ok = gs_ui_confirm_ok_rect(w, h);
        gs_ui_rect_t no = gs_ui_confirm_no_rect(w, h);

        gs_ui_px_rounded(px, w, h, ok, 4,
                         state->hover == GS_UI_HIT_CONFIRM_OK
                             ? 0x00583038u : 0x00402428u,
                         0x00785058u);
        gs_ui_px_rounded(px, w, h, no, 4,
                         state->hover == GS_UI_HIT_CONFIRM_NO
                             ? GS_UI_BTN_FACE_HOVER : GS_UI_BTN_FACE,
                         GS_UI_BTN_EDGE);
    }
}

void gs_ui_confirm_labels(gs_window_t *win, const gs_ui_state_t *state)
{
    gs_ui_rect_t box, ok, no;
    const gs_library_entry_t *entry;
    char line[200];
    char size[32];
    int w, h, y;

    if (win == NULL || !gs_ui_confirm_visible(state))
        return;
    if (state->confirm_step != GS_UI_CONFIRM_STEPS)
        return;

    w = gs_window_width(win);
    h = gs_window_height(win);
    box = gs_ui_confirm_rect(w, h, state->confirm_step);
    if (box.w <= 0)
        return;

    y = box.y + 14 + gs_window_font_ascent(win);

    /* The same box asks both questions. Adding reads the fit list and
     * removing reads the disk, so the name comes from different tables. */
    if (state->confirm_add) {
        const gs_catalogue_entry_t *fit = gs_ui_panes_model(
            state->confirm_row);

        if (state->confirm_busy) {
            gs_window_text(win, box.x + 16, y, "Pulling onto the disk",
                           GS_UI_BTN_LABEL);
            if (fit != NULL)
                gs_window_text(win, box.x + 16, y + 24, fit->file,
                               GS_UI_ROW_TEXT);
            if (state->busy_percent >= 0 && state->busy_percent <= 99) {
                snprintf(line, sizeof line, "%d%%", state->busy_percent);
                gs_window_text(win, box.x + box.w - 16 -
                               gs_window_text_width(win, line), y + 24,
                               line, GS_UI_ROW_TEXT);
            }
            return;
        }
        gs_window_text(win, box.x + 16, y, "Add this model to the disk?",
                       GS_UI_BTN_LABEL);
        if (fit != NULL) {
            gs_str_bytes(fit->bytes, size, sizeof size);
            snprintf(line, sizeof line, "%s   %s", fit->file, size);
            gs_window_text(win, box.x + 16, y + 24, line, GS_UI_ROW_TEXT);
        }
        gs_window_text(win, box.x + 16, y + 48,
                       "Downloads through ollama into its store",
                       GS_UI_ROW_DIM);

        ok = gs_ui_confirm_ok_rect(w, h);
        no = gs_ui_confirm_no_rect(w, h);
        gs_window_text(win, ok.x + (ok.w - 3 * gs_ui_glyph_width()) / 2,
                       ok.y + (ok.h + gs_window_font_ascent(win)) / 2 - 1,
                       "Add", GS_UI_BTN_LABEL);
        gs_window_text(win, no.x + (no.w - 6 * gs_ui_glyph_width()) / 2,
                       no.y + (no.h + gs_window_font_ascent(win)) / 2 - 1,
                       "Cancel", GS_UI_BTN_LABEL);
        return;
    }

    entry = gs_library_at(state->confirm_row);

    if (state->confirm_busy) {
        gs_window_text(win, box.x + 16, y, "Removing from the disk",
                       GS_UI_BTN_LABEL);
        if (entry != NULL)
            gs_window_text(win, box.x + 16, y + 24, entry->name,
                           GS_UI_ROW_TEXT);
        return;
    }

    gs_window_text(win, box.x + 16, y, "Remove this model from the disk?",
                   GS_UI_BTN_LABEL);
    if (entry != NULL) {
        gs_str_bytes(entry->bytes, size, sizeof size);
        snprintf(line, sizeof line, "%s   %s", entry->name, size);
        gs_window_text(win, box.x + 16, y + 24, line, GS_UI_ROW_TEXT);
    }
    gs_window_text(win, box.x + 16, y + 48,
                   "Tags share layers, so less space can come back",
                   GS_UI_ROW_DIM);

    ok = gs_ui_confirm_ok_rect(w, h);
    no = gs_ui_confirm_no_rect(w, h);
    gs_window_text(win, ok.x + (ok.w - 6 * 6) / 2,
                   ok.y + (ok.h + gs_window_font_ascent(win)) / 2 - 1,
                   "Remove", GS_UI_BTN_LABEL);
    gs_window_text(win, no.x + (no.w - 6 * 6) / 2,
                   no.y + (no.h + gs_window_font_ascent(win)) / 2 - 1,
                   "Cancel", GS_UI_BTN_LABEL);
}
