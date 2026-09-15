/* ui_chat.c
 *
 * The chat panel down the left of the home screen. A space for the
 * conversation, and a composer underneath holding the typed prompt with
 * a plus for attaching a file and an arrow for sending.
 *
 * Nothing is sent anywhere yet. The panel takes text, draws it wrapped,
 * and reports which of its controls a click landed on.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "str.h"
#include "window.h"

#include <stdio.h>
#include <string.h>

#define PAD 10

gs_ui_rect_t gs_ui_chat_rect(int width, int height)
{
    gs_ui_rect_t r = {0, 0, 0, 0};

    /* The panel runs the whole height between the margins, so the left
     * of the window belongs to the conversation and nothing else. */
    r.x = GS_UI_MARGIN;
    r.y = GS_UI_MARGIN;
    r.w = width * GS_UI_CHAT_SHARE / 100 - GS_UI_MARGIN;
    r.h = height - 2 * GS_UI_MARGIN;

    /* A panel too small to hold the composer is no panel. */
    if (r.w < 200 || r.h < GS_UI_CHAT_COMPOSER + 40) {
        r.w = 0;
        r.h = 0;
        return r;
    }
    return r;
}

gs_ui_rect_t gs_ui_chat_composer_rect(int width, int height)
{
    gs_ui_rect_t panel = gs_ui_chat_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (panel.w <= 0)
        return r;
    r.x = panel.x;
    r.w = panel.w;
    r.h = GS_UI_CHAT_COMPOSER;
    r.y = panel.y + panel.h - r.h;
    return r;
}

/* The buttons share the strip along the bottom of the composer, sitting
 * clear of its lower edge rather than against it. */
static int button_row_y(gs_ui_rect_t box)
{
    return box.y + box.h - GS_UI_CHAT_FOOT - GS_UI_CHAT_SIDE;
}

gs_ui_rect_t gs_ui_chat_attach_rect(int width, int height)
{
    gs_ui_rect_t box = gs_ui_chat_composer_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (box.w <= 0)
        return r;
    r.w = GS_UI_CHAT_SIDE;
    r.h = GS_UI_CHAT_SIDE;
    r.x = box.x + PAD;
    r.y = button_row_y(box);
    return r;
}

gs_ui_rect_t gs_ui_chat_send_rect(int width, int height)
{
    gs_ui_rect_t box = gs_ui_chat_composer_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (box.w <= 0)
        return r;
    r.w = GS_UI_CHAT_SIDE;
    r.h = GS_UI_CHAT_SIDE;
    r.x = box.x + box.w - PAD - r.w;
    r.y = button_row_y(box);
    return r;
}

/* The button naming how many models answer, between the other two. */
gs_ui_rect_t gs_ui_chat_fleet_rect(int width, int height)
{
    gs_ui_rect_t attach = gs_ui_chat_attach_rect(width, height);
    gs_ui_rect_t send = gs_ui_chat_send_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (attach.w <= 0)
        return r;
    r.x = attach.x + attach.w + 10;
    r.y = attach.y;
    r.h = GS_UI_CHAT_SIDE;
    r.w = 126;
    /* It gives way rather than growing under the send arrow. */
    if (r.x + r.w > send.x - 10) {
        r.w = 0;
        r.h = 0;
    }
    return r;
}

int gs_ui_chat_type(gs_ui_state_t *state, int ch)
{
    size_t n;

    if (state == NULL)
        return 0;
    n = strlen(state->prompt);

    if (ch == 8) {
        if (n == 0)
            return 0;
        state->prompt[n - 1] = '\0';
        return 1;
    }
    /* A return goes into the prompt as a line break, since a question
     * worth asking often needs more than one line. The loop only passes
     * one down here when shift was held, because return on its own is
     * what sends the prompt. */
    if (ch != '\n' && (ch < 0x20 || ch > 0x7e))
        return 0;
    if (n + 1 >= sizeof state->prompt)
        return 0;
    state->prompt[n] = (char)ch;
    state->prompt[n + 1] = '\0';
    return 1;
}

