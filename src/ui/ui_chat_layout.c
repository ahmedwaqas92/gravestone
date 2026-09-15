/* ui_chat_layout.c
 *
 * Where each line of the history sits, how tall it stands, and how far the
 * reader has scrolled.
 *
 * Every bubble is measured by wrapping its text to the width of the panel,
 * and the measurements are kept until the lines or the window change. They
 * used to be taken again for every bubble on every repaint and on every
 * movement of the pointer, and each line was measured one character at a
 * time from its start, so sixteen long answers took two seconds to paint
 * and sixty four took twenty eight. One wrap of each line now serves
 * every question asked of the layout until something changes.
 *
 * Kept apart from ui_chat.c so neither file grows past what fits in a head.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "window.h"

#include <string.h>

#define PAD 10

gs_ui_rect_t gs_ui_chat_history_rect(int width, int height)
{
    gs_ui_rect_t panel = gs_ui_chat_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};
    /* Room for the heading above, and a gap above the box below. */
    int head = 8 + 13 + 8;

    if (panel.w <= 0)
        return r;
    r.x = panel.x + PAD;
    /* The strip on the right is the scroll bar's, whether or not the
     * history is long enough to need one. Making room only once a bar
     * appeared would narrow every bubble, rewrap them all, and change the
     * very height that decided a bar was needed. */
    r.w = panel.w - 2 * PAD - GS_UI_CHAT_BAR_ROOM;
    r.y = panel.y + head;
    r.h = panel.h - GS_UI_CHAT_COMPOSER - head - 8;
    if (r.w <= 0 || r.h <= 0) {
        r.w = 0;
        r.h = 0;
    }
    return r;
}

gs_ui_rect_t gs_ui_chat_bar_box(int width, int height)
{
    gs_ui_rect_t seen = gs_ui_chat_history_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (seen.w <= 0)
        return r;
    /* The bar takes the right hand end of a box this wide, which lands it
     * in the strip beside the bubbles with a clear gap before them. */
    r.x = seen.x + seen.w;
    r.y = seen.y;
    r.w = GS_UI_CHAT_BAR_ROOM + PAD;
    r.h = seen.h;
    return r;
}

int gs_ui_chat_bubble_columns(int width, int height)
{
    gs_ui_rect_t seen = gs_ui_chat_history_rect(width, height);
    int room;

    if (seen.w <= 0)
        return 0;
    /* A bubble takes at most part of the width, so the side it is not on
     * stays clear and the two sides read apart. */
    room = seen.w * GS_UI_BUBBLE_SHARE / 100 - 2 * GS_UI_BUBBLE_PAD_X;
    return room > 0 ? room : 0;
}

int gs_ui_chat_composer_skip(const char *text, int room, int lines)
{
    char piece[256];
    size_t at = 0;
    size_t len;
    int total = 0;

    if (text == NULL || room <= 0 || lines <= 0)
        return 0;
    while (gs_ui_chat_next_line(text, room, &at, piece, sizeof piece))
        total++;
    /* The walk steps over a break at the very end without a line after
     * it, and the caret stands on that line. */
    len = strlen(text);
    if (len > 0 && text[len - 1] == '\n')
        total++;
    return total > lines ? total - lines : 0;
}

int gs_ui_chat_next_line(const char *text, int room, size_t *at,
                         char *out, size_t cap)
{
    size_t start;
    size_t take = 0;
    size_t space = 0;      /* the last place the line could break */
    int broke = 0;
    int wide = 0;

    if (text == NULL || at == NULL || out == NULL || cap == 0 || room <= 0)
        return 0;
    out[0] = '\0';
    start = *at;
    if (text[start] == '\0')
        return 0;

    /* Letters differ in width, so the line grows one character at a time
     * until the room runs out. The width is carried along as it grows
     * rather than measured again from the start of the line, which made
     * every line cost the square of its length. */
    while (text[start + take] != '\0' && take + 1 < cap) {
        char c = text[start + take];
        int step;

        if (c == '\n') {
            broke = 1;
            break;
        }
        step = gs_window_face_width_n(text + start + take, 1);
        if (wide + step > room)
            break;
        wide += step;
        if (c == ' ')
            space = take;
        take++;
    }

    /* A line that ran out of room inside a word pulls back to the last
     * space. A single word wider than the line has nowhere to pull back
     * to and is cut where it is. */
    if (!broke && text[start + take] != '\0' && text[start + take] != ' ' &&
        space > 0)
        take = space;

    /* A room narrower than one letter would otherwise take nothing and
     * never move, so at least one character goes on every line. */
    if (!broke && take == 0)
        take = 1;

    memcpy(out, text + start, take);
    out[take] = '\0';

    /* Step over whatever ended the line, so the next one starts on a word
     * rather than on the space in front of it. */
    *at = start + take;
    while (text[*at] == ' ')
        (*at)++;
    if (text[*at] == '\n')
        (*at)++;
    return 1;
}

