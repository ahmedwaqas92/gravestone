/* ui_fleet.c
 *
 * The box listing the models sitting on the chosen disk, where the ones
 * answering the next prompt are ticked. It grows out of the chat panel
 * in the same motion as the specs panel and folds away when dismissed.
 *
 * Asking several models the same question is what makes comparing their
 * answers possible, so this list decides how much evidence one prompt
 * produces.
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

#define TICK_SIDE 14
#define PAD       14

int gs_ui_fleet_visible(const gs_ui_state_t *state)
{
    return state != NULL && (state->fleet_step > 0 || state->fleet_dir != 0);
}

int gs_ui_fleet_animating(const gs_ui_state_t *state)
{
    return state != NULL && state->fleet_dir != 0;
}

int gs_ui_fleet_advance(gs_ui_state_t *state)
{
    if (state == NULL || state->fleet_dir == 0)
        return 0;

    state->fleet_step += state->fleet_dir;
    if (state->fleet_step >= GS_UI_FLEET_STEPS) {
        state->fleet_step = GS_UI_FLEET_STEPS;
        state->fleet_dir = 0;
        return 0;
    }
    if (state->fleet_step <= 0) {
        state->fleet_step = 0;
        state->fleet_dir = 0;
        return 0;
    }
    return 1;
}

int gs_ui_fleet_rows(const gs_ui_state_t *state)
{
    int have;

    if (state == NULL)
        return 0;
    have = state->library_count;
    if (have < 0)
        have = 0;
    if (have > GS_UI_FLEET_ROWS)
        have = GS_UI_FLEET_ROWS;
    /* An empty disk still needs a line saying so. */
    return have > 0 ? have : 1;
}

int gs_ui_fleet_chosen(const gs_ui_state_t *state, int index)
{
    if (state == NULL || index < 0 || index >= GS_UI_CHOSEN_MAX)
        return 0;
    return state->model_chosen[index] != 0;
}

void gs_ui_fleet_toggle(gs_ui_state_t *state, int index)
{
    int i;

    if (state == NULL || index < 0 || index >= GS_UI_CHOSEN_MAX)
        return;
    if (index >= state->library_count)
        return;

    state->model_chosen[index] = (unsigned char)!state->model_chosen[index];

    state->chosen_count = 0;
    for (i = 0; i < GS_UI_CHOSEN_MAX; i++)
        if (state->model_chosen[i])
            state->chosen_count++;
}

gs_ui_rect_t gs_ui_fleet_rect(const gs_ui_state_t *state, int width,
                              int height, int step)
{
    gs_ui_rect_t panel = gs_ui_chat_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};
    int full_h, bw, bh;

    if (panel.w <= 0 || step <= 0 || state == NULL)
        return r;
    if (step > GS_UI_FLEET_STEPS)
        step = GS_UI_FLEET_STEPS;

    full_h = GS_UI_FLEET_HEAD + gs_ui_fleet_rows(state) * GS_UI_FLEET_ROW +
             GS_UI_FLEET_FOOT;

    bw = 60 + (GS_UI_FLEET_W - 60) * step / GS_UI_FLEET_STEPS;
    bh = 26 + (full_h - 26) * step / GS_UI_FLEET_STEPS;
    if (bw > panel.w - 8)
        bw = panel.w - 8;
    if (bh > panel.h - GS_UI_CHAT_COMPOSER - 12)
        bh = panel.h - GS_UI_CHAT_COMPOSER - 12;
    if (bw <= 0 || bh <= 0)
        return r;

    /* It grows out of the composer, so it sits just above it. */
    r.x = panel.x + (panel.w - bw) / 2;
    r.y = panel.y + panel.h - GS_UI_CHAT_COMPOSER - bh - 8;
    if (r.y < panel.y)
        r.y = panel.y;
    r.w = bw;
    r.h = bh;
    return r;
}

