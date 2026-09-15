/* ui_record.c
 *
 * The record behind an answer. One run puts the question to every chosen
 * model in turn, and what each of them said, how long it took, and what
 * the checking made of it is written down as it happens. Pressing the
 * mark on an answer opens that record over the panel.
 *
 * The sheet covers the chat while it is up, so nothing under it answers a
 * click until it is shut.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "session.h"
#include "str.h"
#include "window.h"

#include <stdlib.h>
#include <string.h>

int gs_ui_record_showing(const gs_ui_state_t *state)
{
    if (state == NULL)
        return 0;
    if (state->record_open < 0 || state->record_open >= state->history_count)
        return 0;
    return state->record_text[0] != '\0';
}

/* Adds text to the sheet, cut to what is left. */
static void put(char *sheet, size_t cap, const char *text)
{
    size_t have = strlen(sheet);

    if (text == NULL || have + 1 >= cap)
        return;
    gs_str_copy(sheet + have, cap - have, text);
}

/* Reads one text from the file into room of its own, in the plain ASCII
 * the screen draws. Gives back NULL when there was no room to read it. */
static char *read_plain(int (*reader)(long long, char *, size_t),
                        long long id)
{
    char *raw = malloc(GS_UI_RECORD_FULL);
    char *plain = malloc(GS_UI_RECORD_FULL);

    if (raw == NULL || plain == NULL) {
        free(raw);
        free(plain);
        return NULL;
    }
    raw[0] = '\0';
    plain[0] = '\0';
    if (id > 0 && reader(id, raw, GS_UI_RECORD_FULL) == GS_OK)
        gs_str_to_ascii(raw, plain, GS_UI_RECORD_FULL);
    free(raw);
    return plain;
}

void gs_ui_record_open(gs_ui_state_t *state, int row)
{
    char *sheet;
    char *working;
    char *note;
    char when[48];
    long long turn;
    long long answer;
    long long asked = 0;
    int k;

    if (state == NULL)
        return;
    if (row < 0 || row >= state->history_count) {
        gs_ui_record_shut(state);
        return;
    }
    sheet = state->record_text;
    sheet[0] = '\0';
    state->record_open = row;
    state->record_scroll = 0;
    turn = state->record_turn[row];
    answer = state->record_answer[row];

    /* When the question was asked is on its own line, just above. */
    for (k = row; k >= 0; k--)
        if (state->history_mine[k] && state->record_turn[k] == turn &&
            turn > 0) {
            asked = state->history_at[k];
            break;
        }

    /* Everything behind the line is read from the file now rather than
     * held for every line, since a model's reasoning and the record of a
     * run each run to several thousand characters and one sheet is open
     * at a time. */
    working = read_plain(gs_session_working, answer);
    note = read_plain(gs_session_turn_note, turn);

    if (asked > 0) {
        gs_str_clock(asked, when, sizeof when);
        put(sheet, GS_UI_RECORD_FULL, "Asked ");
        put(sheet, GS_UI_RECORD_FULL, when);
        put(sheet, GS_UI_RECORD_FULL, ".");
    }
    if (answer > 0) {
        gs_str_clock(state->history_at[row], when, sizeof when);
        put(sheet, GS_UI_RECORD_FULL, asked > 0 ? " Answered " : "Answered ");
        put(sheet, GS_UI_RECORD_FULL, when);
        if (state->record_by[row][0] != '\0') {
            put(sheet, GS_UI_RECORD_FULL, " by ");
            put(sheet, GS_UI_RECORD_FULL, state->record_by[row]);
        }
        put(sheet, GS_UI_RECORD_FULL, ".");
    }
    if (sheet[0] != '\0')
        put(sheet, GS_UI_RECORD_FULL, "\n\n");

    if (answer > 0) {
        put(sheet, GS_UI_RECORD_FULL, "WHAT ");
        put(sheet, GS_UI_RECORD_FULL, state->record_by[row][0] != '\0'
                                          ? state->record_by[row]
                                          : "THE MODEL");
        put(sheet, GS_UI_RECORD_FULL, " WORKED OUT BEFORE ANSWERING\n\n");
        if (working != NULL && working[0] != '\0')
            put(sheet, GS_UI_RECORD_FULL, working);
        else
            put(sheet, GS_UI_RECORD_FULL,
                "No working out is kept for this answer. Either the model "
                "wrote none, or the answer was recorded before 13 September "
                "2026, when keeping it began.");
    } else if (turn > 0) {
        int worded = gs_session_answer_count(turn, 1);
        int rows = gs_session_answer_count(turn, 0);
        int noted = note != NULL && note[0] != '\0';

        put(sheet, GS_UI_RECORD_FULL, "WHY NO ANSWER IS SHOWN\n\n");
        if (worded > 0)
            put(sheet, GS_UI_RECORD_FULL,
                "The models answered and every answer was set aside by the "
                "checking. What each model said and why is in the record of "
                "the run below.");
        else if (rows > 0)
            put(sheet, GS_UI_RECORD_FULL, noted
                ? "Every model that was asked came back with nothing, so "
                  "there was no answer to check. What happened to each is "
                  "in the record of the run below."
                : "Every model that was asked came back with nothing, so "
                  "there was no answer to check.");
        else if (noted)
            put(sheet, GS_UI_RECORD_FULL,
                "No model gave an answer. What each was asked and what "
                "happened is in the record of the run below.");
        else
            put(sheet, GS_UI_RECORD_FULL,
                "No model was asked. Either none was ticked in the box, no "
                "inference server was answering, or another run was still "
                "going when the prompt was sent.");
    } else {
        put(sheet, GS_UI_RECORD_FULL,
            "Nothing in the file sits behind this line.");
    }

    if (turn > 0) {
        put(sheet, GS_UI_RECORD_FULL, "\n\nHOW THE RUN WENT\n\n");
        if (note != NULL && note[0] != '\0')
            put(sheet, GS_UI_RECORD_FULL, note);
        else if (answer == 0 && gs_session_answer_count(turn, 0) == 0)
            put(sheet, GS_UI_RECORD_FULL,
                "No run was recorded for this prompt.");
        else
            put(sheet, GS_UI_RECORD_FULL,
                "No record of this run was kept. Before 14 September 2026 "
                "only the window wrote the record, so a run started from "
                "the command line, or one still going when the window "
                "closed, kept none.");
    }
    free(working);
    free(note);
}