int gs_ui_chat_line(const char *text, int room, int index,
                    char *out, size_t cap)
{
    size_t at = 0;
    int line;

    if (text == NULL || out == NULL || cap == 0 || room <= 0 || index < 0)
        return 0;
    out[0] = '\0';
    for (line = 0; line <= index; line++)
        if (!gs_ui_chat_next_line(text, room, &at, out, cap)) {
            out[0] = '\0';
            return 0;
        }
    return 1;
}

/* Every bubble of one page, measured once. */
typedef struct {
    const gs_ui_state_t *state;
    long long gen;
    int w;
    int h;
    int count;
    int total;                        /* every bubble and every gap */
    int top[GS_UI_HISTORY_MAX];       /* from the top of the whole page */
    int tall[GS_UI_HISTORY_MAX];
    int wide[GS_UI_HISTORY_MAX];
} layout_t;

static layout_t kept;

/* Wraps one line of the history and says how tall and how wide its bubble
 * stands. */
static void measure(const gs_ui_state_t *state, int index, int room,
                    int *tall, int *wide)
{
    char line[GS_UI_HISTORY_TEXT];
    size_t at = 0;
    int lines = 0;
    int longest = 0;
    int name;

    while (gs_ui_chat_next_line(state->history[index], room, &at, line,
                                sizeof line)) {
        int w = gs_window_face_width(line);

        if (w > longest)
            longest = w;
        lines++;
    }
    if (lines == 0)
        lines = 1;                    /* an empty line still takes a line */

    *tall = GS_UI_BUBBLE_HEAD + 2 * GS_UI_BUBBLE_PAD_Y +
            lines * GS_UI_BUBBLE_LINE;

    /* The name above has to fit as well, or it would hang over the edge.
     * It is drawn in the screen's own letters, which are a fixed width. */
    name = 17 * gs_ui_glyph_width();
    if (name > longest)
        longest = name;
    *wide = longest + 2 * GS_UI_BUBBLE_PAD_X;
}

static const layout_t *layout_of(const gs_ui_state_t *state, int w, int h)
{
    int room = gs_ui_chat_bubble_columns(w, h);
    int i;
    int at = 0;

    /* A measurement taken for these very lines at this very size is still
     * true. Lines put in by hand, as a test does, carry no generation and
     * are measured afresh every time rather than trusted. */
    if (state->history_gen != 0 && kept.state == state &&
        kept.gen == state->history_gen && kept.w == w && kept.h == h &&
        kept.count == state->history_count)
        return &kept;

    kept.state = state;
    kept.gen = state->history_gen;
    kept.w = w;
    kept.h = h;
    kept.count = state->history_count;
    kept.total = 0;
    if (room <= 0)
        return &kept;

    for (i = 0; i < state->history_count && i < GS_UI_HISTORY_MAX; i++) {
        measure(state, i, room, &kept.tall[i], &kept.wide[i]);
        kept.top[i] = at;
        at += kept.tall[i] + GS_UI_BUBBLE_GAP;
    }
    kept.total = at;
    return &kept;
}

int gs_ui_chat_bubble_lines(const gs_ui_state_t *state, int width,
                            int height, int index)
{
    const layout_t *l;

    if (state == NULL || index < 0 || index >= state->history_count)
        return 0;
    if (gs_ui_chat_bubble_columns(width, height) <= 0)
        return 0;
    l = layout_of(state, width, height);
    return (l->tall[index] - GS_UI_BUBBLE_HEAD - 2 * GS_UI_BUBBLE_PAD_Y) /
           GS_UI_BUBBLE_LINE;
}

