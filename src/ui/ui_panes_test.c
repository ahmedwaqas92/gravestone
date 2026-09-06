/* ui_panes_test.c
 *
 * Adversarial pass over the chooser and the three panes. Everything here
 * runs against plain memory, so it needs no display.
 *
 * Two questions are asked. Does the geometry stay inside the window at
 * every size a window manager might hand back, and does the drawing ever
 * write outside the buffer it was given when the state holds numbers that
 * no sane run would produce.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "catalogue.h"
#include "detect.h"
#include "library.h"
#include "log.h"
#include "str.h"
#include "window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define GUARD 64
#define GUARD_VALUE 0xFEEDFACEu

static int failures;
static int checks;

static void check(int condition, const char *what)
{
    checks++;
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

/* A small repeatable generator, so a failure can be looked at again. */
static unsigned int seed = 20260902u;

static int pick(int low, int high)
{
    seed = seed * 1664525u + 1013904223u;
    if (high <= low)
        return low;
    return low + (int)(seed % (unsigned int)(high - low + 1));
}

static int overlaps(gs_ui_rect_t a, gs_ui_rect_t b)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0)
        return 0;
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

static int inside(gs_ui_rect_t r, int w, int h)
{
    if (r.w <= 0 || r.h <= 0)
        return 1;
    return r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h;
}

static void test_geometry_at_every_size(void)
{
    int bad_bounds = 0, bad_overlap = 0, bad_negative = 0, bad_bar = 0;
    int worst_w = 0, worst_h = 0;
    int trials;

    printf("geometry at sizes a window manager might hand back\n");

    for (trials = 0; trials < 4000; trials++) {
        int w = pick(0, 2400);
        int h = pick(0, 1800);
        gs_ui_rect_t chooser = gs_ui_chooser_rect(w, h);
        gs_ui_rect_t left = gs_ui_left_rect(w, h);
        gs_ui_rect_t mid = gs_ui_mid_rect(w, h);
        gs_ui_rect_t right = gs_ui_right_rect(w, h);
        gs_ui_rect_t drop = gs_ui_drop_rect(w, h, pick(0, 4000));

        if (chooser.w < 0 || left.w < 0 || mid.w < 0 || right.w < 0 ||
            drop.w < 0 || drop.h < 0 || left.h < 0) {
            bad_negative++;
            worst_w = w;
            worst_h = h;
        }
        if (!inside(left, w, h) || !inside(mid, w, h) ||
            !inside(right, w, h) || !inside(chooser, w, h)) {
            bad_bounds++;
            worst_w = w;
            worst_h = h;
        }
        if (overlaps(left, mid) || overlaps(mid, right) ||
            overlaps(left, right) || overlaps(chooser, left)) {
            bad_overlap++;
            worst_w = w;
            worst_h = h;
        }
        if (left.h > 0 && left.y < GS_UI_BAR_HEIGHT)
            bad_bar++;
        if (chooser.h > 0 && chooser.y < GS_UI_BAR_HEIGHT)
            bad_bar++;
    }

    if (bad_bounds || bad_overlap || bad_negative || bad_bar)
        printf("        worst size seen was %dx%d\n", worst_w, worst_h);
    check(bad_negative == 0, "no rectangle ever comes back with a negative "
                             "width or height");
    check(bad_bounds == 0, "every pane stays inside the window");
    check(bad_overlap == 0, "the panes never sit on top of one another");
    check(bad_bar == 0, "nothing under the bar climbs into it");
}

static void test_hit_test_never_lies(void)
{
    int bad_row = 0, bad_outside = 0;
    int trials;

    printf("what the hit test answers with nonsense state\n");

    for (trials = 0; trials < 20000; trials++) {
        gs_ui_state_t s;
        int w = pick(0, 2400);
        int h = pick(0, 1800);
        int x = pick(-50, 2500);
        int y = pick(-50, 1900);
        int row = -7;
        gs_ui_hit_t hit;

        memset(&s, 0, sizeof s);
        s.mounted = pick(0, 1);
        s.chooser_open = pick(0, 1);
        s.model_count = pick(-10, 4000);
        s.disk_count = pick(-10, 40);
        s.library_count = pick(-10, 600);
        s.model_scroll = pick(-20, 4000);
        s.library_scroll = pick(-20, 600);
        s.model_sel = pick(-5, 4000);
        s.disk_sel = pick(-5, 40);
        s.category_count = pick(-5, 30);
        s.category_sel = pick(-5, 30);
        s.confirm_step = pick(-2, GS_UI_CONFIRM_STEPS + 2);
        s.confirm_dir = pick(-1, 1);
        s.confirm_row = pick(-5, 600);
        s.confirm_busy = pick(0, 1);
        s.confirm_add = pick(0, 1);
        s.spin_phase = pick(-100, 100000);

        hit = gs_ui_hit_test(&s, w, h, x, y, &row);

        /* While the confirm box is up nothing else may answer, its
         * buttons only answer once it has finished growing, and a busy
         * box answers nobody at all. */
        if (s.confirm_step > 0 || s.confirm_dir != 0) {
            if (hit != GS_UI_HIT_NONE && hit != GS_UI_HIT_CONFIRM_OK &&
                hit != GS_UI_HIT_CONFIRM_NO)
                bad_row++;
            if (s.confirm_step != GS_UI_CONFIRM_STEPS &&
                hit != GS_UI_HIT_NONE)
                bad_row++;
            if (s.confirm_busy && hit != GS_UI_HIT_NONE)
                bad_row++;
        }

        /* A row answer has to name a row that exists. */
        if (hit == GS_UI_HIT_DROP_ROW) {
            if (row < 0 || row >= s.category_count)
                bad_row++;
        } else if (hit == GS_UI_HIT_LEFT_ADD) {
            if (row < 0 || row >= s.model_count)
                bad_row++;
        } else if (hit == GS_UI_HIT_LEFT_ROW) {
            if (row < 0 || row >= s.model_count)
                bad_row++;
        } else if (hit == GS_UI_HIT_MID_ROW) {
            if (row < 0 || row >= s.disk_count)
                bad_row++;
        } else if (hit == GS_UI_HIT_RIGHT_ROW ||
                   hit == GS_UI_HIT_RIGHT_REMOVE) {
            if (row < 0 || row >= s.library_count)
                bad_row++;
        } else if (hit == GS_UI_HIT_CONFIRM_OK ||
                   hit == GS_UI_HIT_CONFIRM_NO) {
            if (row != -1)
                bad_row++;
        } else if (row != -1) {
            /* Anything that is not a row leaves the index alone. */
            bad_row++;
        }

        if ((x < 0 || y < 0 || x >= w || y >= h) && hit != GS_UI_HIT_NONE)
            bad_outside++;
    }

    check(bad_row == 0, "a row answer always names a row that exists");
    check(bad_outside == 0, "a point outside the window lands on nothing");
}

