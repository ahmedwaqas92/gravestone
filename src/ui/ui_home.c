/* ui_home.c
 *
 * The screen the window opens on. A bare canvas in the same colours as
 * the workspace, carrying a settings gear in the top right and whatever
 * warning the last startup check produced.
 *
 * The gear wears a small mark while the database holds no settings, so a
 * first run says plainly that something needs configuring.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "window.h"

#include <stdio.h>
#include <string.h>

#define GEAR_SIZE   34
#define GEAR_INSET  22
#define MARK_RADIUS  5

gs_ui_rect_t gs_ui_gear_rect(int width, int height)
{
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (width < GEAR_SIZE + 2 * GEAR_INSET || height < GEAR_SIZE + 2 * GEAR_INSET)
        return r;
    r.x = width - GEAR_INSET - GEAR_SIZE;
    r.y = GEAR_INSET;
    r.w = GEAR_SIZE;
    r.h = GEAR_SIZE;
    return r;
}

/* A ring with eight teeth, drawn from a distance test rather than from
 * trigonometry, since the project carries no maths library. */
static void draw_gear(unsigned int *px, int w, int h, gs_ui_rect_t r,
                      unsigned int colour)
{
    int cx = r.x + r.w / 2;
    int cy = r.y + r.h / 2;
    int outer = r.w / 2 - 2;
    int inner = outer / 2;
    int dx, dy;

    for (dy = -outer - 2; dy <= outer + 2; dy++) {
        for (dx = -outer - 2; dx <= outer + 2; dx++) {
            int d2 = dx * dx + dy * dy;
            int on = 0;

            /* The rim of the wheel. */
            if (d2 <= outer * outer && d2 >= (outer - 3) * (outer - 3))
                on = 1;
            /* Four teeth on the axes and four on the diagonals, each a
             * short square block reaching past the rim. */
            if (d2 <= (outer + 2) * (outer + 2)) {
                int ax = dx < 0 ? -dx : dx;
                int ay = dy < 0 ? -dy : dy;

                if ((ax <= 2 || ay <= 2 || (ax - ay <= 2 && ay - ax <= 2)) &&
                    d2 >= (outer - 3) * (outer - 3))
                    on = 1;
            }
            /* The hole in the middle stays open. */
            if (d2 <= inner * inner)
                on = 0;
            if (on)
                gs_ui_px_rect(px, w, h,
                              (gs_ui_rect_t){cx + dx, cy + dy, 1, 1}, colour);
        }
    }
}

void gs_ui_home_compose(unsigned int *px, int w, int h,
                        const gs_ui_state_t *state)
{
    gs_ui_rect_t gear;
    int dx, dy;

    if (state == NULL || state->screen != GS_UI_SCREEN_HOME)
        return;

    gs_ui_chat_compose(px, w, h, state);

    gear = gs_ui_gear_rect(w, h);
    if (gear.w <= 0)
        return;

    draw_gear(px, w, h, gear,
              state->hover == GS_UI_HIT_SETTINGS ? GS_UI_BTN_LABEL
                                                 : GS_UI_STATUS_TEXT);

    /* The mark on the gear reports whether models are ready to use. A
     * green tick means the chosen disk holds some, and a red circle
     * means it holds none, whatever else has been configured. */
    {
        int cx = gear.x + gear.w - 2;
        int cy = gear.y + 2;

        if (state->library_count > 0) {
            int step;

            /* The short arm of the tick, then the long one. */
            for (step = 0; step < 3; step++)
                gs_ui_px_rect(px, w, h,
                              (gs_ui_rect_t){cx - 5 + step, cy + step, 2, 2},
                              GS_UI_LED_ON);
            for (step = 0; step < 6; step++)
                gs_ui_px_rect(px, w, h,
                              (gs_ui_rect_t){cx - 2 + step, cy + 2 - step,
                                             2, 2}, GS_UI_LED_ON);
        } else {
            for (dy = -MARK_RADIUS; dy <= MARK_RADIUS; dy++)
                for (dx = -MARK_RADIUS; dx <= MARK_RADIUS; dx++)
                    if (dx * dx + dy * dy <= MARK_RADIUS * MARK_RADIUS)
                        gs_ui_px_rect(px, w, h,
                                      (gs_ui_rect_t){cx + dx, cy + dy, 1, 1},
                                      GS_UI_LED_OFF);
        }
    }
}

void gs_ui_home_labels(gs_window_t *win, const gs_ui_state_t *state)
{
    int w, h, y;
    int free_left = 0;
    int free_width;

    if (win == NULL || state == NULL || state->screen != GS_UI_SCREEN_HOME)
        return;

    w = gs_window_width(win);
    h = gs_window_height(win);
    y = h / 2;

    gs_ui_chat_labels(win, state);

    /* The line about the models is centred in whatever the chat panel
     * leaves, so the two never sit on top of one another. */
    {
        gs_ui_rect_t chat = gs_ui_chat_rect(w, h);

        if (chat.w > 0) {
            free_left = chat.x + chat.w;
            free_width = w - free_left;
        }
    }

    free_width = free_left > 0 ? free_width : w;

    if (state->alert[0] != '\0') {
        int tw = gs_window_text_width(win, state->alert);

        gs_window_text(win, free_left + (free_width - tw) / 2, y,
                       state->alert, GS_UI_LED_OFF);
    } else if (state->library_count > 0) {
        char line[96];
        int tw;

        snprintf(line, sizeof line, "%d model%s ready on this disk",
                 state->library_count, state->library_count == 1 ? "" : "s");
        tw = gs_window_text_width(win, line);
        gs_window_text(win, free_left + (free_width - tw) / 2, y, line,
                       GS_UI_ROW_TEXT);
    } else {
        const char *line = "Open settings to add a model";
        int tw = gs_window_text_width(win, line);

        gs_window_text(win, free_left + (free_width - tw) / 2, y, line,
                       GS_UI_ROW_TEXT);
    }

    /* The word beside the gear, so the icon needs no guessing. */
    {
        gs_ui_rect_t gear = gs_ui_gear_rect(w, h);
        const char *word = "settings";
        int tw = gs_window_text_width(win, word);

        if (gear.w > 0 && gear.x - tw - 10 > 0)
            gs_window_text(win, gear.x - tw - 10,
                           gear.y + (gear.h + gs_window_font_ascent(win)) / 2
                           - 1, word, GS_UI_ROW_DIM);
    }
}