void gs_ui_record_shut(gs_ui_state_t *state)
{
    if (state == NULL)
        return;
    state->record_open = -1;
    state->record_scroll = 0;
    state->record_text[0] = '\0';
}

int gs_ui_record_scroll_limit(const gs_ui_state_t *state, int width,
                              int height, int line_height)
{
    gs_ui_rect_t sheet = gs_ui_record_rect(width, height);
    char line[512];
    const char *text;
    int glyph = gs_ui_glyph_width();
    int across;
    int rows;
    int total = 0;

    if (!gs_ui_record_showing(state) || sheet.w <= 0 || glyph <= 0)
        return 0;
    if (line_height <= 0)
        return 0;

    across = (sheet.w - 28) / glyph;
    rows = (sheet.h - 40) / line_height;
    if (across <= 0 || rows <= 0)
        return 0;

    /* Counting the wrapped lines is the same walk the drawing does, at
     * the same width, so the last notch of the wheel lands on the last
     * line rather than past it. */
    text = state->record_text;
    {
        size_t at = 0;

        while (gs_ui_chat_next_line(text, across * glyph, &at, line,
                                    sizeof line))
            total++;
    }

    if (total <= rows)
        return 0;
    return total - rows;
}

gs_ui_rect_t gs_ui_record_rect(int width, int height)
{
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (width < 200 || height < 200)
        return r;
    /* It covers most of the window, since a record of several models is
     * long and reading it is the whole point of opening it. */
    r.x = width / 10;
    r.y = height / 12;
    r.w = width - 2 * r.x;
    r.h = height - 2 * r.y;
    return r;
}

gs_ui_rect_t gs_ui_record_shut_rect(int width, int height)
{
    gs_ui_rect_t panel = gs_ui_record_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (panel.w <= 0)
        return r;
    r.w = 22;
    r.h = 22;
    r.x = panel.x + panel.w - r.w - 10;
    r.y = panel.y + 10;
    return r;
}