static unsigned int *fenced(int w, int h, unsigned int **base_out)
{
    size_t n = (size_t)w * (size_t)h;
    unsigned int *base = malloc((n + 2 * GUARD) * sizeof *base);
    size_t i;

    if (base == NULL)
        return NULL;
    for (i = 0; i < n + 2 * GUARD; i++)
        base[i] = GUARD_VALUE;
    *base_out = base;
    return base + GUARD;
}

static int fence_intact(const unsigned int *base, int w, int h)
{
    size_t n = (size_t)w * (size_t)h;
    size_t i;

    for (i = 0; i < GUARD; i++)
        if (base[i] != GUARD_VALUE)
            return 0;
    for (i = 0; i < GUARD; i++)
        if (base[GUARD + n + i] != GUARD_VALUE)
            return 0;
    return 1;
}

static void test_drawing_stays_in_the_buffer(void)
{
    int broken = 0;
    int trials;

    printf("drawing with counts and scrolls that no real run produces\n");

    for (trials = 0; trials < 400; trials++) {
        gs_ui_state_t s;
        unsigned int *base, *px;
        int w = pick(1, 900);
        int h = pick(1, 700);

        memset(&s, 0, sizeof s);
        s.mounted = pick(0, 1);
        s.chooser_open = pick(0, 1);
        s.model_count = pick(-10, 4000);
        s.disk_count = pick(-10, 40);
        s.library_count = pick(-10, 600);
        s.model_scroll = pick(-20, 4000);
        s.library_scroll = pick(-20, 600);
        s.model_sel = pick(-5, 4000);
        s.disk_sel = pick(-5, 40);
        s.hover = (gs_ui_hit_t)pick(0, 11);
        s.hover_row = pick(-5, 4000);
        s.category_count = pick(-5, 30);
        s.category_sel = pick(-5, 30);
        s.confirm_step = pick(-2, GS_UI_CONFIRM_STEPS + 2);
        s.confirm_dir = pick(-1, 1);
        s.confirm_row = pick(-5, 600);
        s.confirm_busy = pick(0, 1);
        s.confirm_add = pick(0, 1);
        s.spin_phase = pick(-100, 100000);
        s.busy_percent = pick(-200, 300);

        px = fenced(w, h, &base);
        if (px == NULL)
            continue;
        gs_ui_compose(px, w, h, &s);
        if (!fence_intact(base, w, h)) {
            printf("        the fence broke at %dx%d\n", w, h);
            broken++;
        }
        free(base);
    }

    check(broken == 0, "nothing is written outside the buffer");
}

/* The chooser and the panes have to answer the same way twice, since a
 * repaint that differed from the last one would flicker. */
static void test_repeatable(void)
{
    const int w = 980, h = 640;
    size_t n = (size_t)w * (size_t)h;
    unsigned int *a = malloc(n * sizeof *a);
    unsigned int *b = malloc(n * sizeof *b);
    gs_ui_state_t s;

    printf("the same state paints the same picture\n");

    if (a == NULL || b == NULL) {
        check(0, "buffers allocated");
        free(a);
        free(b);
        return;
    }

    memset(&s, 0, sizeof s);
    s.mounted = 1;
    s.chooser_open = 1;
    s.model_count = 300;
    s.disk_count = 4;
    s.library_count = 9;
    s.model_sel = 7;
    s.disk_sel = 2;
    s.category_count = 6;
    s.category_sel = 3;
    s.hover = GS_UI_HIT_LEFT_ROW;
    s.hover_row = 5;

    gs_ui_compose(a, w, h, &s);
    gs_ui_compose(b, w, h, &s);
    check(memcmp(a, b, n * sizeof *a) == 0,
          "two passes over the same state agree everywhere");

    free(a);
    free(b);
}