int gs_ui_fleet_capacity(const gs_ui_state_t *state,
                         const gs_detect_report_t *machine,
                         long long *needed, long long *have)
{
    long long sizes[GS_UI_CHOSEN_MAX];
    gs_catalogue_room_t room;
    int picked = 0;
    int i;

    if (needed != NULL)
        *needed = 0;
    if (have != NULL)
        *have = 0;
    if (state == NULL || machine == NULL)
        return 0;

    room = gs_catalogue_room(machine);
    if (have != NULL)
        *have = room.memory_bytes + room.graphics_bytes;

    /* The ticked ones, in the order they sit in the list, since that is
     * the order a person reading the box sees them in. */
    for (i = 0; i < state->library_count && picked < GS_UI_CHOSEN_MAX; i++) {
        const gs_library_entry_t *entry;

        if (!gs_ui_fleet_chosen(state, i))
            continue;
        entry = gs_library_at(i);
        if (entry == NULL)
            continue;
        sizes[picked++] = entry->bytes;
    }

    if (needed != NULL)
        *needed = gs_catalogue_needed(sizes, picked);
    return gs_catalogue_fit_many(sizes, picked, machine);
}

gs_ui_rect_t gs_ui_fleet_close_rect(const gs_ui_state_t *state, int width,
                                    int height)
{
    gs_ui_rect_t box = gs_ui_fleet_rect(state, width, height,
                                        GS_UI_FLEET_STEPS);
    gs_ui_rect_t r = {0, 0, 0, 0};

    /* Only once the box has finished growing. A target that slides while
     * the pointer is coming down on it is worse than none. */
    if (box.w <= 0 || state == NULL ||
        state->fleet_step != GS_UI_FLEET_STEPS)
        return r;

    r.w = 18;
    r.h = 18;
    r.x = box.x + box.w - r.w - 6;
    r.y = box.y + 6;
    return r;
}

gs_ui_rect_t gs_ui_fleet_row_rect(const gs_ui_state_t *state, int width,
                                  int height, int index)
{
    gs_ui_rect_t box = gs_ui_fleet_rect(state, width, height,
                                        GS_UI_FLEET_STEPS);
    gs_ui_rect_t r = {0, 0, 0, 0};
    int slot;

    if (box.w <= 0 || state == NULL)
        return r;
    if (index < 0 || index >= state->library_count)
        return r;

    slot = index - state->fleet_scroll;
    if (slot < 0 || slot >= gs_ui_fleet_rows(state))
        return r;

    r.x = box.x + 6;
    r.w = box.w - 12;
    r.h = GS_UI_FLEET_ROW;
    r.y = box.y + GS_UI_FLEET_HEAD + slot * GS_UI_FLEET_ROW;
    return r;
}