void gs_ui_chat_compose(unsigned int *px, int w, int h,
                        const gs_ui_state_t *state)
{
    gs_ui_rect_t panel, box, send, attach;
    int arm;

    if (state == NULL || state->screen != GS_UI_SCREEN_HOME)
        return;
    panel = gs_ui_chat_rect(w, h);
    if (panel.w <= 0)
        return;

    gs_ui_px_rounded(px, w, h, panel, 5, GS_UI_PANE_FILL, GS_UI_PANE_EDGE);

    /* One bubble behind each thing said. The words go on in the pass that
     * draws glyphs, and both passes ask the same rectangle, so the shape
     * and the writing cannot drift apart. */
    {
        gs_ui_rect_t seen = gs_ui_chat_history_rect(w, h);
        int i;

        for (i = 0; i < state->history_count; i++) {
            gs_ui_rect_t bubble = gs_ui_chat_bubble_rect(state, w, h, i);

            if (bubble.w <= 0 || bubble.h <= 0)
                continue;
            /* A bubble part way past either edge is cut at that edge, so
             * nothing is drawn over the heading above or into the box
             * below while the history is scrolled. */
            if (bubble.y < seen.y) {
                bubble.h -= seen.y - bubble.y;
                bubble.y = seen.y;
            }
            if (bubble.y + bubble.h > seen.y + seen.h)
                bubble.h = seen.y + seen.h - bubble.y;
            if (bubble.h <= 0)
                continue;
            if (state->history_kind[i] != GS_UI_LINE_SAID) {
                /* The lines that move between pages are pressed, so they
                 * take the shade of a button and light up under the
                 * pointer. */
                int lit = (state->hover == GS_UI_HIT_CHAT_EARLIER ||
                           state->hover == GS_UI_HIT_CHAT_NEWER) &&
                          state->hover_row == i;

                gs_ui_px_rounded(px, w, h, bubble, 6,
                                 lit ? GS_UI_CHAT_HOVER : GS_UI_CHAT_FACE,
                                 GS_UI_CHAT_EDGE);
                continue;
            }
            gs_ui_px_rounded(px, w, h, bubble, 6,
                             state->history_mine[i] ? GS_UI_BUBBLE_MINE
                                                    : GS_UI_BUBBLE_THEIRS,
                             GS_UI_BUBBLE_EDGE);

            /* The mark opening the record of how this line was arrived
             * at, on the lines that have one. */
            {
                gs_ui_rect_t note = gs_ui_chat_note_rect(state, w, h, i);

                if (note.w > 0 && note.y >= seen.y)
                    gs_ui_px_rounded(px, w, h, note, 4,
                                     state->hover == GS_UI_HIT_NOTE &&
                                     state->hover_row == i
                                         ? GS_UI_CHAT_HOVER
                                         : GS_UI_CHAT_FACE,
                                     GS_UI_CHAT_EDGE);
            }
        }
    }

    /* The bar down the right of the history, saying how much of the page
     * lies above what is showing. Its grip sits at the bottom while the
     * newest line is against the box. */
    {
        gs_ui_rect_t bar = gs_ui_chat_bar_box(w, h);
        gs_ui_rect_t seen = gs_ui_chat_history_rect(w, h);
        gs_ui_rect_t rail, grip;
        int total = gs_ui_chat_content_height(state, w, h);
        int reach = gs_ui_chat_scroll_reach(state, w, h);
        int back = state->history_back;

        if (back < 0)
            back = 0;
        if (back > reach)
            back = reach;
        if (bar.w > 0 &&
            gs_ui_scrollbar(bar, total, seen.h, reach - back, &rail, &grip)) {
            gs_ui_px_rounded(px, w, h, rail, 2, GS_UI_SCROLL_RAIL,
                             GS_UI_SCROLL_RAIL);
            gs_ui_px_rounded(px, w, h, grip, 2,
                             state->drag_bar == GS_UI_BAR_CHAT
                                 ? GS_UI_SCROLL_HELD : GS_UI_SCROLL_GRIP,
                             GS_UI_SCROLL_GRIP);
        }
    }

    /* The record itself, over everything, while one is open. */
    gs_ui_record_compose(px, w, h, state);

    box = gs_ui_chat_composer_rect(w, h);
    gs_ui_px_rounded(px, w, h, box, 5, GS_UI_BTN_FACE_DOWN, GS_UI_BTN_EDGE);

    attach = gs_ui_chat_attach_rect(w, h);
    send = gs_ui_chat_send_rect(w, h);

    gs_ui_px_rounded(px, w, h, attach, 5,
                     state->hover == GS_UI_HIT_CHAT_ATTACH
                         ? GS_UI_CHAT_HOVER : GS_UI_CHAT_FACE,
                     GS_UI_CHAT_EDGE);
    /* The plus, two bars crossing at the middle of its box. */
    {
        int cx = attach.x + attach.w / 2;
        int cy = attach.y + attach.h / 2;

        gs_ui_px_rect(px, w, h, (gs_ui_rect_t){cx - 6, cy - 1, 13, 2},
                      GS_UI_CHAT_INK);
        gs_ui_px_rect(px, w, h, (gs_ui_rect_t){cx - 1, cy - 6, 2, 13},
                      GS_UI_CHAT_INK);
    }

    /* The button naming how many models answer one prompt. */
    {
        gs_ui_rect_t fleet = gs_ui_chat_fleet_rect(w, h);

        if (fleet.w > 0)
            gs_ui_px_rounded(px, w, h, fleet, 5,
                             state->hover == GS_UI_HIT_FLEET
                                 ? GS_UI_CHAT_HOVER : GS_UI_CHAT_FACE,
                             GS_UI_CHAT_EDGE);
    }

    /* The send button lights up once there is something to send. */
    gs_ui_px_rounded(px, w, h, send, 5,
                     state->prompt[0] == '\0' ? GS_UI_BTN_OFF_FACE
                         : (state->hover == GS_UI_HIT_CHAT_SEND
                                ? GS_UI_CHAT_HOVER : GS_UI_CHAT_LIVE),
                     state->prompt[0] == '\0' ? GS_UI_BTN_OFF_EDGE
                                              : GS_UI_CHAT_EDGE);
    {
        unsigned int ink = state->prompt[0] == '\0' ? GS_UI_BTN_OFF_LABEL
                                                    : GS_UI_CHAT_INK;
        int cx = send.x + send.w / 2;
        int cy = send.y + send.h / 2;

        /* The shaft, then the head widening downward from the tip. */
        gs_ui_px_rect(px, w, h, (gs_ui_rect_t){cx - 1, cy - 6, 2, 13}, ink);
        for (arm = 0; arm < 5; arm++)
            gs_ui_px_rect(px, w, h,
                          (gs_ui_rect_t){cx - arm - 1, cy - 6 + arm,
                                         2 * arm + 3, 2}, ink);
    }

    gs_ui_fleet_compose(px, w, h, state);
}