/* The chooser drives the left pane, so the counts it shows and the rows
 * the pane holds have to be the same number every time. */
static void test_category_filter(void)
{
    gs_ui_state_t s;
    gs_detect_report_t machine;
    int all, sum = 0;
    int i;
    int mismatch = 0;

    printf("filtering the left pane by category\n");

    memset(&s, 0, sizeof s);
    memset(&machine, 0, sizeof machine);
    /* Ten terabytes of system memory and no card, so every row down to
     * deepseek-v3 at F16 is runnable and the filter is the only thing
     * narrowing the list. */
    machine.ram_total_bytes = 10000000000000LL;

    gs_ui_panes_refresh(&s, &machine);

    all = s.model_count;
    printf("        every row runnable on this made up machine: %d of %d\n",
           all, gs_catalogue_count());
    check(all == gs_catalogue_count(),
          "with ten terabytes of memory the pane holds every row");
    check(s.category_count == (int)GS_KIND_COUNT + 1,
          "the chooser offers one row per kind plus All models");
    check(s.category_sel == 0, "it starts on All models");
    check(gs_ui_panes_category_total(0) == all,
          "the All models count matches the pane");

    for (i = 1; i < s.category_count; i++) {
        int want = gs_ui_panes_category_total(i);
        int j;
        int wrong_kind = 0;

        gs_ui_panes_pick_category(&s, i);
        printf("        %-18s chooser says %d, pane holds %d\n",
               gs_ui_panes_category_name(i), want, s.model_count);
        if (s.model_count != want)
            mismatch++;
        sum += want;

        /* Every row left in the pane has to be of that kind. */
        for (j = 0; j < s.model_count; j++) {
            const gs_catalogue_entry_t *e = gs_ui_panes_model(j);

            if (e == NULL || (int)gs_catalogue_kind(e) != i - 1)
                wrong_kind++;
        }
        if (wrong_kind != 0) {
            printf("        %d rows of the wrong kind survived\n",
                   wrong_kind);
            mismatch++;
        }
    }

    check(mismatch == 0,
          "each category shows exactly the rows it counted");
    check(sum == all, "the categories add up to the whole list");

    /* Out of range choices fall back rather than reading past the array. */
    gs_ui_panes_pick_category(&s, -4);
    check(s.category_sel == 0 && s.model_count == all,
          "a category below zero falls back to All models");
    gs_ui_panes_pick_category(&s, 900);
    check(s.category_sel == 0 && s.model_count == all,
          "a category past the end falls back to All models");
    check(gs_ui_panes_model(s.model_count) == NULL,
          "one past the last row gives nothing");
    check(gs_ui_panes_model(-1) == NULL, "a negative row gives nothing");
    gs_ui_panes_pick_category(NULL, 1);
    check(1, "picking into a null state does not crash");

    gs_catalogue_release();
}

/* Text drawn after the open list would sit on top of it, so anything the
 * list covers has to stay undrawn. These are the covering rules alone,
 * with no display involved. */
static void test_covering(void)
{
    const int w = 980, h = 640;
    gs_ui_state_t s;
    gs_ui_rect_t drop, left, below, beside;

    printf("what the open list covers\n");

    memset(&s, 0, sizeof s);
    s.category_count = 6;
    left = gs_ui_left_rect(w, h);

    check(!gs_ui_text_covered(&s, w, h, left),
          "a closed list covers nothing");

    s.chooser_open = 1;
    drop = gs_ui_drop_rect(w, h, s.category_count);
    check(drop.h > 0, "the open list has height to cover with");

    check(gs_ui_text_covered(&s, w, h, left),
          "the left pane is covered while the list is open");

    below = left;
    below.y = drop.y + drop.h + 2;
    below.h = GS_UI_ROW_HEIGHT;
    check(!gs_ui_text_covered(&s, w, h, below),
          "a row below the list is left alone");

    beside = left;
    beside.y = drop.y;
    beside.h = 1;
    beside.x = 0;
    beside.w = drop.x - 1;
    check(!gs_ui_text_covered(&s, w, h, beside),
          "a rectangle beside the list is left alone");

    check(!gs_ui_text_covered(NULL, w, h, left),
          "a null state covers nothing");
    check(!gs_ui_text_covered(&s, w, h, (gs_ui_rect_t){0, 0, 0, 0}),
          "an empty rectangle is never covered");
}

/* Paints a real window with the list open and reads the screen back.
 * Every sampled point inside the list, chosen where no category text sits
 * and where pane text would have been drawn, has to match the composed
 * buffer exactly. Before the covering rule, the mount points of the middle
 * pane bled through the list right at these points. */