int gs_ui_chat_content_height(const gs_ui_state_t *state, int width,
                              int height)
{
    if (state == NULL || state->history_count <= 0)
        return 0;
    if (gs_ui_chat_bubble_columns(width, height) <= 0)
        return 0;
    return layout_of(state, width, height)->total;
}

int gs_ui_chat_scroll_reach(const gs_ui_state_t *state, int width,
                            int height)
{
    gs_ui_rect_t seen = gs_ui_chat_history_rect(width, height);
    int reach;

    if (seen.h <= 0)
        return 0;
    reach = gs_ui_chat_content_height(state, width, height) - seen.h;
    return reach > 0 ? reach : 0;
}

/* How far back the view sits, held inside what the page allows, so a page
 * that shrank never leaves the reader looking at nothing. */
static int back_of(const gs_ui_state_t *state, int width, int height)
{
    int reach = gs_ui_chat_scroll_reach(state, width, height);
    int back = state->history_back;

    if (back < 0)
        back = 0;
    if (back > reach)
        back = reach;
    return back;
}

int gs_ui_chat_scroll_by(gs_ui_state_t *state, int width, int height,
                         int pixels)
{
    int before;
    int after;

    if (state == NULL)
        return 0;
    before = back_of(state, width, height);
    after = before - pixels;
    state->history_back = after;
    after = back_of(state, width, height);
    state->history_back = after;
    return after != before;
}

gs_ui_rect_t gs_ui_chat_bubble_rect(const gs_ui_state_t *state, int width,
                                    int height, int index)
{
    gs_ui_rect_t seen = gs_ui_chat_history_rect(width, height);
    gs_ui_rect_t r = {0, 0, 0, 0};
    const layout_t *l;

    if (state == NULL || seen.w <= 0 || index < 0 ||
        index >= state->history_count || index >= GS_UI_HISTORY_MAX)
        return r;
    if (gs_ui_chat_bubble_columns(width, height) <= 0)
        return r;

    l = layout_of(state, width, height);
    if (l->total <= 0)
        return r;

    r.h = l->tall[index];
    r.w = l->wide[index];
    if (r.w > seen.w)
        r.w = seen.w;

    /* The page stands with its newest line against the box, and scrolling
     * back lowers the whole page so older lines come down into view. */
    r.y = seen.y + seen.h - l->total + back_of(state, width, height) +
          l->top[index];

    /* The person on the right, the program on the left, and a line that
     * moves between pages across the middle. */
    if (state->history_kind[index] != GS_UI_LINE_SAID)
        r.x = seen.x + (seen.w - r.w) / 2;
    else
        r.x = state->history_mine[index] ? seen.x + seen.w - r.w : seen.x;

    /* Anything wholly outside the area is not there to be drawn. */
    if (r.y + r.h <= seen.y || r.y >= seen.y + seen.h) {
        r.w = 0;
        r.h = 0;
    }
    return r;
}

gs_ui_rect_t gs_ui_chat_note_rect(const gs_ui_state_t *state, int width,
                                  int height, int index)
{
    gs_ui_rect_t seen = gs_ui_chat_history_rect(width, height);
    gs_ui_rect_t bubble;
    gs_ui_rect_t r = {0, 0, 0, 0};

    if (state == NULL || index < 0 || index >= state->history_count)
        return r;
    /* Every line that came back carries a record, whether an answer or the
     * reason no answer is shown. The person's own question carries none,
     * since they know what they wrote, and the lines that move between
     * pages are controls rather than things said. */
    if (state->history_kind[index] != GS_UI_LINE_SAID ||
        state->history_mine[index] || state->record_turn[index] <= 0)
        return r;

    bubble = gs_ui_chat_bubble_rect(state, width, height, index);
    if (bubble.w <= 0)
        return r;

    r.w = 16;
    r.h = 16;
    r.x = bubble.x + bubble.w - r.w - 6;
    r.y = bubble.y + 4;

    /* A mark scrolled past either edge cannot be pressed, since the part
     * of the bubble holding it is not on the screen. */
    if (r.y < seen.y || r.y + r.h > seen.y + seen.h) {
        r.w = 0;
        r.h = 0;
    }
    return r;
}