/* The prompt box, written in the same letters the bubbles use so the two
 * read as one panel. The room is measured in pixels, since the letters
 * differ in width. */
static void draw_wrapped(gs_window_t *win, gs_ui_rect_t box,
                         const char *text, unsigned int colour,
                         int show_caret)
{
    int room = box.w - 2 * PAD;
    int line_height = gs_window_face_height() + 4;
    int lines = (box.h - GS_UI_CHAT_BUTTONS - 6) / line_height;
    int y = box.y + 6 + gs_window_face_ascent();
    size_t at = 0;
    size_t len = strlen(text);
    int drawn = 0;
    int skip;

    if (room <= 0 || lines <= 0)
        return;

    /* The box shows the lines nearest the caret, so a prompt longer than
     * the box scrolls up as it grows and the line being written stays in
     * sight. */
    skip = gs_ui_chat_composer_skip(text, room, lines);
    while (skip > 0) {
        char piece[256];

        if (!gs_ui_chat_next_line(text, room, &at, piece, sizeof piece))
            break;
        skip--;
    }

    while (drawn < lines) {
        char piece[256];

        /* One walk of the prompt reads every line, and the walk steps
         * over the break at the end of each, which is never drawn. */
        if (!gs_ui_chat_next_line(text, room, &at, piece, sizeof piece))
            break;

        gs_window_face_text(win, box.x + PAD, y + drawn * line_height, piece,
                            colour, GS_UI_BTN_FACE_DOWN);
        drawn++;

        /* The caret stands after the last letter written, which is where
         * the next one will land. A prompt ending in a break puts it at
         * the start of the empty line below. */
        if (show_caret && at >= len && len > 0 && text[len - 1] == '\n' &&
            drawn < lines) {
            gs_window_fill(win, box.x + PAD,
                           y + drawn * line_height -
                           gs_window_face_ascent() + 1,
                           2, gs_window_face_height(), GS_UI_CHAT_CURSOR);
        } else if (show_caret && at >= len) {
            gs_window_fill(win, box.x + PAD + gs_window_face_width(piece) + 1,
                           y + (drawn - 1) * line_height -
                           gs_window_face_ascent() + 1,
                           2, gs_window_face_height(), GS_UI_CHAT_CURSOR);
        }
    }

    /* An empty prompt still shows the caret, at the start of the line. */
    if (show_caret && len == 0)
        gs_window_fill(win, box.x + PAD, box.y + 8, 2,
                       gs_window_face_height(), GS_UI_CHAT_CURSOR);
}