static void test_list_covers_on_screen(void)
{
    const int open_w = 980, open_h = 640;
    gs_window_t *win;
    gs_window_event_t drain;
    gs_ui_state_t st;
    gs_detect_report_t machine;
    unsigned int *expected;
    gs_ui_rect_t drop, mid;
    int w, h, rounds, slot;
    int tried = 0, matches = 0;

    if (!gs_window_display_available()) {
        printf("  skip  the list on a real screen (the display is refusing "
               "connections)\n");
        return;
    }

    printf("the list on a real screen\n");

    memset(&st, 0, sizeof st);
    memset(&machine, 0, sizeof machine);
    machine.ram_total_bytes = 1000000000000LL;
    machine.disk_count = 3;
    for (rounds = 0; rounds < 3; rounds++) {
        snprintf(machine.disk[rounds].mount,
                 sizeof machine.disk[rounds].mount,
                 "/mnt/models-%c", 'a' + rounds);
        machine.disk[rounds].total_bytes = 100000000000LL;
        machine.disk[rounds].free_bytes = 50000000000LL;
    }
    gs_ui_panes_refresh(&st, &machine);
    st.chooser_open = 1;
    st.mounted = 1;

    win = gs_window_open("Gravestone cover test", open_w, open_h);
    check(win != NULL, "window opened");
    if (win == NULL)
        return;

    for (rounds = 0; rounds < 20; rounds++)
        if (gs_window_wait_event(win, &drain, 50) == 1 &&
            drain.kind == GS_WINDOW_EVENT_EXPOSE)
            break;

    check(gs_ui_paint(win, &st) == GS_OK, "painted with the list open");

    w = gs_window_width(win);
    h = gs_window_height(win);
    expected = malloc((size_t)w * (size_t)h * sizeof *expected);
    if (expected == NULL) {
        gs_window_close(win);
        return;
    }
    gs_ui_compose(expected, w, h, &st);

    drop = gs_ui_drop_rect(w, h, st.category_count);
    mid = gs_ui_mid_rect(w, h);

    /* The middle pane's x range inside the list holds no category glyphs,
     * since the names sit at the left edge and the counts at the right,
     * while the made up mount names would have put text exactly here. */
    for (slot = 0; slot < st.category_count; slot++) {
        int y = drop.y + 4 + slot * GS_UI_ROW_HEIGHT + GS_UI_ROW_HEIGHT / 2;
        int x;

        if (y >= drop.y + drop.h)
            break;
        for (x = mid.x + 4; x < mid.x + mid.w - 60; x += 9) {
            unsigned int got = 0;
            unsigned int want = expected[(size_t)y * (size_t)w + (size_t)x];
            int attempt, agreed = 0;

            for (attempt = 0; attempt < 10 && !agreed; attempt++) {
                if (gs_window_read_pixel(win, x, y, &got) != GS_OK)
                    break;
                if (got == want)
                    agreed = 1;
                else
                    gs_window_wait_event(win, &drain, 25);
            }
            if (attempt == 0)
                continue;
            tried++;
            if (agreed)
                matches++;
            else if (tried - matches == 1)
                printf("        (%d,%d) screen 0x%06x, buffer 0x%06x\n",
                       x, y, got, want);
        }
    }

    printf("        %d of %d sampled points inside the list match\n",
           matches, tried);
    if (tried == 0) {
        printf("  skip  no point could be read back\n");
    } else {
        check(matches == tried,
              "nothing bleeds through the open list on screen");
    }
    check(gs_window_error_count(win) == 0, "the server was upset nowhere");

    free(expected);
    gs_window_close(win);
    gs_ui_release();
    gs_catalogue_release();
}

/* A model on disk pointing back at its row in the list, attacked from
 * every angle that must not move the selection. */
static void test_locate(void)
{
    gs_ui_state_t s;
    gs_detect_report_t machine;
    gs_library_entry_t owned;
    const gs_catalogue_entry_t *hit;
    int before_cat, before_sel, i;
    long long real_bytes = 0;
    char real_name[160] = {0};
    char aliased[160];

    printf("a disk model finding its row in the list\n");

    memset(&s, 0, sizeof s);
    memset(&machine, 0, sizeof machine);
    machine.ram_total_bytes = 10000000000000LL;
    gs_ui_panes_refresh(&s, &machine);

    if (s.model_count == 0) {
        printf("  skip  no snapshot to search\n");
        return;
    }

    /* A real row, taken from the snapshot itself so the test survives
     * every future refetch. */
    for (i = 0; i < gs_catalogue_count(); i++) {
        const gs_catalogue_entry_t *e = gs_catalogue_at(i);

        if (strchr(e->file, ':') != NULL) {
            gs_str_copy(real_name, sizeof real_name, e->file);
            real_bytes = e->bytes;
            break;
        }
    }

    memset(&owned, 0, sizeof owned);
    gs_str_copy(owned.name, sizeof owned.name, real_name);
    owned.bytes = real_bytes;
    check(gs_ui_panes_locate(&s, &owned) == GS_OK,
          "an exact name is found");
    hit = gs_ui_panes_model(s.model_sel);
    check(hit != NULL && strcmp(hit->file, real_name) == 0,
          "and the selected row is that very model");
    check(s.category_sel >= 1 && s.category_sel < s.category_count,
          "the chooser moved to the model's own category");
    check(s.model_sel >= 0 && s.model_sel < s.model_count,
          "the selection sits inside the filtered list");
    check(s.model_scroll >= 0 && s.model_scroll <= s.model_sel,
          "the scroll keeps the selection reachable");

    /* The alias case, latest standing in for the explicit tag, matched
     * through the model half plus the exact size. */
    snprintf(aliased, sizeof aliased, "%.*s:latest",
             (int)(strchr(real_name, ':') - real_name), real_name);
    gs_str_copy(owned.name, sizeof owned.name, aliased);
    owned.bytes = real_bytes;
    check(gs_ui_panes_locate(&s, &owned) == GS_OK,
          "an alias with the right size is found");
    hit = gs_ui_panes_model(s.model_sel);
    check(hit != NULL && strcmp(hit->file, real_name) == 0,
          "and lands on the explicit row");

    /* Everything below must leave the selection exactly where it is. */
    before_cat = s.category_sel;
    before_sel = s.model_sel;

    owned.bytes = real_bytes + 1;
    check(gs_ui_panes_locate(&s, &owned) == GS_ERR,
          "an alias one byte off is refused");
    gs_str_copy(owned.name, sizeof owned.name, "no-such-model:latest");
    owned.bytes = real_bytes;
    check(gs_ui_panes_locate(&s, &owned) == GS_ERR,
          "a name from nowhere is refused even with a familiar size");
    gs_str_copy(owned.name, sizeof owned.name, "own-download.gguf");
    owned.bytes = 55;
    check(gs_ui_panes_locate(&s, &owned) == GS_ERR,
          "a hand downloaded file with no colon is refused");
    owned.name[0] = '\0';
    check(gs_ui_panes_locate(&s, &owned) == GS_ERR_ARG,
          "an empty name is refused");
    check(gs_ui_panes_locate(&s, NULL) == GS_ERR_ARG,
          "a null entry is refused");
    check(gs_ui_panes_locate(NULL, &owned) == GS_ERR_ARG,
          "a null state is refused");

    check(s.category_sel == before_cat && s.model_sel == before_sel,
          "every refusal left the selection untouched");

    gs_catalogue_release();
}