void gs_ui_record_compose(unsigned int *px, int w, int h,
                          const gs_ui_state_t *state)
{
    if (px == NULL || state == NULL || !gs_ui_record_showing(state))
        return;

    gs_ui_rect_t sheet = gs_ui_record_rect(w, h);
    gs_ui_rect_t shut = gs_ui_record_shut_rect(w, h);

    if (sheet.w > 0) {
        gs_ui_rect_t rail, grip;
        int rows;
        int total;

        gs_ui_px_rounded(px, w, h, sheet, 6, GS_UI_PANE_FILL,
                         GS_UI_BUBBLE_EDGE);
        gs_ui_px_rounded(px, w, h, shut, 4,
                         state->hover == GS_UI_HIT_RECORD_SHUT
                             ? GS_UI_CHAT_HOVER : GS_UI_CHAT_FACE,
                         GS_UI_CHAT_EDGE);

        /* A record of several models runs past one sheet, so the bar says
         * how much of it is still below. The height of one line comes
         * from the window, which this pass does not hold, so the height
         * the interface uses is assumed here and the limit is worked out
         * against it. */
        rows = (sheet.h - 40) / GS_UI_RECORD_LINE;
        total = rows + gs_ui_record_scroll_limit(state, w, h,
                                                 GS_UI_RECORD_LINE);
        if (rows > 0 &&
            gs_ui_scrollbar(sheet, total, rows, state->record_scroll,
                            &rail, &grip)) {
            gs_ui_px_rounded(px, w, h, rail, 2, GS_UI_SCROLL_RAIL,
                             GS_UI_SCROLL_RAIL);
            gs_ui_px_rounded(px, w, h, grip, 2, GS_UI_SCROLL_GRIP,
                             GS_UI_SCROLL_GRIP);
        }
    }
}

void gs_ui_record_draw(gs_window_t *win, const gs_ui_state_t *state)
{
    gs_ui_rect_t sheet, shut;
    const char *text;
    int w, h;
    int line_height;
    int rows;
    int glyph;
    int across;
    int drawn = 0;
    int n;

    if (win == NULL || state == NULL || !gs_ui_record_showing(state))
        return;

    w = gs_window_width(win);
    h = gs_window_height(win);
    sheet = gs_ui_record_rect(w, h);
    shut = gs_ui_record_shut_rect(w, h);
    text = state->record_text;
    line_height = gs_window_font_height(win) + 3;
    rows = (sheet.h - 40) / line_height;
    glyph = gs_ui_glyph_width();
    across = glyph > 0 ? (sheet.w - 28) / glyph : 0;
    n = state->record_scroll;

    if (sheet.w > 0 && across > 0) {
        char line[512];

        gs_window_text(win, sheet.x + 14,
                       sheet.y + 8 + gs_window_font_ascent(win),
                       "HOW THIS ANSWER WAS ARRIVED AT",
                       GS_UI_HEADER_TEXT);
        {
            int k;

            for (k = 5; k < shut.w - 5; k++) {
                gs_ui_rect_t a;

                a.x = shut.x + k;
                a.y = shut.y + k;
                a.w = 2;
                a.h = 2;
                gs_window_fill(win, a.x, a.y, 2, 2, GS_UI_CHAT_INK);
                gs_window_fill(win, shut.x + k,
                               shut.y + shut.h - 1 - k, 2, 2,
                               GS_UI_CHAT_INK);
            }
        }

        /* One walk of the sheet steps past the lines scrolled away and
         * then draws what fits, where asking for each line by number
         * walked the whole sheet again for every line drawn. */
        {
            size_t at = 0;
            int skipped = 0;

            while (skipped < n &&
                   gs_ui_chat_next_line(text, across * glyph, &at, line,
                                        sizeof line))
                skipped++;
            while (drawn < rows &&
                   gs_ui_chat_next_line(text, across * glyph, &at, line,
                                        sizeof line)) {
                gs_window_text(win, sheet.x + 14,
                               sheet.y + 34 + gs_window_font_ascent(win) +
                               drawn * line_height, line, GS_UI_ROW_TEXT);
                drawn++;
            }
        }
    }
}