void gs_ui_chat_labels(gs_window_t *win, const gs_ui_state_t *state)
{
    gs_ui_rect_t panel, box;
    int w, h;

    if (win == NULL || state == NULL ||
        state->screen != GS_UI_SCREEN_HOME)
        return;
    w = gs_window_width(win);
    h = gs_window_height(win);
    panel = gs_ui_chat_rect(w, h);
    if (panel.w <= 0)
        return;

    {
        gs_ui_rect_t head = panel;

        head.h = 24;
        if (!gs_ui_text_covered(state, w, h, head)) {
            int base = panel.y + 8 + gs_window_font_ascent(win);

            gs_window_text(win, panel.x + PAD, base, "CHAT",
                           GS_UI_HEADER_TEXT);

            /* What the run is doing, against the right edge of the same
             * line. A question put to three models takes a while, and a
             * screen saying nothing at all reads as a program that has
             * stopped. The words come from the same two fields the
             * workspace bar shows, so both screens say the same thing. */
            if (state->status[0] != '\0') {
                int tw = gs_window_text_width(win, state->status);
                int x = panel.x + panel.w - PAD - tw;

                if (x > panel.x + PAD + 60)
                    gs_window_text(win, x, base, state->status,
                                   GS_UI_HEADER_TEXT);
            }
            if (state->detail[0] != '\0') {
                int tw = gs_window_text_width(win, state->detail);
                int x = panel.x + panel.w - PAD - tw;

                if (x > panel.x + PAD + 60)
                    gs_window_text(win, x,
                                   base + gs_window_font_height(win) + 2,
                                   state->detail, GS_UI_ROW_DIM);
            }
        }
    }

    /* The name above each bubble and the words inside it. */
    {
        gs_ui_rect_t seen = gs_ui_chat_history_rect(w, h);
        int room = gs_ui_chat_bubble_columns(w, h);
        int i;

        for (i = 0; i < state->history_count && room > 0; i++) {
            gs_ui_rect_t bubble = gs_ui_chat_bubble_rect(state, w, h, i);
            char line[GS_UI_HISTORY_TEXT];
            size_t at = 0;
            int n = 0;
            int top;

            if (bubble.w <= 0)
                continue;
            top = bubble.y;

            /* Nothing under an open panel is drawn, since words go on
             * the glass after the shapes and would otherwise sit over
             * the panel rather than under it. */
            if (gs_ui_text_covered(state, w, h, bubble))
                continue;

            /* A line moving between pages says what pressing it does,
             * in the middle of its bubble. It is wrapped by the same walk
             * that measured the bubble, since drawing it whole ran it out
             * past the button and the edge of the panel on any window too
             * narrow to hold it in one line. */
            if (state->history_kind[i] != GS_UI_LINE_SAID) {
                int rise = gs_window_face_ascent();
                int lit = (state->hover == GS_UI_HIT_CHAT_EARLIER ||
                           state->hover == GS_UI_HIT_CHAT_NEWER) &&
                          state->hover_row == i;
                int lines = gs_ui_chat_bubble_lines(state, w, h, i);
                int first = top + (bubble.h - lines * GS_UI_BUBBLE_LINE) / 2;

                while (gs_ui_chat_next_line(state->history[i], room, &at,
                                            line, sizeof line)) {
                    int y = first + n * GS_UI_BUBBLE_LINE + rise;

                    if (y - rise >= seen.y && y <= seen.y + seen.h)
                        gs_window_face_text(
                            win, bubble.x + GS_UI_BUBBLE_PAD_X, y, line,
                            GS_UI_CHAT_INK,
                            lit ? GS_UI_CHAT_HOVER : GS_UI_CHAT_FACE);
                    n++;
                }
                continue;
            }

            /* The name and when it was said, unless the bubble has
             * climbed past the top. The moment sits after the name so
             * the two read as one line, and it is written by the clock
             * of this machine rather than by any shared one. */
            if (top >= seen.y &&
                top + GS_UI_BUBBLE_HEAD <= seen.y + seen.h) {
                const char *who = state->history_mine[i] ? "You"
                                                         : "Gravestone agent";
                char said[32];
                int y = top + GS_UI_BUBBLE_HEAD - 5;

                gs_window_text(win, bubble.x + GS_UI_BUBBLE_PAD_X, y, who,
                               GS_UI_BUBBLE_NAME);

                gs_str_clock(state->history_at[i], said, sizeof said);
                if (said[0] != '\0') {
                    int after = bubble.x + GS_UI_BUBBLE_PAD_X +
                                gs_window_text_width(win, who) +
                                GS_UI_BUBBLE_WHEN_GAP;

                    if (after + gs_window_text_width(win, said) <=
                        bubble.x + bubble.w - GS_UI_BUBBLE_PAD_X)
                        gs_window_text(win, after, y, said,
                                       GS_UI_BUBBLE_WHEN);
                }
            }

            while (gs_ui_chat_next_line(state->history[i], room, &at, line,
                                        sizeof line)) {
                int rise = gs_window_face_ascent();
                int y = top + GS_UI_BUBBLE_HEAD + GS_UI_BUBBLE_PAD_Y +
                        n * GS_UI_BUBBLE_LINE + rise;

                /* Only what is inside the area is written, so nothing
                 * lands over the heading or in the box below. */
                if (y - rise >= seen.y && y <= seen.y + seen.h)
                    gs_window_face_text(win, bubble.x + GS_UI_BUBBLE_PAD_X,
                                        y, line, GS_UI_CHAT_INK,
                                        state->history_mine[i]
                                            ? GS_UI_BUBBLE_MINE
                                            : GS_UI_BUBBLE_THEIRS);
                n++;
            }
        }
    }

    /* The mark on each line that has a record, and the record itself
     * while one is open. */
    {
        int i;

        for (i = 0; i < state->history_count; i++) {
            gs_ui_rect_t note = gs_ui_chat_note_rect(state, w, h, i);
            gs_ui_rect_t seen = gs_ui_chat_history_rect(w, h);

            if (note.w > 0 && note.y >= seen.y &&
                !gs_ui_text_covered(state, w, h, note))
                gs_window_text(win, note.x + 5,
                               note.y + 12, "i", GS_UI_CHAT_INK);
        }
    }

    gs_ui_record_draw(win, state);

    box = gs_ui_chat_composer_rect(w, h);
    if (gs_ui_text_covered(state, w, h, box)) {
        /* The box sits under an open panel, so its words wait. */
    } else if (state->prompt[0] != '\0') {
        draw_wrapped(win, box, state->prompt, GS_UI_ROW_TEXT,
                     state->cursor_on);
    } else {
        draw_wrapped(win, box, "Ask anything", GS_UI_ROW_DIM, 0);
        if (state->cursor_on)
            draw_wrapped(win, box, "", GS_UI_ROW_DIM, 1);
    }

    /* The button says how many models the prompt would go to. */
    {
        gs_ui_rect_t fleet = gs_ui_chat_fleet_rect(w, h);
        char label[40];

        if (fleet.w > 0 && !gs_ui_text_covered(state, w, h, fleet)) {
            snprintf(label, sizeof label, "models loaded  %d",
                     state->chosen_count);
            gs_window_text(win, fleet.x + 10,
                           fleet.y + (fleet.h + gs_window_font_ascent(win))
                           / 2 - 1, label, GS_UI_CHAT_INK);
        }
    }

    gs_ui_fleet_labels(win, state);
}