/* The search field, attacked with case games, overflow, garbage bytes
 * and a term that matches nothing. */
static void test_search(void)
{
    gs_ui_state_t s;
    gs_detect_report_t machine;
    gs_ui_rect_t box, left;
    const int w = 980, h = 640;
    int full, i, by_hand;
    const char *term = "qwen";

    printf("the search field under attack\n");

    memset(&s, 0, sizeof s);
    memset(&machine, 0, sizeof machine);
    machine.ram_total_bytes = 10000000000000LL;
    gs_ui_panes_refresh(&s, &machine);
    full = s.model_count;
    if (full == 0) {
        printf("  skip  no snapshot to search\n");
        return;
    }

    box = gs_ui_search_rect(w, h);
    left = gs_ui_left_rect(w, h);
    check(box.w > 0, "the field exists at the normal window size");
    check(box.x + box.w <= left.x + left.w - 40,
          "and stops short of the count instead of spilling");
    check(box.y >= left.y && box.y + box.h <= left.y + GS_UI_PANE_HEADER,
          "and stays inside the header band");
    check(gs_ui_search_rect(120, 200).w == 0,
          "a window too narrow gets no field at all");

    for (i = 0; term[i] != '\0'; i++)
        check(gs_ui_search_type(&s, term[i]) == 1,
              "a typed letter changes the text");
    gs_ui_panes_pick_category(&s, 0);

    by_hand = 0;
    for (i = 0; i < gs_catalogue_count(); i++) {
        const gs_catalogue_entry_t *e = gs_catalogue_at(i);

        if (strstr(e->file, "qwen") != NULL ||
            strstr(e->file, "Qwen") != NULL ||
            strstr(e->file, "QWEN") != NULL)
            by_hand++;
    }
    printf("        qwen narrows %d rows to %d, counted %d by hand\n",
           full, s.model_count, by_hand);
    check(s.model_count == by_hand,
          "the filter keeps exactly the rows carrying the term");
    for (i = 0; i < s.model_count; i++)
        if (strstr(gs_ui_panes_model(i)->file, "qwen") == NULL)
            break;
    check(i == s.model_count, "every kept row really carries it");

    /* The same search in capitals finds the same rows. */
    s.search[0] = '\0';
    for (i = 0; i < 4; i++)
        gs_ui_search_type(&s, "QWEN"[i]);
    gs_ui_panes_pick_category(&s, 0);
    check(s.model_count == by_hand, "capitals find the same rows");

    /* Category and search narrow together. */
    gs_ui_panes_pick_category(&s, (int)GS_KIND_CODE + 1);
    for (i = 0; i < s.model_count; i++) {
        const gs_catalogue_entry_t *e = gs_ui_panes_model(i);

        if ((int)gs_catalogue_kind(e) != (int)GS_KIND_CODE ||
            strstr(e->file, "qwen") == NULL)
            break;
    }
    check(s.model_count > 0 && i == s.model_count,
          "a category and a search narrow together");

    /* A term matching nothing empties the pane and upsets nothing. */
    gs_ui_panes_pick_category(&s, 0);
    s.search[0] = '\0';
    for (i = 0; i < 6; i++)
        gs_ui_search_type(&s, "zq9#x!"[i]);
    gs_ui_panes_pick_category(&s, 0);
    check(s.model_count == 0, "a hopeless term leaves zero rows");
    check(s.model_sel == -1, "and nothing selected");
    check(gs_ui_panes_model(0) == NULL, "and no row to read");

    /* Backspace walks all the way home. */
    for (i = 0; i < 6; i++)
        gs_ui_search_type(&s, 8);
    gs_ui_panes_pick_category(&s, 0);
    check(s.search[0] == '\0', "backspace empties the text");
    check(s.model_count == full, "and the whole list returns");
    check(gs_ui_search_type(&s, 8) == 0,
          "backspace on empty text changes nothing");

    /* Overflow and garbage. */
    for (i = 0; i < 200; i++)
        gs_ui_search_type(&s, 'a');
    check(strlen(s.search) == sizeof s.search - 1,
          "typing past the field's room stops at its edge");
    check(gs_ui_search_type(&s, 'b') == 0, "a full field refuses more");
    check(gs_ui_search_type(&s, 1) == 0, "a control byte is ignored");
    check(gs_ui_search_type(&s, 0x7f + 40) == 0,
          "a byte past plain text is ignored");
    check(gs_ui_search_type(&s, -3) == 0, "a negative byte is ignored");
    check(gs_ui_search_type(NULL, 'a') == 0, "a null state is refused");
    gs_ui_panes_pick_category(&s, 0);
    check(s.model_count == 0 && gs_ui_panes_model(-1) == NULL,
          "two hundred letters of a match nothing and break nothing");

    gs_catalogue_release();
}