void gs_ui_fleet_compose(unsigned int *px, int w, int h,
                         const gs_ui_state_t *state)
{
    gs_ui_rect_t box;
    int i;

    if (!gs_ui_fleet_visible(state))
        return;
    box = gs_ui_fleet_rect(state, w, h, state->fleet_step);
    if (box.w <= 0)
        return;

    gs_ui_px_rounded(px, w, h, box, 5, GS_UI_BAR_FILL, GS_UI_CHAT_EDGE);

    if (state->fleet_step != GS_UI_FLEET_STEPS)
        return;

    /* A bar down the right, since the list of models on a disk runs past
     * what the box shows. */
    {
        gs_ui_rect_t list = box;
        gs_ui_rect_t rail, grip;

        list.y += GS_UI_FLEET_HEAD;
        list.h -= GS_UI_FLEET_HEAD + GS_UI_FLEET_FOOT;
        if (gs_ui_scrollbar(list, state->library_count,
                            gs_ui_fleet_rows(state), state->fleet_scroll,
                            &rail, &grip)) {
            gs_ui_px_rounded(px, w, h, rail, 2, GS_UI_SCROLL_RAIL,
                             GS_UI_SCROLL_RAIL);
            gs_ui_px_rounded(px, w, h, grip, 2, GS_UI_SCROLL_GRIP,
                             GS_UI_SCROLL_GRIP);
        }
    }

    /* The cross that shuts it, two strokes across a square that lights up
     * under the pointer. */
    {
        gs_ui_rect_t shut = gs_ui_fleet_close_rect(state, w, h);
        unsigned int ink = state->hover == GS_UI_HIT_FLEET_CLOSE
                         ? GS_UI_CHAT_INK : GS_UI_ROW_DIM;
        int k;

        if (shut.w > 0) {
            if (state->hover == GS_UI_HIT_FLEET_CLOSE)
                gs_ui_px_rounded(px, w, h, shut, 3, GS_UI_ROW_HOVER,
                                 GS_UI_ROW_HOVER);
            /* Two strokes corner to corner, each two pixels wide so the
             * mark reads at this size. */
            for (k = 5; k < shut.w - 5; k++) {
                gs_ui_rect_t a, b;

                a.x = shut.x + k;
                a.y = shut.y + k;
                a.w = 2;
                a.h = 2;
                b.x = shut.x + k;
                b.y = shut.y + shut.h - 1 - k;
                b.w = 2;
                b.h = 2;
                gs_ui_px_rect(px, w, h, a, ink);
                gs_ui_px_rect(px, w, h, b, ink);
            }
        }
    }

    for (i = 0; i < state->library_count; i++) {
        gs_ui_rect_t row = gs_ui_fleet_row_rect(state, w, h, i);
        gs_ui_rect_t tick;
        int chosen = gs_ui_fleet_chosen(state, i);

        if (row.w <= 0)
            continue;
        if (state->hover == GS_UI_HIT_FLEET_OPTION && state->hover_row == i)
            gs_ui_px_rect(px, w, h, row, GS_UI_ROW_HOVER);

        tick.x = row.x + 6;
        tick.y = row.y + (row.h - TICK_SIDE) / 2;
        tick.w = TICK_SIDE;
        tick.h = TICK_SIDE;
        gs_ui_px_rounded(px, w, h, tick, 3,
                         chosen ? GS_UI_CHAT_LIVE : GS_UI_BTN_FACE_DOWN,
                         GS_UI_CHAT_EDGE);
        if (chosen) {
            int step;

            for (step = 0; step < 3; step++)
                gs_ui_px_rect(px, w, h,
                              (gs_ui_rect_t){tick.x + 3 + step,
                                             tick.y + 6 + step, 2, 2},
                              GS_UI_CHAT_INK);
            for (step = 0; step < 4; step++)
                gs_ui_px_rect(px, w, h,
                              (gs_ui_rect_t){tick.x + 6 + step,
                                             tick.y + 8 - step, 2, 2},
                              GS_UI_CHAT_INK);
        }
    }
}

/* Turns a token count into something short. A token is a chunk of text,
 * roughly three or four characters of English. */
static void context_text(long long tokens, char *out, size_t cap)
{
    if (tokens <= 0)
        gs_str_copy(out, cap, "context unknown");
    else if (tokens >= 1024)
        snprintf(out, cap, "%lldk context", tokens / 1024);
    else
        snprintf(out, cap, "%lld context", tokens);
}