/* The confirm box on a real screen, over a canvas as busy as it gets.
 * Panes full, search text set, the category list open underneath, and
 * every sampled point has to match the composed buffer, since a single
 * label bleeding through would differ from it. */
static void test_confirm_covers_on_screen(void)
{
    const int open_w = 980, open_h = 640;
    gs_window_t *win;
    gs_window_event_t drain;
    gs_ui_state_t st;
    gs_detect_report_t machine;
    unsigned int *expected;
    gs_ui_rect_t box, left;
    int w, h, rounds, x, y;
    int tried = 0, matches = 0;

    if (!gs_window_display_available()) {
        printf("  skip  the confirm box on a real screen (the display is "
               "refusing connections)\n");
        return;
    }

    printf("the confirm box on a real screen\n");

    memset(&st, 0, sizeof st);
    memset(&machine, 0, sizeof machine);
    machine.ram_total_bytes = 10000000000000LL;
    machine.disk_count = 1;
    gs_str_copy(machine.disk[0].mount, sizeof machine.disk[0].mount,
                "/mnt/full");
    machine.disk[0].free_bytes = 1000000000LL;
    gs_ui_panes_refresh(&st, &machine);
    st.mounted = 1;
    st.chooser_open = 1;
    gs_str_copy(st.search, sizeof st.search, "llama");
    st.confirm_step = GS_UI_CONFIRM_STEPS;
    st.confirm_dir = 0;
    st.confirm_row = 0;

    win = gs_window_open("Gravestone confirm cover", open_w, open_h);
    check(win != NULL, "window opened");
    if (win == NULL)
        return;
    for (rounds = 0; rounds < 20; rounds++)
        if (gs_window_wait_event(win, &drain, 50) == 1 &&
            drain.kind == GS_WINDOW_EVENT_EXPOSE)
            break;

    check(gs_ui_paint(win, &st) == GS_OK, "painted with the box open");

    w = gs_window_width(win);
    h = gs_window_height(win);
    expected = malloc((size_t)w * (size_t)h * sizeof *expected);
    if (expected == NULL) {
        gs_window_close(win);
        return;
    }
    gs_ui_compose(expected, w, h, &st);

    box = gs_ui_confirm_rect(w, h, GS_UI_CONFIRM_STEPS);
    left = gs_ui_left_rect(w, h);

    /* A band inside the box between its message and its buttons, and a
     * band outside it where the left pane's rows would have been drawn.
     * Both have to hold exactly the composed pixels. */
    for (y = box.y + 78; y < box.y + box.h - 45; y += 7) {
        for (x = box.x + 8; x < box.x + box.w - 8; x += 23) {
            unsigned int got = 0;
            unsigned int want = expected[(size_t)y * (size_t)w + (size_t)x];
            int attempt, agreed = 0;

            for (attempt = 0; attempt < 10 && !agreed; attempt++) {
                if (gs_window_read_pixel(win, x, y, &got) != GS_OK)
                    break;
                if (got == want)
                    agreed = 1;
                else
                    gs_window_wait_event(win, &drain, 25);
            }
            if (attempt == 0)
                continue;
            tried++;
            if (agreed)
                matches++;
        }
    }
    for (y = left.y + GS_UI_PANE_HEADER + 4; y < box.y - 4;
         y += GS_UI_ROW_HEIGHT) {
        for (x = left.x + 8; x < left.x + left.w - 8; x += 31) {
            unsigned int got = 0;
            unsigned int want = expected[(size_t)y * (size_t)w + (size_t)x];
            int attempt, agreed = 0;

            for (attempt = 0; attempt < 10 && !agreed; attempt++) {
                if (gs_window_read_pixel(win, x, y, &got) != GS_OK)
                    break;
                if (got == want)
                    agreed = 1;
                else
                    gs_window_wait_event(win, &drain, 25);
            }
            if (attempt == 0)
                continue;
            tried++;
            if (agreed)
                matches++;
        }
    }

    printf("        %d of %d sampled points match the composed frame\n",
           matches, tried);
    if (tried == 0) {
        printf("  skip  no point could be read back\n");
    } else {
        check(matches == tried,
              "nothing shows through the confirm box or the dimmed canvas");
    }
    check(gs_window_error_count(win) == 0, "the server was upset nowhere");

    free(expected);
    gs_window_close(win);
    gs_ui_release();
    gs_catalogue_release();
}

/* The removal thread driven end to end against a real file, with the
 * double click, the premature poll and the dead index all thrown at it. */
static void test_removal_thread(void)
{
    gs_ui_state_t s;
    gs_detect_report_t machine;
    FILE *f;
    int i, fired = 0, target = -1;

    printf("the removal thread under attack\n");

    (void)mkdir("build/gravestone-models", 0700);
    f = fopen("build/gravestone-models/spin-target.gguf", "wb");
    if (f == NULL) {
        check(0, "the target file was written");
        return;
    }
    for (i = 0; i < 4096; i++)
        fputc('m', f);
    fclose(f);

    memset(&s, 0, sizeof s);
    memset(&machine, 0, sizeof machine);
    machine.ram_total_bytes = 10000000000000LL;
    machine.disk_count = 1;
    gs_str_copy(machine.disk[0].mount, sizeof machine.disk[0].mount, "/");
    machine.disk[0].free_bytes = 1000000000LL;
    gs_ui_panes_refresh(&s, &machine);

    if (gs_library_scan("build") < 1) {
        check(0, "the target was seen on the disk");
        return;
    }
    for (i = 0; i < gs_library_count(); i++)
        if (strcmp(gs_library_at(i)->name, "spin-target.gguf") == 0)
            target = i;
    check(target >= 0, "the target was seen on the disk");

    check(gs_ui_remove_poll(&s) == 0, "polling before starting reports nothing");

    s.confirm_row = target;
    check(gs_ui_remove_begin(&s) == GS_OK, "the removal thread started");
    check(gs_ui_remove_begin(&s) != GS_OK,
          "a second press while it runs is refused");

    for (i = 0; i < 500 && !fired; i++) {
        if (gs_ui_remove_poll(&s)) {
            fired = 1;
        } else {
            struct timespec nap = {0, 20000000};

            nanosleep(&nap, NULL);
        }
    }
    check(fired, "the thread reported back");
    check(access("build/gravestone-models/spin-target.gguf", F_OK) != 0,
          "and the file is really gone");
    check(strstr(s.detail, "removed") != NULL,
          "and the outcome landed in the status line");
    check(gs_ui_remove_poll(&s) == 0, "it reports back exactly once");

    s.confirm_row = 9999;
    check(gs_ui_remove_begin(&s) != GS_OK, "a dead index never starts");
    check(gs_ui_remove_poll(&s) == 0, "and there is nothing to poll");

    (void)system("rm -rf build/gravestone-models");
    gs_library_release();
    gs_catalogue_release();
}

/* The add path attacked at the gate and through the thread. Names with
 * shell syntax, a slash smuggled in, a pull that cannot succeed, and the
 * double press, with the store directories checked for their promised
 * creation. */
static void test_add_under_attack(void)
{
    gs_ui_state_t s;
    gs_detect_report_t machine;
    struct stat st;
    int i, fired = 0;

    printf("adding under attack\n");

    check(gs_library_pull("qwen3.5:4b; touch build/pwned-add", "build")
          == GS_ERR_ARG, "a name carrying shell syntax is refused");
    check(access("build/pwned-add", F_OK) != 0,
          "and nothing it named was run");
    check(gs_library_pull("hf.co/owner/repo:Q4", "build") == GS_ERR_ARG,
          "a slash never reaches the shell");
    check(gs_library_pull(NULL, "build") == GS_ERR_ARG,
          "a null name is refused");
    check(gs_library_pull("", "build") == GS_ERR_ARG,
          "an empty name is refused");
    check(gs_library_pull("-rf", "build") == GS_ERR_ARG,
          "a name opening with a dash is refused");

    /* The gate on the name comes first, and the directories are made
     * only for a name that passes it, so the failing pull of a clean
     * name is what proves the promised creation. */
    (void)system("rm -rf build/models build/gravestone-models");
    (void)gs_library_pull("no-such-model-xyzzy:1b", "build");
    check(stat("build/models", &st) == 0 && S_ISDIR(st.st_mode),
          "the store directory was created on the way");
    check(stat("build/gravestone-models", &st) == 0 && S_ISDIR(st.st_mode),
          "and the download directory beside it");

    /* Through the thread, against a snapshot doctored down to one row
     * naming a model the registry has never heard of. The pull fails in
     * seconds and cannot download a byte, which matters, because an
     * earlier draft of this test pointed the thread at the real snapshot
     * and its first row was a real 1.3 terabyte model. */
    {
        FILE *doctored = fopen("build/one-row-catalogue.tsv", "w");

        if (doctored == NULL) {
            check(0, "the doctored snapshot was written");
            return;
        }
        fputs("library/zz-no-such\tzz-no-such-model-xyzzy:1b\tQ4_K_M"
              "\t1000\t1\n", doctored);
        fclose(doctored);
    }
    setenv("GS_CATALOGUE_PATH", "build/one-row-catalogue.tsv", 1);
    gs_catalogue_release();

    memset(&s, 0, sizeof s);
    memset(&machine, 0, sizeof machine);
    machine.ram_total_bytes = 10000000000000LL;
    machine.disk_count = 1;
    gs_str_copy(machine.disk[0].mount, sizeof machine.disk[0].mount, "/");
    machine.disk[0].free_bytes = 1000000000LL;
    gs_ui_panes_refresh(&s, &machine);
    check(s.model_count == 1, "the doctored snapshot holds its one row");

    s.confirm_row = 999999;
    check(gs_ui_add_begin(&s) == GS_ERR_ARG, "a dead row never starts");

    s.confirm_row = 0;
    check(gs_ui_add_begin(&s) == GS_OK,
          "the unpullable row starts the thread");
    check(gs_ui_add_begin(&s) != GS_OK,
          "a second press while it runs is refused");
    check(gs_ui_remove_begin(&s) != GS_OK,
          "and a removal cannot start over it either");

    for (i = 0; i < 3000 && !fired; i++) {
        if (gs_ui_remove_poll(&s)) {
            fired = 1;
        } else {
            struct timespec nap = {0, 20000000};

            nanosleep(&nap, NULL);
        }
    }
    check(fired, "the thread reported back");
    printf("        outcome: %s / %s\n", s.status, s.detail);
    check(strcmp(s.status, "ADD FAILED") == 0,
          "the impossible pull reports its failure");
    check(strstr(s.detail, "zz-no-such-model-xyzzy:1b") != NULL,
          "and names the model that did not arrive");
    check(gs_ui_remove_poll(&s) == 0, "it reports back exactly once");

    unsetenv("GS_CATALOGUE_PATH");
    remove("build/one-row-catalogue.tsv");
    (void)system("rm -rf build/models build/gravestone-models");
    gs_library_release();
    gs_catalogue_release();
}