void gs_ui_fleet_labels(gs_window_t *win, const gs_ui_state_t *state)
{
    gs_ui_rect_t box;
    const gs_detect_report_t *machine;
    char line[128];
    int w, h, i;

    if (win == NULL || !gs_ui_fleet_visible(state))
        return;
    if (state->fleet_step != GS_UI_FLEET_STEPS)
        return;

    w = gs_window_width(win);
    h = gs_window_height(win);
    box = gs_ui_fleet_rect(state, w, h, state->fleet_step);
    if (box.w <= 0)
        return;

    gs_window_text(win, box.x + PAD, box.y + 12 + gs_window_font_ascent(win),
                   "Models for the next prompt", GS_UI_BTN_LABEL);
    gs_window_text(win, box.x + PAD, box.y + 30 + gs_window_font_ascent(win),
                   "Tick every model that should answer", GS_UI_ROW_DIM);

    if (state->library_count == 0) {
        gs_window_text(win, box.x + PAD,
                       box.y + GS_UI_FLEET_HEAD + 14,
                       "No models on this disk", GS_UI_ROW_DIM);
        return;
    }

    /* Held between repaints rather than read here, since reading it
     * starts the vendor tools and this runs on every mouse move. */
    machine = gs_ui_machine();

    for (i = 0; i < state->library_count; i++) {
        gs_ui_rect_t row = gs_ui_fleet_row_rect(state, w, h, i);
        const gs_library_entry_t *entry = gs_library_at(i);
        char right[80];
        char context[40];
        gs_catalogue_fit_t fit = GS_FIT_NONE;
        int quick = 1;
        int baseline, room, cw;

        if (row.w <= 0 || entry == NULL)
            continue;
        baseline = row.y + (row.h + gs_window_font_ascent(win)) / 2 - 1;

        context_text(entry->context, context, sizeof context);
        /* Where the model would sit on this machine, written beside the
         * context window, so a person ticking a row sees before pressing
         * send that it would run on the processor and crawl. */
        if (machine != NULL) {
            fit = gs_catalogue_fit(entry->bytes, machine);
            quick = gs_catalogue_conversational(fit);
            /* The short form here, since a row in this box also carries
             * the model name and the context window, and the phrase
             * would push the name off the edge. */
            {
                char where[24];

                gs_ui_where(entry->bytes, machine, where, sizeof where);
                snprintf(right, sizeof right, "%s  %s", where, context);
            }
        } else {
            gs_str_copy(right, sizeof right, context);
        }
        cw = gs_window_text_width(win, right);
        room = row.w - TICK_SIDE - 18 - cw - 10;
        /* A window too narrow leaves no room at all, and a name drawn
         * anyway would run over the text on the right and past the edge
         * of the box, so it is cut until it fits or until nothing is
         * left. */
        if (room < 0)
            room = 0;

        gs_str_copy(line, sizeof line, entry->name);
        while (line[0] != '\0' && gs_window_text_width(win, line) > room)
            line[strlen(line) - 1] = '\0';

        gs_window_text(win, row.x + TICK_SIDE + 14, baseline, line,
                       gs_ui_fleet_chosen(state, i) ? GS_UI_BTN_LABEL
                                                    : GS_UI_ROW_TEXT);
        gs_window_text(win, row.x + row.w - 6 - cw, baseline, right,
                       quick ? GS_UI_ROW_DIM : GS_UI_LED_OFF);
    }

    /* What the ticked set would actually do. A run puts the question to
     * one model at a time, so each is measured against the whole machine
     * on its own rather than against what is left after the others.
     * Only the ones held entirely on the card answer at conversational
     * speed, and that count is the one a person needs before pressing
     * send. */
    {
        int quick = 0;
        int slow = 0;
        int refused = 0;

        for (i = 0; i < state->library_count; i++) {
            const gs_library_entry_t *entry = gs_library_at(i);
            gs_catalogue_fit_t fit;

            if (!gs_ui_fleet_chosen(state, i) || entry == NULL)
                continue;
            if (machine == NULL) {
                quick++;
                continue;
            }
            fit = gs_catalogue_fit(entry->bytes, machine);
            if (fit == GS_FIT_NONE)
                refused++;
            else if (gs_catalogue_conversational(fit))
                quick++;
            else
                slow++;
        }

        if (machine != NULL)
            snprintf(line, sizeof line,
                     "%d of %d chosen, %d on the card, %d slow, %d too large",
                     state->chosen_count, state->library_count,
                     quick, slow, refused);
        else
            snprintf(line, sizeof line, "%d of %d chosen",
                     state->chosen_count, state->library_count);

        gs_window_text(win, box.x + PAD, box.y + box.h - 10, line,
                       quick == 0 ? GS_UI_LED_OFF : GS_UI_ROW_DIM);
    }
}