/* The measured percent fed poison. Partial files that lie, sizes of
 * zero, a partial bigger than the whole, and arithmetic at the edges. */
static void test_progress_under_attack(void)
{
    FILE *f;
    int i;

    printf("the download percent under attack\n");

    check(gs_ui_progress_percent(0, 1000) == 0, "nothing yet reads zero");
    check(gs_ui_progress_percent(500, 1000) == 50, "half reads fifty");
    check(gs_ui_progress_percent(1000, 1000) == 99,
          "done in bytes still reads ninety nine, completion earns the rest");
    check(gs_ui_progress_percent(5000, 1000) == 99,
          "a partial bigger than the whole is clamped");
    check(gs_ui_progress_percent(100, 0) == -1,
          "an expected size of zero gives no percent");
    check(gs_ui_progress_percent(100, -5) == -1,
          "a negative expected size gives no percent");
    check(gs_ui_progress_percent(-100, 1000) == -1,
          "negative partial bytes give no percent");
    check(gs_ui_progress_percent(1342273044000LL, 1342273044000LL) == 99,
          "the arithmetic survives a terabyte");

    /* A store built by hand: two partial files that count, a finished
     * blob and a stray directory that must not. */
    (void)system("rm -rf build/models");
    (void)system("mkdir -p build/models/blobs/sha256-dir-partial-fake");
    f = fopen("build/models/blobs/sha256-aa-partial", "wb");
    if (f != NULL) {
        for (i = 0; i < 300; i++)
            fputc('p', f);
        fclose(f);
    }
    f = fopen("build/models/blobs/sha256-bb-partial-3", "wb");
    if (f != NULL) {
        for (i = 0; i < 200; i++)
            fputc('p', f);
        fclose(f);
    }
    f = fopen("build/models/blobs/sha256-cc", "wb");
    if (f != NULL) {
        for (i = 0; i < 9000; i++)
            fputc('c', f);
        fclose(f);
    }

    check(gs_library_partial_bytes("build") == 500,
          "only the partial files count, and both of them do");
    check(gs_ui_progress_percent(gs_library_partial_bytes("build"), 1000)
          == 50, "and they read as fifty percent of a thousand");
    check(gs_library_partial_bytes("/no/such/root") == 0,
          "a root with no store reads zero");
    check(gs_library_partial_bytes(NULL) == 0, "a null root reads zero");
    check(gs_ui_add_progress() == -1,
          "no percent exists while no pull is running");

    /* The stream parser fed poison. The bar reads only what survives. */
    gs_library_pull_note_line("{\"total\":-50,\"completed\":10}");
    gs_library_pull_note_line("{\"total\":0,\"completed\":10}");
    gs_library_pull_note_line("not json at all");
    gs_library_pull_note_line(NULL);
    {
        long long completed = -1, total = -1;

        gs_library_pull_progress(&completed, &total);
        check(completed == 0 && total == 0,
              "poisoned lines move nothing");

        gs_library_pull_note_line("{\"total\":1000,\"completed\":100}");
        gs_library_pull_note_line("{\"total\":9,\"completed\":9}");
        gs_library_pull_progress(&completed, &total);
        check(total == 1000 && completed == 100,
              "a smaller layer finishing cannot drag the bar backwards");

        gs_library_pull_note_line("{\"total\":1000,\"completed\":999999}");
        gs_library_pull_progress(&completed, &total);
        check(completed == 1000,
              "completed past the total is clamped to it");
    }

    (void)system("rm -rf build/models");
}

int main(void)
{
    gs_log_set_level(GS_LOG_ERROR);

    test_geometry_at_every_size();
    test_hit_test_never_lies();
    test_drawing_stays_in_the_buffer();
    test_repeatable();
    test_category_filter();
    test_covering();
    test_locate();
    test_search();
    test_removal_thread();
    test_add_under_attack();
    test_progress_under_attack();
    test_list_covers_on_screen();
    test_confirm_covers_on_screen();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
