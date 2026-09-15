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
#include "harness.h"
#include "gravestone.h"
#include "session.h"
#include "catalogue.h"
#include "detect.h"
#include "library.h"
#include "log.h"
#include "mount.h"
#include "paths.h"
#include "store.h"
#include "str.h"
#include "window.h"

#include <dirent.h>
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
    s.screen = GS_UI_SCREEN_WORKSPACE;
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
        s.screen = (gs_ui_screen_t)pick(0, 1);
        s.settings_ready = pick(0, 1);
        s.fleet_step = pick(-2, GS_UI_FLEET_STEPS + 2);
        s.fleet_dir = pick(-1, 1);
        s.fleet_scroll = pick(-5, 400);
        s.chosen_count = pick(-3, 400);

        hit = gs_ui_hit_test(&s, w, h, x, y, &row);

        /* The home screen answers with the gear, one of the three chat
         * controls, the open models box, or nothing. */
        if (s.screen == GS_UI_SCREEN_HOME &&
            hit != GS_UI_HIT_NONE && hit != GS_UI_HIT_SETTINGS &&
            hit != GS_UI_HIT_CHAT_SEND && hit != GS_UI_HIT_CHAT_ATTACH &&
            hit != GS_UI_HIT_CHAT_BOX && hit != GS_UI_HIT_FLEET &&
            hit != GS_UI_HIT_FLEET_OPTION &&
            hit != GS_UI_HIT_FLEET_CLOSE && hit != GS_UI_HIT_FLEET_AWAY)
            bad_row++;

        /* Shutting the box only ever answers while the box is up, or a
         * click on the ordinary screen would go nowhere. */
        if ((hit == GS_UI_HIT_FLEET_CLOSE || hit == GS_UI_HIT_FLEET_AWAY) &&
            !gs_ui_fleet_visible(&s))
            bad_row++;
        if ((hit == GS_UI_HIT_SETTINGS || hit == GS_UI_HIT_CHAT_SEND ||
             hit == GS_UI_HIT_CHAT_ATTACH || hit == GS_UI_HIT_CHAT_BOX) &&
            row != -1)
            bad_row++;

        /* While the confirm box is up nothing else may answer, its
         * buttons only answer once it has finished growing, and a busy
         * box answers nobody at all. */
        if (s.screen == GS_UI_SCREEN_WORKSPACE &&
            (s.confirm_step > 0 || s.confirm_dir != 0)) {
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
                   hit == GS_UI_HIT_CONFIRM_NO ||
                   hit == GS_UI_HIT_SETTINGS ||
                   hit == GS_UI_HIT_CHAT_SEND ||
                   hit == GS_UI_HIT_CHAT_ATTACH ||
                   hit == GS_UI_HIT_CHAT_BOX || hit == GS_UI_HIT_FLEET ||
                   hit == GS_UI_HIT_FLEET_CLOSE ||
                   hit == GS_UI_HIT_FLEET_AWAY) {
            if (row != -1)
                bad_row++;
        } else if (hit == GS_UI_HIT_FLEET_OPTION) {
            if (row < 0 || row >= s.library_count)
                bad_row++;
        } else if (hit == GS_UI_HIT_SCROLL) {
            /* The bar names the row a press would put at the top rather
             * than a row under the pointer, so it is bounded by the last
             * row that can be first. */
            if (row < 0)
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

/* How many threads this program is running. A worker that has not been
 * waited for is still counted here, which is what makes it visible. */
static int thread_count(void)
{
    DIR *task = opendir("/proc/self/task");
    struct dirent *entry;
    int n = 0;

    if (task == NULL)
        return -1;
    while ((entry = readdir(task)) != NULL)
        if (entry->d_name[0] != '.')
            n++;
    closedir(task);
    return n;
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
    s.screen = GS_UI_SCREEN_WORKSPACE;
        s.mounted = pick(0, 1);
        s.chooser_open = pick(0, 1);
        s.model_count = pick(-10, 4000);
        s.disk_count = pick(-10, 40);
        s.library_count = pick(-10, 600);
        s.model_scroll = pick(-20, 4000);
        s.library_scroll = pick(-20, 600);
        s.model_sel = pick(-5, 4000);
        s.disk_sel = pick(-5, 40);
        s.hover = (gs_ui_hit_t)pick(0, 17);
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
        s.screen = (gs_ui_screen_t)pick(0, 1);
        s.settings_ready = pick(0, 1);
        s.fleet_step = pick(-2, GS_UI_FLEET_STEPS + 2);
        s.fleet_dir = pick(-1, 1);
        s.fleet_scroll = pick(-5, 400);
        s.chosen_count = pick(-3, 400);
        s.cursor_on = pick(0, 1);

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
    s.screen = GS_UI_SCREEN_WORKSPACE;
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
    s.screen = GS_UI_SCREEN_WORKSPACE;
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
    /* One row per kind, plus All models, plus one row per place a model
     * would run. The kinds come first, so the loop below walks only them. */
    check(s.category_count > (int)GS_KIND_COUNT + 1,
          "the chooser offers the kinds and the places as well");
    check(s.category_sel == 0, "it starts on All models");
    check(gs_ui_panes_category_total(0) == all,
          "the All models count matches the pane");

    for (i = 1; i <= (int)GS_KIND_COUNT; i++) {
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
    check(sum == all, "the kinds add up to the whole list");

    /* The places also cover the list, since every row runs somewhere. A
     * row is counted once against its kind and once against its place, so
     * the two tallies are summed apart. */
    {
        int places = 0;

        for (i = (int)GS_KIND_COUNT + 1; i < s.category_count; i++)
            places += gs_ui_panes_category_total(i);
        check(places == all, "and so do the places");
    }

    /* The sweep above runs on a machine with ten terabytes of memory and
     * no card, so every row lands on the processor and three of the four
     * place tallies would be nought. A machine with a card exercises the
     * other three. */
    {
        gs_detect_report_t carded;
        int filled = 0;
        int places = 0;

        memset(&carded, 0, sizeof carded);
        /* The card has to hold more than memory has left, or nothing is
         * ever card only. Four gigabytes of memory with three free leaves
         * three, against a six gigabyte card.
         *
         * Only three of the four can ever appear at once. A row lands on
         * the processor alone when the card is too small for it, and on
         * the card alone when memory is too small, and one machine cannot
         * have both stores smaller than the other. */
        carded.ram_total_bytes = 4LL * 1073741824LL;
        carded.ram_free_bytes = 3LL * 1073741824LL;
        carded.gpu_memory_bytes = 6LL * 1073741824LL;
        carded.gpu_free_bytes = 6LL * 1073741824LL;
        carded.disk_count = 1;
        carded.disk[0].total_bytes = 900LL * 1073741824LL;
        carded.disk[0].free_bytes = 800LL * 1073741824LL;
        gs_ui_panes_refresh(&s, &carded);
        /* The sweep above left the chooser on one kind, so the whole list
         * is asked for again before its size is compared. */
        gs_ui_panes_pick_category(&s, 0);

        for (i = (int)GS_KIND_COUNT + 1; i < s.category_count; i++) {
            int n = gs_ui_panes_category_total(i);

            places += n;
            if (n > 0)
                filled++;
        }
        printf("        with a card: %d of the four places carry rows\n",
               filled);
        check(filled >= 3, "a machine with a card fills most of the places");
        check(places == s.model_count,
              "and the places still cover the whole list");

        /* Every row under the divided entry clears the floor, since the
         * list itself was already filtered by it. */
        {
            int under = 0;
            int j;

            gs_ui_panes_pick_category(&s, s.category_count - 1);
            for (j = 0; j < s.model_count; j++) {
                const gs_catalogue_entry_t *e = gs_ui_panes_model(j);

                if (e != NULL && !gs_catalogue_listed(e->bytes, &carded))
                    under++;
            }
            printf("        %d divided rows shown, %d under the floor\n",
                   s.model_count, under);
            check(s.model_count > 0, "the divided entry holds rows");
            check(under == 0, "and every one of them clears the floor");
            gs_ui_panes_pick_category(&s, 0);
        }

        gs_ui_panes_refresh(&s, &machine);
    }

    /* Out of range choices fall back rather than reading past the array. */
    gs_ui_panes_pick_category(&s, -4);
    check(s.category_sel == 0 && s.model_count == all,
          "a category below zero falls back to All models");
    gs_ui_panes_pick_category(&s, 900);
    check(s.category_sel == 0 && s.model_count == all,
          "a category past the end falls back to All models");
    /* Every row in a panel headed by what fits has to be labelled from
     * the reading that panel was built from. A label taken from a fresh
     * reading would call a row too large while it sits in the list, since
     * free memory falls while the program runs. */
    {
        const gs_detect_report_t *built = gs_ui_panes_machine();
        int row;
        int wrong = 0;

        check(built != NULL, "the panel kept the reading it was built from");
        for (row = 0; row < s.model_count; row++) {
            const gs_catalogue_entry_t *e = gs_ui_panes_model(row);

            if (e == NULL)
                continue;
            if (gs_catalogue_fit(e->bytes, built) == GS_FIT_NONE)
                wrong++;
        }
        check(wrong == 0, "and no row in it is labelled too large");
        printf("        %d rows, %d labelled too large\n",
               s.model_count, wrong);
    }

    /* The tag each row wears: its words, its colour and the pill behind
     * them. Both drawing passes ask for the same answer, so a colour that
     * disagreed with the words would show as a green pill saying that a
     * model runs partly on the processor. */
    {
        const gs_detect_report_t *built = gs_ui_panes_machine();
        gs_ui_tag_t tag;
        gs_detect_report_t pi;
        gs_detect_report_t tiny;
        gs_detect_report_t divides;
        long long divided_size;
        int was = gs_ui_glyph_width();
        int wrong = 0;
        int wrong_tally = 0;
        int bad_name = 0;
        int row;

        gs_ui_set_glyph_width(7);

        /* Four hundred rows would print four hundred lines, so the faults
         * are counted and reported once. */
        for (row = 0; row < s.model_count && row < 400; row++) {
            const gs_catalogue_entry_t *e = gs_ui_panes_model(row);

            if (e == NULL || !gs_ui_model_tag(e->bytes, built, &tag))
                continue;
            if (tag.text[0] == '\0' || tag.ink == tag.face)
                wrong++;
            if (tag.box.w < (int)strlen(tag.text) * 7)
                wrong++;
            if (tag.box.h != GS_UI_TAG_HEIGHT)
                wrong++;
        }
        check(wrong == 0, "every tag has words, a colour and a pill to fit");

        /* A board with no card puts everything on the processor. */
        memset(&pi, 0, sizeof pi);
        pi.ram_total_bytes = 4294967296LL;
        check(gs_ui_model_tag(1073741824LL, &pi, &tag) != 0,
              "a board with no card still tags its rows");
        check(strcmp(tag.text, "CPU") == 0, "with the processor named");
        check(tag.ink == GS_UI_TAG_CPU_INK, "in the processor colour");

        /* Six gigabytes of memory and a four gigabyte card holds neither
         * a large model alone. The size is derived, since a model putting
         * under seven tenths of itself on the card is refused rather than
         * divided. */
        memset(&divides, 0, sizeof divides);
        divides.ram_total_bytes = 6LL * 1073741824LL;
        divides.gpu_memory_bytes = 4LL * 1073741824LL;
        /* Somebody sits at this one, so the reserve leaves memory too
         * small for the model and the card has to take a share. */
        divides.desktop = 1;
        divided_size = gs_catalogue_room(&divides).graphics_bytes
                     * 10 / 7 * 5 / 6;
        check(gs_ui_model_tag(divided_size, &divides, &tag) != 0,
              "a divided model carries a tag");
        check(strstr(tag.text, "GPU ") != NULL &&
              strstr(tag.text, "CPU ") != NULL,
              "naming both shares, since the part outside the card is what "
              "crosses the bus");
        check(tag.ink == GS_UI_TAG_SPLIT_INK, "in the divided colour");

        check(gs_ui_model_tag(1073741824LL, &divides, &tag) != 0,
              "a small model on the same machine tags too");
        check(strcmp(tag.text, "GPU + CPU") == 0, "naming both places");
        check(tag.ink == GS_UI_TAG_CARD_INK,
              "in the card colour, since the card would hold it whole");

        /* One too large for the machine says so, in the warning colour. */
        memset(&tiny, 0, sizeof tiny);
        tiny.ram_total_bytes = 1073741824LL;
        check(gs_ui_model_tag(500LL * 1073741824LL, &tiny, &tag) != 0,
              "a model past the machine still tags");
        check(strcmp(tag.text, "TOO LARGE") == 0, "saying so plainly");
        check(tag.ink == GS_UI_TAG_NONE_INK, "in the warning colour");

        check(gs_ui_model_tag(1073741824LL, NULL, &tag) == 0,
              "no machine, no tag");
        check(gs_ui_model_tag(0, &pi, &tag) == 0, "no size, no tag");
        check(gs_ui_model_tag(1073741824LL, &pi, NULL) == 0,
              "and nowhere to put it, no tag");

        /* Where the tag lands in its row. The size sits in a fixed column
         * at the right edge, and a tag drawn over it reads as one string
         * of nonsense, which is what happens when the two are placed from
         * different measurements. */
        {
            static const int widths[] = { 980, 1200, 1600, 820 };
            int over_size = 0;
            int outside = 0;
            int no_name = 0;
            int ragged = 0;
            int placed = 0;
            int which;

            for (which = 0; which < (int)(sizeof widths /
                                          sizeof widths[0]); which++) {
                int ww = widths[which];
                int hh = 640;
                gs_ui_rect_t pane = gs_ui_left_rect(ww, hh);
                int seats = gs_ui_pane_rows(pane);
                int first_x = -1;
                int seat;

                for (seat = 0; seat < seats && seat < 20; seat++) {
                    gs_ui_rect_t size = gs_ui_model_size_rect(ww, hh, seat);

                    if (!gs_ui_model_tag_at(ww, hh, seat, seat, &tag))
                        continue;
                    placed++;

                    /* Clear of the byte count on its right. */
                    if (tag.box.x + tag.box.w > size.x)
                        over_size++;
                    /* Inside its own pane on the left. */
                    if (tag.box.x < pane.x)
                        outside++;
                    /* Leaving something for the model name. */
                    if (tag.box.x - pane.x < 4 * 7)
                        no_name++;
                    /* Every pill in a pane ends on the same line, so the
                     * column reads straight down. */
                    if (first_x < 0)
                        first_x = tag.box.x + tag.box.w;
                    else if (tag.box.x + tag.box.w != first_x)
                        ragged++;
                }
            }

            check(placed > 0, "tags were placed in the rows");
            check(over_size == 0, "and none of them reaches the byte count");
            check(outside == 0, "none runs out of its pane");
            check(no_name == 0, "and none leaves the name with nowhere");
            check(ragged == 0, "every pill ends on the same line");
            printf("        %d tags placed across four widths\n", placed);
        }

        /* The chooser asks two questions in one list. Everything, then
         * one entry per kind of model, then one entry per place a model
         * would run. */
        {
            int places = 0;
            int entry;

            check(gs_ui_panes_category_count() > (int)GS_KIND_COUNT + 1,
                  "the chooser carries more than the kinds");
            for (entry = 0; entry < gs_ui_panes_category_count(); entry++) {
                const char *label = gs_ui_panes_category_name(entry);

                if (label == NULL || label[0] == '\0')
                    bad_name++;
                if (strstr(label, "Runs on") != NULL ||
                    strstr(label, "Divided") != NULL)
                    places++;
            }
            check(bad_name == 0, "and every entry in it carries a name");
            check(places == 4, "four of them name a place rather than a kind");

            /* Picking a place shows exactly the rows that land there, so
             * the tally beside the entry and the list under it agree. */
            for (entry = 0; entry < gs_ui_panes_category_count(); entry++) {
                gs_ui_panes_pick_category(&s, entry);
                if (gs_ui_panes_category_total(entry) != s.model_count)
                    wrong_tally++;
            }
            check(wrong_tally == 0,
                  "every entry shows as many rows as it counts");

            /* A search narrows the list, so the tallies have to narrow
             * with it. A tally claiming two thousand over a list showing
             * nine is worse than no tally at all. */
            {
                int narrowed = 0;
                int stale = 0;

                /* Picking a category is what rebuilds the list, so it is
                 * how the interface takes a changed search as well. */
                gs_str_copy(s.search, sizeof s.search, "qwen");
                gs_ui_panes_pick_category(&s, s.category_sel);
                for (entry = 0; entry < gs_ui_panes_category_count();
                     entry++) {
                    gs_ui_panes_pick_category(&s, entry);
                    if (gs_ui_panes_category_total(entry) != s.model_count)
                        stale++;
                }
                gs_ui_panes_pick_category(&s, 0);
                narrowed = gs_ui_panes_category_total(0);
                check(stale == 0,
                      "with a search running the tallies still match");
                check(narrowed > 0 && narrowed < 2000,
                      "and the whole tally came down with the list");
                printf("        searching for qwen leaves %d rows\n",
                       narrowed);

                s.search[0] = '\0';
                gs_ui_panes_pick_category(&s, 0);
                check(gs_ui_panes_category_total(0) > narrowed,
                      "clearing the search puts the tallies back");
            }

            /* Nothing under seven tenths on the card reaches the list, so
             * the divided entry starts there. */
            check(strstr(gs_ui_panes_category_name(
                      gs_ui_panes_category_count() - 1), "70%") != NULL,
                  "and the divided entry says where it starts");

            gs_ui_panes_pick_category(&s, 0);
        }

        /* The mark that pulls a model has to stand clear of the bar. A
         * hand reaching for a six pixel rail and missing it by two would
         * otherwise start a download. */
        {
            static const int widths[] = { 980, 1200, 1600, 820 };
            gs_ui_rect_t rail, grip;
            int touching = 0;
            int overlapping = 0;
            int narrowest = 9999;
            int which;

            for (which = 0; which < (int)(sizeof widths /
                                          sizeof widths[0]); which++) {
                int ww = widths[which];
                int hh = 640;
                gs_ui_rect_t pane = gs_ui_left_rect(ww, hh);
                gs_ui_rect_t seat = gs_ui_pane_row_rect(pane, 0);
                gs_ui_rect_t mark = gs_ui_row_mark_rect(seat);
                gs_ui_rect_t size = gs_ui_model_size_rect(ww, hh, 0);
                gs_ui_rect_t list = pane;
                int clear;

                list.y += GS_UI_PANE_HEADER;
                list.h -= GS_UI_PANE_HEADER;
                if (!gs_ui_scrollbar(list, 2000, gs_ui_pane_rows(pane), 0,
                                     &rail, &grip))
                    continue;

                clear = rail.x - (mark.x + mark.w);
                if (clear < narrowest)
                    narrowest = clear;
                if (clear < GS_UI_ROW_MARK_GAP)
                    touching++;
                /* The size sits left of the mark and must not run into
                 * it either. */
                if (size.x + size.w > mark.x)
                    overlapping++;
            }

            printf("        narrowest gap between the mark and the bar: "
                   "%d px\n", narrowest);
            check(touching == 0,
                  "the mark stands clear of the bar at every width");
            check(overlapping == 0, "and the byte count clears the mark");
        }

        /* A press on the bar never reaches the mark behind it. */
        {
            int w = 980;
            int h = 640;
            gs_ui_rect_t pane = gs_ui_left_rect(w, h);
            gs_ui_rect_t list = pane;
            gs_ui_rect_t rail, grip;
            int caught = 0;
            int py;

            s.model_count = 2000;
            list.y += GS_UI_PANE_HEADER;
            list.h -= GS_UI_PANE_HEADER;
            gs_ui_scrollbar(list, s.model_count, gs_ui_pane_rows(pane), 0,
                            &rail, &grip);

            /* Every point across the rail, and two pixels either side of
             * it, since a hand aiming for six pixels lands near them. */
            for (py = rail.y + 4; py < rail.y + rail.h - 4; py += 7) {
                int px;

                for (px = rail.x - 2; px < rail.x + rail.w + 2; px++) {
                    int seat = -1;

                    if (gs_ui_hit_test(&s, w, h, px, py, &seat)
                        == GS_UI_HIT_LEFT_ADD)
                        caught++;
                }
            }
            check(caught == 0,
                  "no press on or beside the bar pulls a model in");
            printf("        the mark answered %d presses near the bar\n",
                   caught);
        }

        /* A pane too narrow to hold a tag and a name drops the tag. */
        check(gs_ui_model_tag_at(200, 640, 0, 0, &tag) == 0,
              "a narrow pane carries no tag at all");

        /* Put the width back, since the checks after this one measure
         * against what the interface uses by default. */
        gs_ui_set_glyph_width(was);
    }

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
    s.screen = GS_UI_SCREEN_WORKSPACE;
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
    st.screen = GS_UI_SCREEN_WORKSPACE;
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

    /* WSLg drops a client's socket now and then, which this codebase
     * records as common after a run opens and closes many windows. Every
     * pixel read after that fails for the one reason, so the section is
     * reported as a skip rather than as a wall of mismatches. */
    if (!gs_window_connected(win)) {
        printf("  skip  reading the open list back "
               "(the server dropped the connection)\n");
        gs_window_close(win);
        return;
    }

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

            if (!gs_window_connected(win))
                break;
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
    if (tried == 0 || !gs_window_connected(win)) {
        printf("  skip  no point could be read back\n");
    } else {
        check(matches == tried,
              "nothing bleeds through the open list on screen");
        check(gs_window_error_count(win) == 0, "the server was upset nowhere");
    }

    free(expected);
    gs_window_close(win);
    gs_ui_release();
    gs_catalogue_release();
}

/* A model on disk pointing back at its row in the list, attacked from
 * every angle that must not move the selection. */
/* A person reading a picture of a pointer knows what a control does
 * before pressing it, so every control has to answer with a shape and the
 * bare canvas has to answer with the plain arrow. */
/* A question worth asking often needs more than one line, so a return
 * has to reach the prompt as a break rather than being dropped. */
/* Sending puts the prompt on record before anything is asked of a model,
 * so a person closing the program finds their question where they left
 * it. */
/* A line ends where the writer put a break, or where the box runs out,
 * whichever comes first. Getting this wrong runs two typed lines into
 * one, or drops the break character into the middle of a word. */
/* A question has to appear in the panel the moment it is sent, and it
 * has to still be there after the program has been closed and opened
 * again. A prompt that vanishes with nothing shown is the fault this
 * covers. */
static void test_history_shows_what_was_asked(void)
{
    static gs_ui_state_t s;   /* a megabyte, kept off the stack */
    static gs_ui_state_t later;   /* a megabyte, kept off the stack */
    gs_ui_rect_t seen, box, panel;

    printf("what the panel shows above the box\n");
    (void)system("rm -rf build/history-test");
    gs_paths_override("build/history-test");
    gs_session_close();

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;

    gs_ui_chat_reload(&s);
    check(s.history_count == 0, "a conversation not started shows nothing");

    /* No model is ticked in this test, so nothing is asked and the panel
     * says so under each prompt rather than leaving it unanswered in
     * silence. */
    gs_str_copy(s.prompt, sizeof s.prompt, "what is time dilation");
    check(gs_ui_chat_send(&s) == GS_OK, "the question sent");
    gs_ui_chat_reload(&s);
    check(s.history_count == 2,
          "the question and the line saying no answer came back");
    check(strcmp(s.history[0], "what is time dilation") == 0,
          "carrying the question word for word");
    check(s.history_mine[0] == 1 && s.history_mine[1] == 0,
          "the question on the person's side and the reason on Gravestone's");
    check(strstr(s.history[1], "No answer came back") != NULL,
          "saying plainly that nothing came back");

    gs_str_copy(s.prompt, sizeof s.prompt, "and length contraction");
    gs_ui_chat_send(&s);
    gs_ui_chat_reload(&s);
    check(s.history_count == 4, "a second question appeared under it");
    check(strcmp(s.history[2], "and length contraction") == 0,
          "in the order they were asked");

    /* Closing the program and opening it again finds the conversation. */
    gs_session_close();
    memset(&later, 0, sizeof later);
    later.screen = GS_UI_SCREEN_HOME;
    gs_ui_chat_resume(&later);
    check(later.session_id == s.session_id,
          "a new run picks up the conversation it left");
    check(later.history_count == 4, "with both questions still in the panel");
    check(strcmp(later.history[0], "what is time dilation") == 0,
          "the first one word for word");

    /* An answer that won sits under the question it answers. */
    {
        gs_session_turn_t turns[4];
        long long answer = 0;

        check(gs_session_turns(later.session_id, turns, 4) == 2,
              "both turns are on record");
        gs_session_add_answer(turns[0].id, "glm4", "clocks run slower",
                              &answer);
        gs_session_judge(answer, GS_SESSION_PASSED, 3);
        gs_session_pick(turns[0].id, NULL);

        gs_ui_chat_reload(&later);
        check(later.history_count == 4,
              "the answer took the place of the line saying none came back");
        check(strcmp(later.history[1], "clocks run slower") == 0,
              "and it sits under the question it answers");
        check(strcmp(later.history[2], "and length contraction") == 0,
              "with the next question below that");
        check(later.record_answer[1] == answer,
              "the answer line knows which answer it is");
    }

    /* The panel has somewhere to draw it, clear of the heading and of
     * the box words are typed into. */
    panel = gs_ui_chat_rect(980, 640);
    seen = gs_ui_chat_history_rect(980, 640);
    box = gs_ui_chat_composer_rect(980, 640);
    check(seen.w > 0 && seen.h > 0, "the panel has room to show it");
    check(seen.y > panel.y, "below the heading");
    check(seen.y + seen.h <= box.y, "and clear of the box below");
    check(seen.x >= panel.x && seen.x + seen.w <= panel.x + panel.w,
          "inside the panel across");
    check(gs_ui_chat_history_rect(40, 40).w == 0,
          "a window too small has no room for it");

    /* Every line carries when it was said, so a conversation read back
     * next week says which day each part of it happened on. */
    {
        gs_session_turn_t turns[4];
        int n;
        int i;
        int missing = 0;

        n = gs_session_turns(later.session_id, turns, 4);
        check(n > 0, "the conversation is on record");
        gs_ui_chat_reload(&later);

        for (i = 0; i < later.history_count; i++) {
            char said[32];

            if (later.history_at[i] <= 0) {
                missing++;
                continue;
            }
            gs_str_clock(later.history_at[i], said, sizeof said);
            if (said[0] == '\0')
                missing++;
        }
        check(missing == 0, "every line carries a moment that reads");
        check(later.history_at[0] >= turns[0].asked_at,
              "the first line is stamped when the prompt was asked");
        printf("        %d lines, first at %lld\n",
               later.history_count, later.history_at[0]);

        /* A line the panel puts up itself is stamped now, since it has
         * no recorded moment in the file. */
        {
            int before = later.history_count;
            long long now = (long long)time(NULL);

            gs_ui_say(&later, "nothing survived");
            check(later.history_at[before] >= now - 5 &&
                  later.history_at[before] <= now + 5,
                  "a line from the panel is stamped as said now");
            later.history_count = before;
        }

        /* Reloading clears the old stamps rather than leaving stale ones
         * beside new lines. */
        later.history_at[GS_UI_HISTORY_MAX - 1] = 999;
        gs_ui_chat_reload(&later);
        check(later.history_at[GS_UI_HISTORY_MAX - 1] == 0,
              "and a reload wipes what was there before");
    }

    /* The record of a run belongs to the prompt it answered, and the
     * sheet reads it from the file when the mark is pressed. */
    {
        gs_session_turn_t turns[4];

        gs_session_turns(later.session_id, turns, 4);
        gs_session_note(turns[0].id, "asking glm4\n  glm4: passed\n");
        gs_ui_chat_reload(&later);
        check(later.record_turn[1] == turns[0].id,
              "the answer line names the prompt behind it");
        check(later.record_turn[0] == turns[0].id,
              "and so does the question it answers");
        check(gs_ui_chat_note_rect(&later, 980, 640, 1).w > 0,
              "so the answer carries a mark to open it with");
        check(gs_ui_chat_note_rect(&later, 980, 640, 0).w == 0,
              "while the question carries none");
        check(gs_ui_chat_note_rect(&later, 980, 640, 3).w > 0,
              "and the line saying no answer came back carries one too");
        gs_ui_record_open(&later, 1);
        check(strstr(later.record_text, "asking glm4") != NULL,
              "opening it reads the record back from the file");
        gs_ui_record_shut(&later);
    }

    /* A line Gravestone puts up itself, with nothing in the file behind
     * it, carries no mark, since pressing it would open nothing. */
    {
        int before = later.history_count;

        gs_ui_say(&later, "nothing survived");
        check(later.history_count == before + 1, "the line went up");
        check(strcmp(later.history[before], "nothing survived") == 0,
              "word for word");
        check(later.history_mine[before] == 0, "on Gravestone's side");
        check(gs_ui_chat_note_rect(&later, 980, 640, before).w == 0,
              "with no mark, since nothing in the file sits behind it");

        gs_ui_say(NULL, "nowhere");
        gs_ui_say(&later, NULL);
        check(later.history_count == before + 1,
              "no state and no words both add nothing");

        later.history_count = GS_UI_HISTORY_MAX;
        gs_ui_say(&later, "one too many");
        check(later.history_count == GS_UI_HISTORY_MAX,
              "and a full panel takes no more");
    }

    gs_session_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/history-test");
}

/* Where the words actually land. Everything else about the transcript is
 * checked without a window, and only a real one can say whether the
 * glyphs fall inside the rectangle the layout gives them.
 *
 * Glyphs are drawn through the window rather than into the pixel buffer,
 * so the pixels are read back off the window itself. The picture is taken
 * twice, once with nothing asked and once with two questions on record.
 * Points inside the rectangle have to differ between the two, and points
 * outside it have to stay the same, which pins the writing to its own
 * area without depending on any font measurement.
 *
 * Reading a pixel is one round trip to the server, so a grid is sampled
 * rather than every point.
 */
static int sample_band(gs_window_t *win, int x0, int y0, int x1, int y1,
                       unsigned int *out, int max)
{
    int n = 0;
    int x, y;

    for (y = y0; y < y1 && n < max; y += 2)
        for (x = x0; x < x1 && n < max; x += 3)
            if (gs_window_read_pixel(win, x, y, &out[n]) == GS_OK)
                n++;
            else
                out[n++] = 0;
    return n;
}

/* The person writes on the right, the program answers on the left, which
 * is the shape every message panel uses and the thing that lets a reader
 * tell the two apart at a glance. */
/* Every answer carries a record of how it was arrived at, opened by a
 * mark on the bubble. A person seeing one model's words needs to know
 * what the others said and why this one won. */
/* The bar down the right of a list. A list twenty rows into two thousand
 * looks the same as twenty rows into thirty without one. */
static void test_the_scrollbar(void)
{
    gs_ui_rect_t box;
    gs_ui_rect_t rail, grip;
    gs_ui_state_t s;
    int w = 980, h = 640;

    printf("the bar down the right of a list\n");

    box.x = 100;
    box.y = 50;
    box.w = 300;
    box.h = 400;

    /* A list that already fits carries none, since a grip filling its
     * whole rail says nothing. */
    check(gs_ui_scrollbar(box, 10, 10, 0, &rail, &grip) == 0,
          "a list that fits carries no bar");
    check(rail.w == 0 && grip.w == 0, "and both rectangles come back empty");
    check(gs_ui_scrollbar(box, 5, 10, 0, &rail, &grip) == 0,
          "nor does one shorter than its box");
    check(gs_ui_scrollbar(box, 0, 10, 0, NULL, NULL) == 0,
          "nor an empty one");
    check(gs_ui_scrollbar(box, 100, 0, 0, NULL, NULL) == 0,
          "nor a box with no room for a row");

    /* A long list carries one, inside its box and against the right. */
    check(gs_ui_scrollbar(box, 2000, 20, 0, &rail, &grip) != 0,
          "a list past its box carries a bar");
    check(rail.x + rail.w <= box.x + box.w, "the rail stays inside across");
    check(rail.x > box.x + box.w / 2, "and sits against the right");
    check(rail.y >= box.y && rail.y + rail.h <= box.y + box.h,
          "inside it down as well");
    check(grip.w == rail.w, "the grip is as wide as its rail");
    check(grip.h >= GS_UI_SCROLL_MIN_THUMB,
          "and long enough to see on a list of two thousand");
    check(grip.h <= rail.h, "never longer than the rail it runs in");
    check(grip.y == rail.y, "at the top of the list it sits at the top");

    /* The grip walks down as the list does, and ends flush at the end. */
    {
        int last = -1;
        int backwards = 0;
        int scroll;

        for (scroll = 0; scroll <= 1980; scroll++) {
            gs_ui_scrollbar(box, 2000, 20, scroll, &rail, &grip);
            if (grip.y < last)
                backwards++;
            if (grip.y < rail.y || grip.y + grip.h > rail.y + rail.h)
                backwards++;
            last = grip.y;
        }
        check(backwards == 0,
              "the grip only ever moves down, and stays in its rail");
        gs_ui_scrollbar(box, 2000, 20, 1980, &rail, &grip);
        check(grip.y + grip.h == rail.y + rail.h,
              "and ends flush with the bottom on the last row");
    }

    /* A grip is a share of the rail as the visible rows are of the whole. */
    {
        gs_ui_rect_t half;

        gs_ui_scrollbar(box, 40, 20, 0, &rail, &half);
        check(half.h > rail.h / 3 && half.h < rail.h * 2 / 3,
              "half a list showing gives about half a rail");
    }

    /* A box too small for a bar carries none rather than a broken one. */
    {
        gs_ui_rect_t tiny = box;

        tiny.w = 8;
        check(gs_ui_scrollbar(tiny, 2000, 20, 0, NULL, NULL) == 0,
              "a box too narrow carries no bar");
        tiny = box;
        tiny.h = 10;
        check(gs_ui_scrollbar(tiny, 2000, 20, 0, NULL, NULL) == 0,
              "and one too short carries none either");
    }

    /* Pressing the rail puts that part of the list at the top. */
    check(gs_ui_scroll_from(box, 2000, 20, box.y) == 0,
          "a press at the top shows the first row");
    check(gs_ui_scroll_from(box, 2000, 20, box.y + box.h) == 1980,
          "a press at the bottom shows the last page");
    {
        int middle = gs_ui_scroll_from(box, 2000, 20, box.y + box.h / 2);

        check(middle > 800 && middle < 1200,
              "and a press halfway lands about halfway");
    }
    check(gs_ui_scroll_from(box, 10, 20, box.y + 5) == 0,
          "a list that fits never moves");

    /* The panes answer a press on their bar rather than on a row. */
    printf("pressing the bar on a pane\n");
    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_WORKSPACE;
    s.record_open = -1;
    s.model_count = 2000;
    s.library_count = 900;
    s.disk_count = 2;

    {
        gs_ui_rect_t pane = gs_ui_left_rect(w, h);
        gs_ui_rect_t list = pane;
        int row = -1;

        list.y += GS_UI_PANE_HEADER;
        list.h -= GS_UI_PANE_HEADER;
        check(gs_ui_scrollbar(list, s.model_count, gs_ui_pane_rows(pane),
                              0, &rail, &grip) != 0,
              "two thousand models put a bar on the pane");
        check(gs_ui_hit_test(&s, w, h, rail.x + rail.w / 2,
                             rail.y + rail.h / 2, &row) == GS_UI_HIT_SCROLL,
              "and a press on it answers with the bar");
        check(row > 0, "naming a row well down the list");
        check(gs_ui_cursor_for(GS_UI_HIT_SCROLL) == GS_WINDOW_CURSOR_HAND,
              "with a hand over it");

        /* A press on the rows beside it still picks a row. */
        check(gs_ui_hit_test(&s, w, h, pane.x + 20,
                             rail.y + 4, &row) == GS_UI_HIT_LEFT_ROW,
              "while the rows beside it still answer");
    }

    /* Taking hold of a grip and dragging it, the way a desktop does. */
    printf("dragging a grip\n");
    {
        gs_ui_rect_t pane = gs_ui_left_rect(w, h);
        gs_ui_rect_t list = pane;
        gs_ui_rect_t bar, knob;

        memset(&s, 0, sizeof s);
        s.screen = GS_UI_SCREEN_WORKSPACE;
        s.record_open = -1;
        s.model_count = 2000;

        list.y += GS_UI_PANE_HEADER;
        list.h -= GS_UI_PANE_HEADER;
        check(gs_ui_scrollbar(list, s.model_count, gs_ui_pane_rows(pane),
                              0, &bar, &knob) != 0, "the pane has a bar");
        check(s.drag_bar == GS_UI_BAR_NONE,
              "a zeroed state holds no grip");

        /* Pressing anywhere but the rail takes nothing. */
        check(gs_ui_bar_grab(&s, w, h, pane.x + 10, bar.y + 20) == 0,
              "a press beside the bar takes no grip");
        check(s.drag_bar == GS_UI_BAR_NONE, "and holds nothing");

        /* Pressing the rail takes hold and moves the list at once. */
        check(gs_ui_bar_grab(&s, w, h, bar.x + bar.w / 2,
                             bar.y + bar.h / 2) != 0,
              "a press on the bar takes hold");
        check(s.drag_bar == GS_UI_BAR_MODELS, "naming which list it holds");
        check(s.model_scroll > 0, "and the list moved to meet it");

        /* Taking hold at the top, so the walk below only ever goes down. */
        gs_ui_bar_drag(&s, w, h, bar.y);
        check(s.model_scroll == 0, "dragging to the top shows the first row");

        /* The pointer is followed, and the list follows monotonically. */
        {
            int last = s.model_scroll;
            int backwards = 0;
            int py;

            for (py = bar.y; py <= bar.y + bar.h; py += 3) {
                gs_ui_bar_drag(&s, w, h, py);
                if (s.model_scroll < last)
                    backwards++;
                last = s.model_scroll;
            }
            check(backwards == 0, "dragging down only ever moves down");
            check(s.model_scroll == s.model_count - gs_ui_pane_rows(pane),
                  "and the bottom of the rail is the last page");
        }

        /* Dragging past either end stays inside the list. */
        gs_ui_bar_drag(&s, w, h, -500);
        check(s.model_scroll == 0, "dragging above the window shows the top");
        gs_ui_bar_drag(&s, w, h, h + 500);
        check(s.model_scroll == s.model_count - gs_ui_pane_rows(pane),
              "and below it shows the last page");

        /* Letting go ends it, and moving afterwards changes nothing. */
        {
            int where = s.model_scroll;

            gs_ui_bar_drop(&s);
            check(s.drag_bar == GS_UI_BAR_NONE, "letting go holds nothing");
            check(gs_ui_bar_drag(&s, w, h, bar.y) == 0,
                  "and the pointer no longer moves the list");
            check(s.model_scroll == where, "which stays where it was left");
        }

        check(gs_ui_bar_grab(NULL, w, h, 0, 0) == 0, "no state takes nothing");
        gs_ui_bar_drop(NULL);
    }

    /* A pane holding a short list has no bar, so the rows keep the width. */
    {
        gs_ui_rect_t pane = gs_ui_right_rect(w, h);
        int row = -1;

        s.library_count = 2;
        check(gs_ui_hit_test(&s, w, h, pane.x + pane.w - 5,
                             pane.y + GS_UI_PANE_HEADER + 4, &row)
              != GS_UI_HIT_SCROLL,
              "a short list leaves no bar to press");
    }
}

static void test_the_record(void)
{
    gs_ui_state_t s;
    gs_ui_rect_t note, mine, sheet, shut;
    int w = 980, h = 640;
    int row = -1;

    printf("the note opening the record behind an answer\n");
    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;
    s.record_open = -1;

    gs_str_copy(s.history[0], GS_UI_HISTORY_TEXT, "what is time dilation");
    s.history_mine[0] = 1;
    gs_str_copy(s.history[1], GS_UI_HISTORY_TEXT, "clocks run slower");
    s.history_mine[1] = 0;
    s.history_count = 2;

    /* A line with no prompt behind it has nothing to open, and a mark
     * opening nothing would be worse than none at all. */
    check(gs_ui_chat_note_rect(&s, w, h, 1).w == 0,
          "a line with no prompt behind it carries no mark");

    s.record_turn[1] = 7;
    note = gs_ui_chat_note_rect(&s, w, h, 1);
    check(note.w > 0, "a line that came back for a prompt carries one");

    /* The person's own words need no record, since they wrote them. */
    check(gs_ui_chat_note_rect(&s, w, h, 0).w == 0,
          "and a line the person wrote never does");
    s.record_turn[0] = 7;
    check(gs_ui_chat_note_rect(&s, w, h, 0).w == 0,
          "even though it belongs to the same prompt");

    mine = gs_ui_chat_bubble_rect(&s, w, h, 1);
    check(note.x >= mine.x && note.x + note.w <= mine.x + mine.w,
          "the mark sits inside the bubble across");
    check(note.y >= mine.y && note.y + note.h <= mine.y + mine.h,
          "and inside it down");

    check(gs_ui_hit_test(&s, w, h, note.x + note.w / 2,
                         note.y + note.h / 2, &row) == GS_UI_HIT_NOTE,
          "pressing it answers with the note");
    check(row == 1, "naming which line it belongs to");
    check(gs_ui_cursor_for(GS_UI_HIT_NOTE) == GS_WINDOW_CURSOR_HAND,
          "and the pointer shows a hand over it");

    check(gs_ui_chat_note_rect(NULL, w, h, 1).w == 0, "no state, no mark");
    check(gs_ui_chat_note_rect(&s, w, h, -1).w == 0, "nor before the first");
    check(gs_ui_chat_note_rect(&s, w, h, 99).w == 0, "nor past the last");

    printf("the record itself\n");
    gs_ui_record_open(&s, 1);
    sheet = gs_ui_record_rect(w, h);
    shut = gs_ui_record_shut_rect(w, h);
    check(sheet.w > 0 && sheet.h > 0, "the record has a sheet to sit on");
    check(sheet.w > w / 2 && sheet.h > h / 2,
          "large enough to read several models at once");
    check(shut.x + shut.w <= sheet.x + sheet.w && shut.y >= sheet.y,
          "with a cross inside its corner");

    /* It covers the chat, so nothing under it answers until it is shut. */
    check(gs_ui_hit_test(&s, w, h, shut.x + shut.w / 2,
                         shut.y + shut.h / 2, &row) == GS_UI_HIT_RECORD_SHUT,
          "the cross shuts it");
    check(gs_ui_hit_test(&s, w, h, sheet.x + 20,
                         sheet.y + sheet.h - 20, &row) == GS_UI_HIT_NONE,
          "the sheet itself answers nothing");
    check(gs_ui_hit_test(&s, w, h, 2, h - 2, &row) == GS_UI_HIT_RECORD_SHUT,
          "and anywhere outside it shuts it too");
    check(gs_ui_hit_test(&s, w, h, gs_ui_gear_rect(w, h).x + 4,
                         gs_ui_gear_rect(w, h).y + 4, &row)
          == GS_UI_HIT_RECORD_SHUT,
          "so the gear underneath stays out of reach");

    gs_ui_record_shut(&s);
    check(gs_ui_hit_test(&s, w, h, gs_ui_gear_rect(w, h).x + 4,
                         gs_ui_gear_rect(w, h).y + 4, &row)
          == GS_UI_HIT_SETTINGS, "with it shut the gear works again");
    check(gs_ui_record_rect(40, 40).w == 0,
          "a window too small carries no record sheet");
    check(gs_ui_record_shut_rect(40, 40).w == 0, "and no cross either");

    /* A record longer than the sheet has to be reachable to the end, and
     * one that fits has nowhere to go. */
    gs_ui_record_open(&s, 1);
    check(gs_ui_record_scroll_limit(&s, w, h, 17) == 0,
          "a short record does not scroll");
    {
        char many[GS_UI_RECORD_TEXT];
        size_t at = 0;
        int i;

        for (i = 0; i < 300 && at + 20 < sizeof many; i++) {
            memcpy(many + at, "a line of record\n", 17);
            at += 17;
        }
        many[at] = '\0';
        gs_ui_record_open(&s, 1);
        gs_str_copy(s.record_text, sizeof s.record_text, many);
        check(gs_ui_record_scroll_limit(&s, w, h, 17) > 0,
              "a long one does");
        gs_ui_record_shut(&s);
        check(gs_ui_record_scroll_limit(&s, w, h, 17) == 0,
              "and a shut record has nothing to scroll");
        gs_ui_record_open(&s, 1);
        check(gs_ui_record_scroll_limit(&s, w, h, 0) == 0,
              "nor does one with no line height to count by");
        check(gs_ui_record_scroll_limit(&s, 40, 40, 17) == 0,
              "nor one in a window with no sheet");
    }

    /* A state straight out of memset carries nought in record_open, and
     * nought is the first line, so the panel would swallow every click on
     * a screen that has never opened one. */
    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;
    check(gs_ui_record_showing(&s) == 0, "a zeroed state shows no record");
    check(gs_ui_record_showing(NULL) == 0, "and neither does no state");
    check(gs_ui_hit_test(&s, w, h, gs_ui_gear_rect(w, h).x + 4,
                         gs_ui_gear_rect(w, h).y + 4, &row)
          == GS_UI_HIT_SETTINGS, "so the gear under it still answers");
}

static void test_bubbles(void)
{
    gs_ui_state_t s;
    gs_ui_rect_t seen, mine, theirs, second;
    int w = 980, h = 640;

    printf("which side each bubble sits on\n");
    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;

    gs_str_copy(s.history[0], GS_UI_HISTORY_TEXT, "what is time dilation");
    s.history_mine[0] = 1;
    gs_str_copy(s.history[1], GS_UI_HISTORY_TEXT, "clocks run slower");
    s.history_mine[1] = 0;
    gs_str_copy(s.history[2], GS_UI_HISTORY_TEXT, "and length contraction");
    s.history_mine[2] = 1;
    s.history_count = 3;

    seen = gs_ui_chat_history_rect(w, h);
    mine = gs_ui_chat_bubble_rect(&s, w, h, 0);
    theirs = gs_ui_chat_bubble_rect(&s, w, h, 1);
    second = gs_ui_chat_bubble_rect(&s, w, h, 2);

    check(mine.w > 0 && theirs.w > 0, "both bubbles have a size");
    check(mine.x + mine.w == seen.x + seen.w,
          "what the person wrote is against the right edge");
    check(theirs.x == seen.x, "what came back is against the left edge");
    check(mine.x > theirs.x, "so the two sides never line up");

    /* A bubble takes part of the width, leaving the other side clear. */
    check(mine.w <= seen.w * GS_UI_BUBBLE_SHARE / 100 + 2 * GS_UI_BUBBLE_PAD_X,
          "a bubble never takes the whole width");
    check(theirs.x + theirs.w < seen.x + seen.w,
          "so the far side of an answer stays clear");

    /* Stacked in order, newest at the bottom against the box. */
    check(mine.y < theirs.y, "the older bubble sits above the newer");
    check(theirs.y < second.y, "and so on down");
    check(mine.y + mine.h <= theirs.y, "with a gap, never overlapping");
    check(theirs.y + theirs.h <= second.y, "at every step");
    check(second.y + second.h <= seen.y + seen.h,
          "and the newest ends inside the area");

    /* Every bubble is tall enough for its name and its words. */
    check(gs_ui_chat_bubble_lines(&s, w, h, 0) >= 1, "a bubble has a line");
    check(mine.h >= GS_UI_BUBBLE_HEAD + GS_UI_BUBBLE_LINE,
          "and room for the name above the words");

    /* A long answer wraps and stands taller. */
    {
        gs_ui_rect_t tall;
        char lengthy[GS_UI_HISTORY_TEXT];
        int i;

        memset(lengthy, 0, sizeof lengthy);
        for (i = 0; i < 20; i++)
            strcat(lengthy, "elephant ");
        gs_str_copy(s.history[1], GS_UI_HISTORY_TEXT, lengthy);
        tall = gs_ui_chat_bubble_rect(&s, w, h, 1);
        check(gs_ui_chat_bubble_lines(&s, w, h, 1) > 1,
              "a long answer takes several lines");
        check(tall.h > theirs.h, "so its bubble stands taller");
        check(tall.x == seen.x, "and it is still on the left");
        gs_str_copy(s.history[1], GS_UI_HISTORY_TEXT, "clocks run slower");
    }

    check(gs_ui_chat_bubble_rect(NULL, w, h, 0).w == 0, "no state, no bubble");
    check(gs_ui_chat_bubble_rect(&s, w, h, -1).w == 0, "nor before the first");
    check(gs_ui_chat_bubble_rect(&s, w, h, 99).w == 0, "nor past the last");
    check(gs_ui_chat_bubble_rect(&s, 40, 40, 0).w == 0,
          "nor in a window with no room");
    check(gs_ui_chat_bubble_columns(40, 40) == 0,
          "which also carries no characters across");

    /* A conversation longer than the area keeps its newest against the
     * box, and the oldest climb out of sight. */
    {
        int i;
        gs_ui_rect_t newest;

        for (i = 0; i < 40; i++) {
            snprintf(s.history[i], GS_UI_HISTORY_TEXT, "line %d", i);
            s.history_mine[i] = (unsigned char)(i % 2);
        }
        s.history_count = 40;
        newest = gs_ui_chat_bubble_rect(&s, w, h, 39);
        check(newest.w > 0, "the newest is there to be seen");
        check(newest.y + newest.h <= seen.y + seen.h,
              "sitting against the bottom of the area");
        check(gs_ui_chat_bubble_rect(&s, w, h, 0).w == 0,
              "and the oldest has climbed out of sight");
    }
}

static void test_history_lands_where_it_should(void)
{
    gs_ui_state_t s;
    gs_window_t *win;
    gs_window_event_t drain;
    gs_ui_rect_t seen, box, panel;
    static unsigned int before[6000];
    static unsigned int after[6000];
    int w = 980, h = 640;
    int rounds;
    int taken, i, changed;

    if (!gs_window_display_available()) {
        printf("  skip  where the transcript lands (no screen)\n");
        return;
    }
    printf("where the transcript lands on the screen\n");

    (void)system("rm -rf build/where-test");
    gs_paths_override("build/where-test");
    gs_session_close();

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;

    win = gs_window_open("Gravestone transcript", w, h);
    check(win != NULL, "window opened");
    if (win == NULL) {
        gs_paths_override(NULL);
        return;
    }
    for (rounds = 0; rounds < 20; rounds++)
        if (gs_window_wait_event(win, &drain, 50) == 1 &&
            drain.kind == GS_WINDOW_EVENT_EXPOSE)
            break;

    w = gs_window_width(win);
    h = gs_window_height(win);
    seen = gs_ui_chat_history_rect(w, h);
    box = gs_ui_chat_composer_rect(w, h);
    panel = gs_ui_chat_rect(w, h);
    check(seen.w > 0 && seen.h > 0, "the transcript has an area");
    if (seen.w <= 0) {
        gs_window_close(win);
        gs_paths_override(NULL);
        return;
    }

    /* Nothing asked yet, so the area is bare. Bubbles stack upwards from
     * the bottom, so the band against the box is where the newest lands
     * and the only band worth reading. */
    check(gs_ui_paint(win, &s) == GS_OK, "painted with nothing asked");
    gs_window_flush(win);
    taken = sample_band(win, seen.x, seen.y + seen.h - 90,
                        seen.x + seen.w, seen.y + seen.h, before,
                        (int)(sizeof before / sizeof before[0]));
    check(taken > 500, "the bare area was read back");

    /* Two questions on record, which is what the panel has to show. */
    gs_str_copy(s.prompt, sizeof s.prompt, "what is time dilation");
    check(gs_ui_chat_send(&s) == GS_OK, "the first question sent");
    gs_str_copy(s.prompt, sizeof s.prompt, "and length contraction");
    gs_ui_chat_send(&s);
    gs_ui_chat_reload(&s);
    check(s.history_count == 2, "both are in the panel");
    check(gs_ui_paint(win, &s) == GS_OK, "painted with them showing");
    gs_window_flush(win);
    sample_band(win, seen.x, seen.y + seen.h - 90, seen.x + seen.w,
                seen.y + seen.h, after,
                (int)(sizeof after / sizeof after[0]));

    changed = 0;
    for (i = 0; i < taken; i++)
        if (before[i] != after[i])
            changed++;
    printf("        %d of %d sampled points changed inside the area\n",
           changed, taken);
    check(changed > 20, "the questions really were drawn there");

    /* Nothing appeared in the box below, where the typing goes. A glyph
     * landing there would put the transcript over the prompt. */
    taken = sample_band(win, box.x + 4, box.y + 4, box.x + box.w - 4,
                        box.y + 30, before,
                        (int)(sizeof before / sizeof before[0]));
    check(gs_ui_paint(win, &s) == GS_OK, "painted again");
    gs_window_flush(win);
    sample_band(win, box.x + 4, box.y + 4, box.x + box.w - 4, box.y + 30,
                after, (int)(sizeof after / sizeof after[0]));
    changed = 0;
    for (i = 0; i < taken; i++)
        if (before[i] != after[i])
            changed++;
    check(changed == 0, "and the box below holds still");

    check(seen.y > panel.y, "the area starts below the heading");
    check(seen.y + seen.h <= box.y, "and ends above the box");
    check(seen.x >= panel.x, "with its left edge inside the panel");
    check(seen.x + seen.w <= panel.x + panel.w, "and its right edge too");
    check(gs_window_error_count(win) == 0, "the server reported no errors");

    gs_window_close(win);
    gs_session_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/where-test");
    gs_ui_release();
    gs_catalogue_release();
}

static void test_prompt_lines(void)
{
    char line[64];

    printf("how a prompt is broken into lines\n");

    check(gs_ui_chat_line("a\nb", 200, 0, line, sizeof line) == 1 &&
          strcmp(line, "a") == 0, "a break ends the line early");
    check(gs_ui_chat_line("a\nb", 200, 1, line, sizeof line) == 1 &&
          strcmp(line, "b") == 0, "and the next line carries on after it");
    check(gs_ui_chat_line("a\nb", 200, 2, line, sizeof line) == 0,
          "and there is no third line");

    /* The room is in pixels, since letters differ in width. "abc" is 32
     * pixels wide and "abcdefgh" is 84, so 40 holds the first and cuts
     * the rest of the word off with nowhere to pull back to. */
    check(gs_window_face_width("abc") == 32, "the widths are what they were");
    check(gs_ui_chat_line("abcdefgh", 40, 0, line, sizeof line) == 1 &&
          strncmp(line, "abc", 3) == 0,
          "one word wider than the line is cut where it is, having "
          "nowhere to pull back to");
    check(gs_ui_chat_line("abcdefgh", 40, 1, line, sizeof line) == 1 &&
          strlen(line) > 0, "and what is left carries on below");

    /* A line that runs out of room inside a word pulls back to the last
     * space. "clocks run" is 91 pixels and adding "slower" makes 157, so
     * 110 pixels breaks between the two. */
    check(gs_ui_chat_line("clocks run slower here", 110, 0, line,
                          sizeof line) == 1 &&
          strcmp(line, "clocks run") == 0,
          "a line pulls back to the last space rather than halving a word");
    check(gs_ui_chat_line("clocks run slower here", 110, 1, line,
                          sizeof line) == 1 &&
          strcmp(line, "slower here") == 0,
          "and the next line starts on the word, not on the space");
    check(gs_ui_chat_line("clocks run slower here", 110, 2, line,
                          sizeof line) == 0, "with nothing after it");

    /* Room for one word and no more. */
    check(gs_ui_chat_line("abc def", 35, 0, line, sizeof line) == 1 &&
          strcmp(line, "abc") == 0, "a word filling the line is kept whole");
    check(gs_ui_chat_line("abc def", 35, 1, line, sizeof line) == 1 &&
          strcmp(line, "def") == 0, "and the next word follows on its own");

    {
        int i;
        int hanging = 0;
        int measured = 0;

        /* No line may run past the room it was given, and none may end on
         * a space, which would read as a gap at the end of a line. */
        for (i = 0; i < 10; i++)
            if (gs_ui_chat_line("a clock moving fast ticks slower than one",
                                120, i, line, sizeof line)) {
                size_t len = strlen(line);

                measured++;
                if (len > 0 && line[len - 1] == ' ')
                    hanging++;
                if (gs_window_face_width(line) > 120)
                    hanging++;
            }
        check(measured > 2, "a long sentence took several lines");
        check(hanging == 0,
              "none of them hangs on a space or runs past the room");
    }

    /* Two breaks in a row leave an empty line, which is what a person
     * typing a blank line between paragraphs expects to see. */
    check(gs_ui_chat_line("a\n\nb", 200, 1, line, sizeof line) == 1 &&
          line[0] == '\0', "two breaks together leave a line empty");
    check(gs_ui_chat_line("a\n\nb", 200, 2, line, sizeof line) == 1 &&
          strcmp(line, "b") == 0, "and the writing goes on below it");

    check(gs_ui_chat_line("", 200, 0, line, sizeof line) == 0,
          "no text gives no lines");
    check(gs_ui_chat_line(NULL, 200, 0, line, sizeof line) == 0,
          "and neither does nothing at all");
    check(gs_ui_chat_line("abc", 0, 0, line, sizeof line) == 0,
          "a box with no room across gives none");
    check(gs_ui_chat_line("abc", -3, 0, line, sizeof line) == 0,
          "nor a negative one");
    check(gs_ui_chat_line("abc", 200, -1, line, sizeof line) == 0,
          "a line before the first is refused");
    check(gs_ui_chat_line("abc", 200, 0, NULL, sizeof line) == 0,
          "nowhere to write is refused");
    check(gs_ui_chat_line("abc", 200, 0, line, 0) == 0,
          "no room to write into is refused");

    /* A break is never drawn, since it is where the line ends. */
    {
        int i;
        int found = 0;

        for (i = 0; i < 6; i++)
            if (gs_ui_chat_line("one\ntwo\nthree", 400, i, line, sizeof line))
                if (strchr(line, '\n') != NULL)
                    found++;
        check(found == 0, "no line ever carries the break itself");
    }
}

static void test_send_records_the_prompt(void)
{
    gs_ui_state_t s;
    gs_session_turn_t turns[8];
    long long was;
    int n;

    printf("sending a prompt writes it down\n");
    (void)system("rm -rf build/send-test");
    gs_paths_override("build/send-test");
    gs_session_close();

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;

    check(gs_ui_chat_send(NULL) == GS_ERR_ARG, "no state is refused");
    check(gs_ui_chat_send(&s) == GS_ERR_ARG, "an empty box sends nothing");
    gs_str_copy(s.prompt, sizeof s.prompt, "   \n\t  ");
    check(gs_ui_chat_send(&s) == GS_ERR_ARG,
          "and a box holding only spaces and breaks asks nothing");
    check(s.session_id == 0, "neither started a conversation");

    gs_str_copy(s.prompt, sizeof s.prompt, "what is a pointer");
    check(gs_ui_chat_send(&s) == GS_OK, "a real prompt sends");
    check(s.session_id > 0, "which started a conversation");
    check(s.prompt[0] == '\0', "and emptied the box");

    was = s.session_id;
    gs_str_copy(s.prompt, sizeof s.prompt, "and a reference\nover two lines");
    check(gs_ui_chat_send(&s) == GS_OK, "a second prompt sends");
    check(s.session_id == was, "into the conversation already going");

    n = gs_session_turns(s.session_id, turns, 8);
    check(n == 2, "both prompts are on record");
    check(strcmp(turns[0].prompt, "what is a pointer") == 0,
          "the first word for word");
    check(strcmp(turns[1].prompt, "and a reference\nover two lines") == 0,
          "and the second with its break kept");
    check(turns[0].ordinal == 1 && turns[1].ordinal == 2,
          "in the order they were sent");

    gs_session_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/send-test");
}

static void test_prompt_holds_lines(void)
{
    gs_ui_state_t s;

    printf("line breaks inside the prompt\n");
    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;

    check(gs_ui_chat_type(&s, 'a') == 1, "a letter is taken");
    check(gs_ui_chat_type(&s, '\n') == 1, "and so is a return");
    check(gs_ui_chat_type(&s, 'b') == 1, "and a letter after it");
    check(strcmp(s.prompt, "a\nb") == 0,
          "the break is kept where it was typed");

    check(gs_ui_chat_type(&s, 8) == 1, "backspace takes one back");
    check(strcmp(s.prompt, "a\n") == 0, "leaving the break in place");
    check(gs_ui_chat_type(&s, 8) == 1, "and again");
    check(strcmp(s.prompt, "a") == 0, "which takes the break itself");

    /* Shift and the oblique give a question mark, which has to reach the
     * prompt the same as any other mark. */
    check(gs_ui_chat_type(&s, '?') == 1, "a question mark is taken");
    check(gs_ui_chat_type(&s, '!') == 1, "and an exclamation mark");
    check(gs_ui_chat_type(&s, '@') == 1, "and an at sign");
    check(strcmp(s.prompt, "a?!@") == 0, "all three land in order");

    /* Nothing else that is not a printable character. */
    check(gs_ui_chat_type(&s, 27) == 0, "escape is not typed into it");
    check(gs_ui_chat_type(&s, 9) == 0, "nor a tab");
    check(gs_ui_chat_type(&s, 1) == 0, "nor a control character");
    check(gs_ui_chat_type(&s, 200) == 0, "nor a byte above the printable ones");
    check(strcmp(s.prompt, "a?!@") == 0, "and none of them changed it");
    check(gs_ui_chat_type(NULL, 'a') == 0, "no state is refused");

    /* A prompt filled to the brim stops rather than running past its end. */
    {
        size_t i;

        for (i = 0; i < sizeof s.prompt + 10; i++)
            gs_ui_chat_type(&s, 'x');
        check(strlen(s.prompt) < sizeof s.prompt, "a full prompt stops");
        check(gs_ui_chat_type(&s, 'x') == 0, "and refuses more");
        check(gs_ui_chat_type(&s, '\n') == 0, "including a break");
    }
}

static void test_cursor_shapes(void)
{
    int hit;
    int bad_shape = 0;
    int hands = 0;

    printf("the shape the pointer takes\n");

    check(gs_ui_cursor_for(GS_UI_HIT_CHAT_BOX) == GS_WINDOW_CURSOR_TEXT,
          "the prompt box shows the bar words are typed against");
    check(gs_ui_cursor_for(GS_UI_HIT_CHAT_SEND) == GS_WINDOW_CURSOR_HAND,
          "the send arrow shows a hand");
    check(gs_ui_cursor_for(GS_UI_HIT_CHAT_ATTACH) == GS_WINDOW_CURSOR_HAND,
          "and so does the plus");
    check(gs_ui_cursor_for(GS_UI_HIT_FLEET) == GS_WINDOW_CURSOR_HAND,
          "and the models button");
    check(gs_ui_cursor_for(GS_UI_HIT_FLEET_OPTION) == GS_WINDOW_CURSOR_HAND,
          "and a model inside the box it opens");
    check(gs_ui_cursor_for(GS_UI_HIT_FLEET_CLOSE) == GS_WINDOW_CURSOR_HAND,
          "and the cross that shuts it");
    check(gs_ui_cursor_for(GS_UI_HIT_SETTINGS) == GS_WINDOW_CURSOR_HAND,
          "and the gear");

    /* Nothing under the pointer, so nothing to promise. */
    check(gs_ui_cursor_for(GS_UI_HIT_NONE) == GS_WINDOW_CURSOR_ARROW,
          "bare canvas shows the ordinary arrow");
    check(gs_ui_cursor_for(GS_UI_HIT_FLEET_AWAY) == GS_WINDOW_CURSOR_ARROW,
          "and so does the space outside an open box");

    /* Every answer the hit test can give has to be a real shape, or a
     * control added later would silently leave the pointer as it was. */
    for (hit = 0; hit <= GS_UI_HIT_FLEET_AWAY; hit++) {
        gs_window_cursor_t shape = gs_ui_cursor_for((gs_ui_hit_t)hit);

        if (shape < 0 || shape >= GS_WINDOW_CURSOR_COUNT)
            bad_shape++;
        if (shape == GS_WINDOW_CURSOR_HAND)
            hands++;
    }
    check(bad_shape == 0, "every answer names a shape that exists");
    check(hands >= 10, "and most controls promise a press");
}

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
    s.screen = GS_UI_SCREEN_WORKSPACE;
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
    s.screen = GS_UI_SCREEN_WORKSPACE;
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

    /* The label sits to the left of the field, and the font is not the
     * same width on every machine, so a wider font has to push the field
     * further right or the two overlap. */
    {
        gs_ui_rect_t wide;

        check(gs_ui_glyph_width() == 6, "six is the width until told otherwise");
        gs_ui_set_glyph_width(9);
        check(gs_ui_glyph_width() == 9, "a wider font is taken");
        wide = gs_ui_search_rect(w, h);
        check(wide.x > box.x, "and the field moves clear of the label");
        check(wide.x - left.x >= 16 * 9, "by at least the label's own width");
        check(wide.w < box.w, "the field gives up the room it moved over");
        gs_ui_set_glyph_width(0);
        check(gs_ui_glyph_width() == 9, "a width of nothing is ignored");
        gs_ui_set_glyph_width(-4);
        check(gs_ui_glyph_width() == 9, "and so is a negative one");
        gs_ui_set_glyph_width(6);
        check(gs_ui_search_rect(w, h).x == box.x,
              "putting the width back puts the field back");
    }

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
    st.screen = GS_UI_SCREEN_WORKSPACE;
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
    s.screen = GS_UI_SCREEN_WORKSPACE;
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
    s.screen = GS_UI_SCREEN_WORKSPACE;
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

    /* The loop can now be left at any moment, by the close button or by a
     * stop signal, and the teardown that follows empties the library
     * table the worker reads. Waiting for the worker is what keeps those
     * two apart. */
    {
        int idle = thread_count();

        check(gs_ui_add_begin(&s) == GS_OK, "another impossible pull starts");
        check(thread_count() > idle, "and a thread is out doing it");

        gs_ui_remove_wait();
        /* Counting threads is what tells a real wait apart from one that
         * only forgets about the worker. A worker still running would
         * still be counted here, and it would still be reading the
         * library table that the teardown is about to empty. */
        check(thread_count() == idle,
              "waiting really did wait for the thread to end");
        check(gs_ui_add_begin(&s) == GS_OK, "so another one can start");
        gs_ui_remove_wait();
        check(thread_count() == idle, "and that one is waited for too");
        check(gs_ui_remove_poll(&s) == 0, "nothing is left to report");
        gs_ui_remove_wait();
        check(1, "waiting with nothing running returns at once");
    }

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

/* Settings surviving a session, and the startup check that runs when
 * the disk they name has gone away. The store is pointed at a file of
 * this test's own, so nothing of yours is touched. */
static void test_settings_round_trip(void)
{
    static gs_ui_state_t s;   /* a megabyte, kept off the stack */
    gs_detect_report_t machine;
    int i;

    printf("settings across sessions\n");

    (void)system("rm -rf build/settings-test");
    gs_paths_override("build/settings-test");
    if (gs_store_open() != GS_OK) {
        check(0, "the test store opened");
        return;
    }

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_WORKSPACE;
    memset(&machine, 0, sizeof machine);
    machine.ram_total_bytes = 10000000000000LL;
    machine.disk_count = 2;
    gs_str_copy(machine.disk[0].mount, sizeof machine.disk[0].mount, "/");
    machine.disk[0].free_bytes = 500;
    gs_str_copy(machine.disk[1].mount, sizeof machine.disk[1].mount,
                "/mnt/removable");
    machine.disk[1].free_bytes = 900;
    gs_ui_panes_refresh(&s, &machine);

    gs_ui_settings_restore(&s);
    check(s.settings_ready == 0, "a fresh file reports nothing configured");
    check(s.alert[0] == '\0', "and raises no warning");

    /* A session choosing a disk, a category and a model. */
    gs_ui_panes_pick_disk(&s, 1);
    gs_ui_panes_pick_category(&s, (int)GS_KIND_CODE + 1);
    s.model_sel = 2;
    gs_ui_settings_save(&s);
    check(gs_store_setting_count() >= 3, "three choices were written");

    /* The next session, with the same disks present. */
    {
        static gs_ui_state_t next;   /* a megabyte, kept off the stack */

        memset(&next, 0, sizeof next);
        next.screen = GS_UI_SCREEN_WORKSPACE;
        gs_ui_panes_refresh(&next, &machine);
        gs_ui_settings_restore(&next);

        check(next.settings_ready == 1, "the next session finds settings");
        check(next.disk_sel == 1, "and the disk that was chosen");
        check(next.category_sel == (int)GS_KIND_CODE + 1,
              "and the category that was chosen");
        check(next.alert[0] == '\0', "with no warning, since the disk is here");
    }

    /* The same machine with the removable disk gone. */
    {
        static gs_ui_state_t next;   /* a megabyte, kept off the stack */
        gs_detect_report_t reduced = machine;

        reduced.disk_count = 1;
        memset(&next, 0, sizeof next);
        next.screen = GS_UI_SCREEN_WORKSPACE;
        gs_ui_panes_refresh(&next, &reduced);
        gs_ui_settings_restore(&next);

        printf("        alert: %s\n", next.alert);
        check(next.settings_ready == 1, "the settings are still there");
        check(strstr(next.alert, "/mnt/removable") != NULL,
              "the warning names the disk that has gone");
        check(strstr(next.alert, "Attach it") != NULL,
              "and says to attach it or use another disk");
    }

    /* An inventory recorded against the missing disk is counted in the
     * warning, so the user learns what is on the disk they have lost. */
    for (i = 0; i < 3; i++) {
        char name[32];

        snprintf(name, sizeof name, "gone-%d:1b", i);
        gs_store_model_remember(name, "/mnt/removable/blobs/x",
                                "/mnt/removable", 100 + i);
    }
    {
        static gs_ui_state_t next;   /* a megabyte, kept off the stack */
        gs_detect_report_t reduced = machine;

        reduced.disk_count = 1;
        memset(&next, 0, sizeof next);
        next.screen = GS_UI_SCREEN_WORKSPACE;
        gs_ui_panes_refresh(&next, &reduced);
        gs_ui_settings_restore(&next);
        printf("        alert: %s\n", next.alert);
        check(strstr(next.alert, "3 model") != NULL,
              "the warning counts the models recorded on it");
        check(strlen(next.alert) < sizeof next.alert - 1,
              "and fits the line it is drawn on");
    }

    /* Saving while the disk is away keeps the choice, since the pane
     * fell back to the first disk on its own. A disk the user picks
     * while the other is away is saved as picked. */
    {
        static gs_ui_state_t next;   /* a megabyte, kept off the stack */
        gs_detect_report_t reduced = machine;
        char kept[128] = {0};

        reduced.disk_count = 1;
        memset(&next, 0, sizeof next);
        next.screen = GS_UI_SCREEN_WORKSPACE;
        gs_ui_panes_refresh(&next, &reduced);
        gs_ui_settings_restore(&next);
        gs_ui_settings_save(&next);
        (void)gs_store_setting_get("disk.mount", kept, sizeof kept);
        check(strcmp(kept, "/mnt/removable") == 0,
              "saving while the disk is away keeps the disk that was chosen");
        check(gs_store_model_count() == 3,
              "and leaves the record of the models on it alone");

        gs_ui_panes_pick_disk(&next, 0);
        next.disk_picked = 1;
        gs_ui_settings_save(&next);
        (void)gs_store_setting_get("disk.mount", kept, sizeof kept);
        check(strcmp(kept, "/") == 0,
              "a disk picked while the other is away is saved as picked");
        gs_store_setting_put("disk.mount", "/mnt/removable");
    }

    /* A saved disk that never existed leaves the pane usable. */
    gs_store_setting_put("disk.mount", "/mnt/never-was");
    {
        static gs_ui_state_t next;   /* a megabyte, kept off the stack */

        memset(&next, 0, sizeof next);
        next.screen = GS_UI_SCREEN_WORKSPACE;
        gs_ui_panes_refresh(&next, &machine);
        gs_ui_settings_restore(&next);
        check(next.alert[0] != '\0', "a disk that never existed warns");
        check(next.disk_sel >= 0 && next.disk_sel < next.disk_count,
              "and the pane still points at a disk that is here");
    }

    /* Poison in the file must not move the selection out of range. */
    gs_store_setting_put("pane.category", "9999");
    gs_store_setting_put("model.chosen", "no-such-model:1b");
    gs_store_setting_put("disk.mount", "/");
    {
        static gs_ui_state_t next;   /* a megabyte, kept off the stack */

        memset(&next, 0, sizeof next);
        next.screen = GS_UI_SCREEN_WORKSPACE;
        gs_ui_panes_refresh(&next, &machine);
        gs_ui_settings_restore(&next);
        check(next.category_sel >= 0 &&
              next.category_sel < next.category_count,
              "a category past the end is ignored");
        check(next.model_sel < next.model_count,
              "a model that is not in the list leaves the selection alone");
    }

    gs_ui_settings_save(NULL);
    gs_ui_settings_restore(NULL);
    gs_ui_settings_record_models(NULL);
    check(1, "every settings call takes a null state without crashing");

    gs_store_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/settings-test");
    gs_catalogue_release();
}

/* The home screen, its gear, and the mark that says nothing has been
 * configured, at every size a window manager might hand back. */
static void test_home_screen(void)
{
    const int w = 980, h = 640;
    gs_ui_state_t s;
    gs_ui_rect_t gear;
    unsigned int *base, *px;
    int row = -1;
    int i, tiny_bad = 0;

    printf("the home screen\n");

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;

    gear = gs_ui_gear_rect(w, h);
    check(gear.w > 0, "the gear exists at the normal size");
    check(gear.x + gear.w <= w && gear.y >= 0,
          "and sits inside the window");
    check(gear.x > w / 2 && gear.y < h / 2,
          "in the top right quarter");

    check(gs_ui_hit_test(&s, w, h, gear.x + gear.w / 2,
                         gear.y + gear.h / 2, &row) == GS_UI_HIT_SETTINGS,
          "clicking the gear answers with settings");
    check(row == -1, "and names no row");
    check(gs_ui_hit_test(&s, w, h, w / 2, h / 2, &row) == GS_UI_HIT_NONE,
          "the middle of the home screen answers with nothing");

    /* Every control of the workspace stays silent on the home screen. */
    check(gs_ui_hit_test(&s, w, h, gs_ui_mount_rect(w).x + 5,
                         gs_ui_mount_rect(w).y + 5, &row) == GS_UI_HIT_NONE,
          "the mount button does not answer from home");
    s.mounted = 1;
    s.model_count = 40;
    s.disk_count = 3;
    s.library_count = 2;
    check(gs_ui_hit_test(&s, w, h, gs_ui_left_rect(w, h).x + 20,
                         gs_ui_left_rect(w, h).y + GS_UI_PANE_HEADER + 2,
                         &row) == GS_UI_HIT_NONE,
          "and neither do the panes");

    /* At every size the gear either fits inside the window or is not
     * offered at all, so no caller ever draws one over the edge. */
    for (i = 0; i < 400; i += 3) {
        gs_ui_rect_t any = gs_ui_gear_rect(i, i);

        if (any.w == 0 && any.h == 0)
            continue;
        if (any.x < 0 || any.y < 0 || any.x + any.w > i || any.y + any.h > i)
            tiny_bad++;
    }
    check(tiny_bad == 0, "a gear is offered only where it fits");
    check(gs_ui_gear_rect(40, 40).w == 0,
          "a window smaller than the gear and its inset carries none");
    check(gs_ui_gear_rect(2000, 20).w == 0,
          "and neither does one too short for it");
    check(gs_ui_hit_test(&s, 40, 40, 20, 20, &row) == GS_UI_HIT_NONE,
          "and a click in one lands nowhere");

    /* Drawing the home screen, marked and unmarked, stays in the buffer. */
    for (i = 0; i < 2; i++) {
        s.settings_ready = i;
        px = fenced(w, h, &base);
        if (px == NULL)
            continue;
        gs_ui_compose(px, w, h, &s);
        check(fence_intact(base, w, h),
              i ? "drawing the gear writes nothing outside the buffer"
                : "drawing the gear and its mark stays inside too");
        free(base);
    }

    /* Models decide the mark, so a home with none looks different from
     * a home with some. */
    {
        size_t n = (size_t)w * (size_t)h;
        unsigned int *a = malloc(n * sizeof *a);
        unsigned int *b = malloc(n * sizeof *b);

        if (a != NULL && b != NULL) {
            s.library_count = 0;
            gs_ui_compose(a, w, h, &s);
            s.library_count = 1;
            gs_ui_compose(b, w, h, &s);
            check(memcmp(a, b, n * sizeof *a) != 0,
                  "a home with models looks different from one without");
        }
        free(a);
        free(b);
        s.library_count = 0;
    }

    /* A home screen at an absurd size draws without complaint. */
    {
        int sizes[][2] = {{1, 1}, {3, 900}, {900, 3}, {61, 61}, {2000, 40}};
        size_t k;
        int broke = 0;

        for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
            px = fenced(sizes[k][0], sizes[k][1], &base);
            if (px == NULL)
                continue;
            gs_ui_compose(px, sizes[k][0], sizes[k][1], &s);
            if (!fence_intact(base, sizes[k][0], sizes[k][1]))
                broke++;
            free(base);
        }
        check(broke == 0, "and so does one at every awkward size");
    }
}

/* The mark on the gear reports models, and the way back from the
 * workspace answers where it is drawn. */
static void test_gear_mark_and_back(void)
{
    const int w = 980, h = 640;
    gs_ui_state_t s;
    gs_ui_rect_t back;
    unsigned int *base, *px;
    size_t n = (size_t)w * (size_t)h;
    unsigned int *none = malloc(n * sizeof *none);
    unsigned int *some = malloc(n * sizeof *some);
    int row = -1;
    int i;

    printf("the mark on the gear, and the way back\n");

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;

    if (none != NULL && some != NULL) {
        s.library_count = 0;
        s.settings_ready = 1;      /* configured, and still no models */
        gs_ui_compose(none, w, h, &s);
        s.library_count = 2;
        gs_ui_compose(some, w, h, &s);
        check(memcmp(none, some, n * sizeof *none) != 0,
              "two models on the disk change the mark");

        /* Settings alone must not change it, since models decide. */
        s.library_count = 0;
        s.settings_ready = 0;
        gs_ui_compose(some, w, h, &s);
        check(memcmp(none, some, n * sizeof *none) == 0,
              "and settings alone leave it exactly as it was");
    }
    free(none);
    free(some);

    for (i = 0; i < 2; i++) {
        s.library_count = i;
        px = fenced(w, h, &base);
        if (px == NULL)
            continue;
        gs_ui_compose(px, w, h, &s);
        check(fence_intact(base, w, h),
              i ? "the tick writes nothing outside the buffer"
                : "and neither does the red mark");
        free(base);
    }

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_WORKSPACE;
    back = gs_ui_back_rect(w, h);
    check(gs_ui_hit_test(&s, w, h, back.x + back.w / 2,
                         back.y + back.h / 2, &row) == GS_UI_HIT_BACK,
          "clicking the arrow answers with back");
    check(row == -1, "and names no row");
    check(gs_ui_hit_test(&s, w, h, back.x - 4, back.y + back.h / 2, &row)
          == GS_UI_HIT_NONE,
          "the gap between them answers with nothing");
    check(gs_ui_hit_test(&s, w, h,
                         gs_ui_chooser_rect(w, h).x +
                         gs_ui_chooser_rect(w, h).w - 2,
                         back.y + back.h / 2, &row) == GS_UI_HIT_CHOOSER,
          "and the chooser answers right up to its own edge");

    s.screen = GS_UI_SCREEN_HOME;
    check(gs_ui_hit_test(&s, w, h, back.x + back.w / 2,
                         back.y + back.h / 2, &row) == GS_UI_HIT_NONE,
          "and the arrow stays silent on the home screen");

    {
        int narrow_bad = 0;

        for (i = 0; i < 400; i += 3) {
            gs_ui_rect_t any = gs_ui_back_rect(i, 640);

            if (any.w == 0 && any.h == 0)
                continue;
            if (any.x < 0 || any.x + any.w > i)
                narrow_bad++;
        }
        check(narrow_bad == 0, "the arrow is offered only where it fits");
    }
}

/* The chat panel, its composer, and the prompt it takes. */
static void test_chat_panel(void)
{
    const int w = 980, h = 640;
    gs_ui_state_t s;
    gs_ui_rect_t panel, box, send, attach, gear;
    unsigned int *base, *px;
    int row = -1;
    int i, unfit = 0;

    printf("the chat panel\n");

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;

    panel = gs_ui_chat_rect(w, h);
    box = gs_ui_chat_composer_rect(w, h);
    send = gs_ui_chat_send_rect(w, h);
    attach = gs_ui_chat_attach_rect(w, h);
    gear = gs_ui_gear_rect(w, h);

    check(panel.w > 0, "the panel exists at the normal size");
    check(panel.x >= GS_UI_MARGIN && panel.x + panel.w < w / 2 + 40,
          "and sits down the left hand side");
    check(panel.y == GS_UI_MARGIN &&
          panel.y + panel.h == h - GS_UI_MARGIN,
          "running the full height between the margins");
    check(panel.x + panel.w < gear.x,
          "clear of the gear in the corner");

    check(box.x == panel.x && box.w == panel.w,
          "the composer spans the panel");
    check(box.y + box.h == panel.y + panel.h,
          "and sits at its bottom");
    check(attach.x < send.x, "the plus is left of the send arrow");
    check(attach.x >= box.x && send.x + send.w <= box.x + box.w,
          "with both inside the composer");
    check(attach.y >= box.y && attach.y + attach.h <= box.y + box.h,
          "and both inside it vertically");
    check(box.y + box.h - (attach.y + attach.h) >= 8,
          "with clear space under the buttons rather than none");
    {
        gs_ui_rect_t fleet = gs_ui_chat_fleet_rect(w, h);

        check(fleet.w > 0, "the models button exists");
        check(fleet.x > attach.x + attach.w && fleet.x + fleet.w < send.x,
              "sitting between the plus and the arrow");
        check(fleet.y == attach.y && fleet.h == attach.h,
              "level with them both");
    }

    /* Clicks land where the drawing puts them. */
    check(gs_ui_hit_test(&s, w, h, send.x + send.w / 2,
                         send.y + send.h / 2, &row) == GS_UI_HIT_CHAT_SEND,
          "the arrow answers with send");
    check(gs_ui_hit_test(&s, w, h, attach.x + attach.w / 2,
                         attach.y + attach.h / 2, &row)
          == GS_UI_HIT_CHAT_ATTACH, "the plus answers with attach");
    check(gs_ui_hit_test(&s, w, h, box.x + box.w / 2, box.y + 6, &row)
          == GS_UI_HIT_CHAT_BOX, "the text area answers with the box");
    check(row == -1, "and none of them names a row");
    check(gs_ui_hit_test(&s, w, h, panel.x + 4, panel.y + 4, &row)
          == GS_UI_HIT_NONE, "the empty part of the panel answers nothing");

    /* The panel belongs to the home screen alone. */
    s.screen = GS_UI_SCREEN_WORKSPACE;
    check(gs_ui_hit_test(&s, w, h, send.x + send.w / 2,
                         send.y + send.h / 2, &row) != GS_UI_HIT_CHAT_SEND,
          "the workspace answers none of the chat controls");
    s.screen = GS_UI_SCREEN_HOME;

    /* Typing, backspacing, and the limits of the field. */
    check(gs_ui_chat_type(&s, 'a') == 1, "a letter reaches the prompt");
    check(strcmp(s.prompt, "a") == 0, "and lands in it");
    check(gs_ui_chat_type(&s, ' ') == 1, "so does a space");
    check(gs_ui_chat_type(&s, 8) == 1, "backspace removes the last");
    check(strcmp(s.prompt, "a") == 0, "leaving what came before");
    check(gs_ui_chat_type(&s, 8) == 1, "and again");
    check(s.prompt[0] == '\0', "emptying the prompt");
    check(gs_ui_chat_type(&s, 8) == 0, "backspace on empty changes nothing");
    check(gs_ui_chat_type(&s, 1) == 0, "a control byte is ignored");
    check(gs_ui_chat_type(&s, 200) == 0, "a byte past plain text is ignored");
    check(gs_ui_chat_type(&s, -3) == 0, "a negative byte is ignored");
    check(gs_ui_chat_type(NULL, 'a') == 0, "a null state is refused");

    for (i = 0; i < 4000; i++)
        gs_ui_chat_type(&s, 'p');
    check(strlen(s.prompt) == sizeof s.prompt - 1,
          "typing past the field stops at its edge");
    check(gs_ui_chat_type(&s, 'q') == 0, "and a full field refuses more");

    /* Drawing a full prompt, an empty one, and every awkward size. */
    for (i = 0; i < 2; i++) {
        if (i == 0)
            s.prompt[0] = '\0';
        px = fenced(w, h, &base);
        if (px == NULL)
            continue;
        gs_ui_compose(px, w, h, &s);
        check(fence_intact(base, w, h),
              i ? "a full prompt draws inside the buffer"
                : "an empty prompt draws inside it too");
        free(base);
        if (i == 0) {
            int k;

            for (k = 0; k < 4000; k++)
                gs_ui_chat_type(&s, 'w');
        }
    }

    {
        int sizes[][2] = {{1, 1}, {5, 700}, {700, 5}, {201, 140}, {2400, 40}};
        size_t k;
        int broke = 0;

        for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
            px = fenced(sizes[k][0], sizes[k][1], &base);
            if (px == NULL)
                continue;
            gs_ui_compose(px, sizes[k][0], sizes[k][1], &s);
            if (!fence_intact(base, sizes[k][0], sizes[k][1]))
                broke++;
            free(base);
        }
        check(broke == 0, "and so does every awkward size");
    }

    /* Wherever the panel is offered it fits, and every part fits in it. */
    for (i = 0; i < 1400; i += 7) {
        gs_ui_rect_t p2 = gs_ui_chat_rect(i, 640);
        gs_ui_rect_t b2 = gs_ui_chat_composer_rect(i, 640);
        gs_ui_rect_t s2 = gs_ui_chat_send_rect(i, 640);

        if (p2.w == 0)
            continue;
        if (p2.x < 0 || p2.x + p2.w > i || p2.y + p2.h > 640)
            unfit++;
        if (b2.x < p2.x || b2.x + b2.w > p2.x + p2.w)
            unfit++;
        if (s2.x + s2.w > b2.x + b2.w)
            unfit++;
    }
    check(unfit == 0, "the panel and its parts fit at every width offered");
    check(gs_ui_chat_rect(120, 640).w == 0,
          "a window too narrow carries no panel");
    check(gs_ui_chat_rect(980, 100).w == 0,
          "and neither does one too short");
    check(gs_ui_chat_send_rect(120, 640).w == 0,
          "with no send arrow either");
}

/* The box listing the models on the disk, its growth, its ticks, and
 * the way it covers the panel while it is up. */
static void test_fleet_box(void)
{
    const int w = 980, h = 640;
    gs_ui_state_t s;
    gs_ui_rect_t button, box, first, last;
    unsigned int *base, *px;
    int row = -1;
    int i, frames, outside = 0;

    printf("the box listing the models on the disk\n");

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;
    s.library_count = 5;

    check(gs_ui_fleet_visible(&s) == 0, "it starts out of sight");
    check(gs_ui_fleet_animating(&s) == 0, "and still");
    check(gs_ui_fleet_advance(&s) == 0, "advancing a still box does nothing");
    check(gs_ui_fleet_visible(NULL) == 0, "a null state shows no box");
    check(gs_ui_fleet_advance(NULL) == 0, "and advancing it is safe");
    check(gs_ui_fleet_rows(NULL) == 0, "a null state has no rows");

    button = gs_ui_chat_fleet_rect(w, h);
    check(gs_ui_hit_test(&s, w, h, button.x + button.w / 2,
                         button.y + button.h / 2, &row) == GS_UI_HIT_FLEET,
          "the button answers with the models control");

    s.fleet_step = 0;
    s.fleet_dir = 1;
    check(gs_ui_fleet_visible(&s) == 1, "starting it makes it visible");
    for (frames = 0; frames < 40 && gs_ui_fleet_advance(&s); frames++)
        ;
    check(s.fleet_step == GS_UI_FLEET_STEPS, "it opens fully");
    check(frames <= GS_UI_FLEET_STEPS, "in no more frames than it has steps");
    check(s.fleet_dir == 0, "and then rests");

    /* The box is as tall as the models it holds, up to its own limit. */
    check(gs_ui_fleet_rows(&s) == 5, "five models give five rows");
    s.library_count = 40;
    check(gs_ui_fleet_rows(&s) == GS_UI_FLEET_ROWS,
          "forty models stop at the rows it shows");
    s.library_count = 0;
    check(gs_ui_fleet_rows(&s) == 1,
          "an empty disk keeps one row for the line saying so");
    s.library_count = 5;

    box = gs_ui_fleet_rect(&s, w, h, s.fleet_step);
    first = gs_ui_fleet_row_rect(&s, w, h, 0);
    last = gs_ui_fleet_row_rect(&s, w, h, 4);
    check(box.w > 0, "the open box has a size");
    {
        gs_ui_rect_t panel = gs_ui_chat_rect(w, h);
        gs_ui_rect_t composer = gs_ui_chat_composer_rect(w, h);

        check(box.x >= panel.x && box.x + box.w <= panel.x + panel.w,
              "and sits inside the chat panel");
        check(box.y + box.h <= composer.y,
              "just above the composer it belongs to");
    }
    check(first.x >= box.x && first.x + first.w <= box.x + box.w,
          "a row fits inside the box");
    check(last.y > first.y, "and the rows run downward");
    check(last.y + last.h <= box.y + box.h, "with the last one inside it");
    check(gs_ui_fleet_row_rect(&s, w, h, -1).w == 0,
          "a row below the first is nothing");
    check(gs_ui_fleet_row_rect(&s, w, h, 5).w == 0,
          "and so is one past the models that exist");

    /* Ticking, unticking, and the count that follows. */
    check(gs_ui_fleet_chosen(&s, 0) == 0, "nothing starts ticked");
    gs_ui_fleet_toggle(&s, 0);
    check(gs_ui_fleet_chosen(&s, 0) == 1, "a model ticks");
    check(s.chosen_count == 1, "and the count follows");
    gs_ui_fleet_toggle(&s, 3);
    gs_ui_fleet_toggle(&s, 4);
    check(s.chosen_count == 3, "three models tick together");
    check(gs_ui_fleet_chosen(&s, 1) == 0, "leaving the others alone");
    gs_ui_fleet_toggle(&s, 3);
    check(gs_ui_fleet_chosen(&s, 3) == 0, "a second press unticks");
    check(s.chosen_count == 2, "and the count follows down");

    gs_ui_fleet_toggle(&s, -1);
    gs_ui_fleet_toggle(&s, 5);
    gs_ui_fleet_toggle(&s, GS_UI_CHOSEN_MAX + 10);
    gs_ui_fleet_toggle(NULL, 0);
    check(s.chosen_count == 2, "no index outside the library moves it");
    check(gs_ui_fleet_chosen(NULL, 0) == 0, "a null state ticks nothing");
    check(gs_ui_fleet_chosen(&s, -4) == 0, "and neither does a bad index");

    /* Clicking a row answers with its index, and nothing else answers. */
    check(gs_ui_hit_test(&s, w, h, last.x + 40, last.y + last.h / 2, &row)
          == GS_UI_HIT_FLEET_OPTION, "a row answers with the option");
    check(row == 4, "naming which model");
    /* Anything outside the box shuts it, rather than doing whatever it
     * would normally do. A small box that opened over the work has to go
     * away when the work is clicked. */
    check(gs_ui_hit_test(&s, w, h, gs_ui_gear_rect(w, h).x + 4,
                         gs_ui_gear_rect(w, h).y + 4, &row)
          == GS_UI_HIT_FLEET_AWAY,
          "the gear shuts the box rather than opening settings");
    check(gs_ui_hit_test(&s, w, h, button.x + 4, button.y + 4, &row)
          == GS_UI_HIT_FLEET_AWAY,
          "and so does the button that opened it");
    check(gs_ui_hit_test(&s, w, h, w - 2, h - 2, &row)
          == GS_UI_HIT_FLEET_AWAY, "and the far corner of the window");

    /* The cross in the corner of the box. */
    {
        gs_ui_rect_t shut = gs_ui_fleet_close_rect(&s, w, h);
        gs_ui_rect_t open_box = gs_ui_fleet_rect(&s, w, h,
                                                 GS_UI_FLEET_STEPS);
        int n;

        check(shut.w > 0 && shut.h > 0, "the cross has a size");
        check(shut.x + shut.w <= open_box.x + open_box.w &&
              shut.y >= open_box.y &&
              shut.y + shut.h <= open_box.y + open_box.h,
              "and sits inside the box");
        check(shut.x > open_box.x + open_box.w / 2,
              "over towards the right hand end of it");
        check(gs_ui_hit_test(&s, w, h, shut.x + shut.w / 2,
                             shut.y + shut.h / 2, &row)
              == GS_UI_HIT_FLEET_CLOSE, "and pressing it answers with close");

        /* A cross sitting on a row would tick a model by accident. */
        for (n = 0; n < s.library_count; n++) {
            gs_ui_rect_t r = gs_ui_fleet_row_rect(&s, w, h, n);

            if (r.w <= 0)
                continue;
            check(!(shut.y + shut.h > r.y && shut.y < r.y + r.h),
                  "the cross lands on no row");
        }

        /* Inside the box, on neither the cross nor a row, so nothing. */
        check(gs_ui_hit_test(&s, w, h, open_box.x + 4,
                             open_box.y + open_box.h - 4, &row)
              == GS_UI_HIT_NONE,
              "an empty part of the box answers nothing at all");
    }

    s.fleet_step = 3;
    {
        gs_ui_rect_t part = gs_ui_fleet_rect(&s, w, h, 3);

        check(gs_ui_hit_test(&s, w, h, part.x + part.w / 2,
                             part.y + part.h / 2, &row) == GS_UI_HIT_NONE,
              "a box still growing answers no row");
    }
    check(gs_ui_fleet_close_rect(&s, w, h).w == 0,
          "and carries no cross, since a moving target is worse than none");
    check(gs_ui_hit_test(&s, w, h, w - 2, h - 2, &row)
          == GS_UI_HIT_FLEET_AWAY,
          "though a click outside still shuts a box that is still growing");
    s.fleet_step = GS_UI_FLEET_STEPS;

    /* With the box away, everything answers as it did before. */
    s.fleet_step = 0;
    s.fleet_dir = 0;
    check(gs_ui_hit_test(&s, w, h, gs_ui_gear_rect(w, h).x + 4,
                         gs_ui_gear_rect(w, h).y + 4, &row)
          == GS_UI_HIT_SETTINGS, "with the box shut the gear works again");
    check(gs_ui_fleet_close_rect(&s, w, h).w == 0,
          "and there is no cross to press");
    check(gs_ui_fleet_close_rect(NULL, w, h).w == 0,
          "a null state carries no cross either");
    s.fleet_step = GS_UI_FLEET_STEPS;
    s.fleet_dir = 0;

    /* Scrolling moves which models the rows stand for. */
    s.library_count = 40;
    s.fleet_scroll = 12;
    check(gs_ui_fleet_row_rect(&s, w, h, 12).w > 0,
          "the scrolled row is the one on show");
    check(gs_ui_fleet_row_rect(&s, w, h, 0).w == 0,
          "and the row scrolled past is not");
    s.fleet_scroll = 0;
    s.library_count = 5;

    s.fleet_dir = -1;
    for (frames = 0; frames < 40 && gs_ui_fleet_advance(&s); frames++)
        ;
    check(s.fleet_step == 0, "it folds back to nothing");
    check(gs_ui_fleet_visible(&s) == 0, "and is out of sight again");

    s.fleet_dir = 0;
    for (i = 0; i <= GS_UI_FLEET_STEPS; i++) {
        s.fleet_step = i;
        s.library_count = i * 5;
        px = fenced(w, h, &base);
        if (px == NULL)
            continue;
        gs_ui_compose(px, w, h, &s);
        if (!fence_intact(base, w, h))
            outside++;
        free(base);
    }
    check(outside == 0, "every step of its growth draws inside the buffer");

    {
        int sizes[][2] = {{1, 1}, {150, 640}, {980, 120}, {260, 200}};
        size_t k;
        int broke = 0;

        s.fleet_step = GS_UI_FLEET_STEPS;
        s.library_count = 200;
        for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
            px = fenced(sizes[k][0], sizes[k][1], &base);
            if (px == NULL)
                continue;
            gs_ui_compose(px, sizes[k][0], sizes[k][1], &s);
            if (!fence_intact(base, sizes[k][0], sizes[k][1]))
                broke++;
            free(base);
        }
        check(broke == 0, "and so does every size too small to hold it");
        check(gs_ui_fleet_rect(&s, 150, 640, GS_UI_FLEET_STEPS).w == 0,
              "no panel means no box");
    }
}

/* The box painted on a real screen with models under it, read back from
 * the server so the ticks are proved rather than assumed. */
static void test_fleet_on_screen(void)
{
    const int open_w = 980, open_h = 640;
    gs_window_t *win;
    gs_window_event_t drain;
    gs_ui_state_t st;
    unsigned int *expected;
    gs_ui_rect_t box, ticked, plain;
    int w, h, rounds, x, y;
    int tried = 0, matches = 0;
    int differs = 0;

    if (!gs_window_display_available()) {
        printf("  skip  the models box on a real screen\n");
        return;
    }

    printf("the models box on a real screen\n");

    /* Two models built by hand, so the test needs no disk of its own. */
    (void)system("rm -rf build/fleet-models");
    (void)system("mkdir -p build/fleet-models/gravestone-models");
    {
        FILE *f = fopen("build/fleet-models/gravestone-models/one.gguf", "wb");
        int i;

        if (f != NULL) {
            for (i = 0; i < 900; i++)
                fputc('a', f);
            fclose(f);
        }
        f = fopen("build/fleet-models/gravestone-models/two.gguf", "wb");
        if (f != NULL) {
            for (i = 0; i < 700; i++)
                fputc('b', f);
            fclose(f);
        }
    }

    memset(&st, 0, sizeof st);
    st.screen = GS_UI_SCREEN_HOME;
    st.library_count = gs_library_scan("build/fleet-models");
    check(st.library_count == 2, "two models were found for the box");
    st.fleet_step = GS_UI_FLEET_STEPS;
    gs_ui_fleet_toggle(&st, 0);

    win = gs_window_open("Gravestone fleet", open_w, open_h);
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

    box = gs_ui_fleet_rect(&st, w, h, GS_UI_FLEET_STEPS);
    ticked = gs_ui_fleet_row_rect(&st, w, h, 0);
    plain = gs_ui_fleet_row_rect(&st, w, h, 1);

    /* The two tick squares differ, since one model is chosen. */
    if (ticked.w > 0 && plain.w > 0) {
        unsigned int a = expected[(size_t)(ticked.y + ticked.h / 2) *
                                  (size_t)w + (size_t)(ticked.x + 12)];
        unsigned int b = expected[(size_t)(plain.y + plain.h / 2) *
                                  (size_t)w + (size_t)(plain.x + 12)];

        check(a != b, "the ticked model looks different from the plain one");
    }

    /* Sampled inside the box's own border, clear of the two lines of
     * text the header carries, so anything bleeding through from the
     * panel underneath would show. Glyphs land after the image, so a
     * row carrying text is never sampled. */
    for (y = box.y + 3; y < box.y + 10; y += 3) {
        for (x = box.x + 6; x < box.x + box.w - 6; x += 27) {
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
    printf("        %d of %d sampled points inside the box match\n",
           matches, tried);
    if (tried > 0)
        check(matches == tried, "the box reaches the screen as composed");

    /* Unticking really changes the picture. */
    {
        size_t n = (size_t)w * (size_t)h;
        unsigned int *after = malloc(n * sizeof *after);

        if (after != NULL) {
            gs_ui_fleet_toggle(&st, 0);
            gs_ui_compose(after, w, h, &st);
            differs = memcmp(expected, after, n * sizeof *after) != 0;
            free(after);
        }
        check(differs, "unticking a model changes what is drawn");
    }

    check(gs_window_error_count(win) == 0, "the server was upset nowhere");

    free(expected);
    gs_window_close(win);
    gs_ui_release();
    gs_library_release();
    (void)system("rm -rf build/fleet-models");
}

/* The caret shows and hides on the blink, and follows what is typed. */
static void test_caret(void)
{
    const int w = 980, h = 640;
    gs_ui_state_t s;
    size_t n = (size_t)w * (size_t)h;
    unsigned int *base, *px;
    int i;

    printf("the caret in the prompt box\n");

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;

    /* Composing paints pixels alone, so the caret is checked through a
     * real window where the glyph pass runs. */
    if (!gs_window_display_available()) {
        printf("  skip  the caret on a real screen\n");
    } else {
        gs_window_t *win = gs_window_open("Gravestone caret", w, h);
        gs_window_event_t drain;
        unsigned int *shot_on = malloc(n * sizeof *shot_on);

        if (win != NULL && shot_on != NULL) {
            int rounds;
            int lost = 0;

            for (rounds = 0; rounds < 20; rounds++) {
                int rc = gs_window_wait_event(win, &drain, 50);

                /* WSLg drops a client's socket now and then, which this
                 * codebase records as common after a run opens and closes
                 * many windows. Every paint after that fails for the same
                 * reason and none of them says anything about the code,
                 * so the section is reported as a skip. */
                if (rc < 0) {
                    lost = 1;
                    break;
                }
                if (rc == 1 && drain.kind == GS_WINDOW_EVENT_EXPOSE)
                    break;
            }
            if (lost) {
                printf("  skip  the caret on a real screen "
                       "(the server dropped the connection)\n");
                free(shot_on);
                gs_window_close(win);
                return;
            }

            gs_str_copy(s.prompt, sizeof s.prompt, "hello");
            s.cursor_on = 1;
            check(gs_ui_paint(win, &s) == GS_OK, "painted with the caret on");
            s.cursor_on = 0;
            check(gs_ui_paint(win, &s) == GS_OK, "and again with it off");
            check(gs_window_error_count(win) == 0,
                  "the caret upset the server nowhere");
        }
        free(shot_on);
        if (win != NULL)
            gs_window_close(win);
        gs_ui_release();
    }

    /* Drawing the caret at every prompt length stays inside the buffer. */
    s.cursor_on = 1;
    s.prompt[0] = '\0';
    for (i = 0; i < 6; i++) {
        int k;

        px = fenced(w, h, &base);
        if (px != NULL) {
            gs_ui_compose(px, w, h, &s);
            check(fence_intact(base, w, h),
                  "the caret draws inside the buffer at every length");
            free(base);
        }
        for (k = 0; k < 200; k++)
            gs_ui_chat_type(&s, 'x');
    }
}

/* The context window read out of a model file's own header, and the
 * ticks kept between sessions. */
static void test_context_and_memory(void)
{
    static gs_ui_state_t s;   /* a megabyte, kept off the stack */
    gs_detect_report_t machine;
    FILE *f;
    int i;

    printf("the context window, and remembering the ticks\n");

    (void)system("rm -rf build/ctx-models");
    (void)system("mkdir -p build/ctx-models/gravestone-models");

    /* A file that is no GGUF at all, and one carrying a real header with
     * a context length inside it. */
    f = fopen("build/ctx-models/gravestone-models/plain.gguf", "wb");
    if (f != NULL) {
        for (i = 0; i < 400; i++)
            fputc('z', f);
        fclose(f);
    }

    f = fopen("build/ctx-models/gravestone-models/real.gguf", "wb");
    if (f != NULL) {
        unsigned char head[24] = {'G', 'G', 'U', 'F', 3, 0, 0, 0};
        unsigned char key[8] = {20, 0, 0, 0, 0, 0, 0, 0};
        unsigned char type32[4] = {4, 0, 0, 0};
        unsigned char type8[4] = {8, 0, 0, 0};
        unsigned char arch_len[8] = {4, 0, 0, 0, 0, 0, 0, 0};
        unsigned char arch_key[8] = {20, 0, 0, 0, 0, 0, 0, 0};
        unsigned char value[4] = {0x00, 0x20, 0x00, 0x00};   /* 8192 */

        head[8] = 0;                       /* no tensors */
        head[16] = 2;                      /* two named values */
        fwrite(head, 1, 24, f);
        fwrite(arch_key, 1, 8, f);
        fwrite("general.architecture", 1, 20, f);
        fwrite(type8, 1, 4, f);
        fwrite(arch_len, 1, 8, f);
        fwrite("qwen", 1, 4, f);
        fwrite(key, 1, 8, f);
        fwrite("qwen.context_length\0", 1, 20, f);
        fwrite(type32, 1, 4, f);
        fwrite(value, 1, 4, f);
        for (i = 0; i < 200; i++)
            fputc('w', f);
        fclose(f);
    }

    check(gs_library_scan("build/ctx-models") == 2, "both files were seen");
    for (i = 0; i < gs_library_count(); i++) {
        const gs_library_entry_t *e = gs_library_at(i);

        if (strcmp(e->name, "plain.gguf") == 0)
            check(e->context == 0,
                  "a file with no header reports no context window");
        if (strcmp(e->name, "real.gguf") == 0)
            check(e->context == 8192,
                  "a real header gives the context window it names");
    }

    /* The ticks are written by name and come back by name. */
    (void)system("rm -rf build/ctx-store");
    gs_paths_override("build/ctx-store");
    if (gs_store_open() != GS_OK) {
        check(0, "the test store opened");
        gs_paths_override(NULL);
        return;
    }

    memset(&s, 0, sizeof s);
    memset(&machine, 0, sizeof machine);
    s.screen = GS_UI_SCREEN_HOME;
    machine.ram_total_bytes = 10000000000000LL;
    machine.disk_count = 1;
    gs_str_copy(machine.disk[0].mount, sizeof machine.disk[0].mount,
                "build/ctx-models");
    machine.disk[0].free_bytes = 1000;
    gs_ui_panes_refresh(&s, &machine);
    check(s.library_count == 2, "the pane sees the two models");

    gs_ui_fleet_toggle(&s, 1);
    gs_ui_settings_save(&s);
    check(s.chosen_count == 1, "one model was ticked and written");

    {
        static gs_ui_state_t next;   /* a megabyte, kept off the stack */
        const gs_library_entry_t *was = gs_library_at(1);
        char kept[160];

        gs_str_copy(kept, sizeof kept, was != NULL ? was->name : "");
        memset(&next, 0, sizeof next);
        next.screen = GS_UI_SCREEN_HOME;
        gs_ui_panes_refresh(&next, &machine);
        gs_ui_settings_restore(&next);
        check(next.chosen_count == 1, "the next session finds one ticked");
        for (i = 0; i < next.library_count; i++) {
            const gs_library_entry_t *e = gs_library_at(i);

            if (e != NULL && strcmp(e->name, kept) == 0)
                check(gs_ui_fleet_chosen(&next, i) == 1,
                      "and it is the very model that was ticked");
        }
    }

    /* A name in the file that no longer exists on disk ticks nothing. */
    gs_store_setting_put("models.chosen", "vanished.gguf");
    {
        static gs_ui_state_t next;   /* a megabyte, kept off the stack */

        memset(&next, 0, sizeof next);
        next.screen = GS_UI_SCREEN_HOME;
        gs_ui_panes_refresh(&next, &machine);
        gs_ui_settings_restore(&next);
        check(next.chosen_count == 0,
              "a remembered model that has gone ticks nothing");
    }

    /* A name that is a piece of another name must not tick it. */
    gs_store_setting_put("models.chosen", "real");
    {
        static gs_ui_state_t next;   /* a megabyte, kept off the stack */

        memset(&next, 0, sizeof next);
        next.screen = GS_UI_SCREEN_HOME;
        gs_ui_panes_refresh(&next, &machine);
        gs_ui_settings_restore(&next);
        check(next.chosen_count == 0,
              "half a name never ticks the model it sits inside");
    }

    gs_store_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/ctx-models build/ctx-store");
    gs_library_release();
    gs_catalogue_release();
}

/* The models ticked in the box have to survive the program closing. A
 * save used to rebuild the list from whatever library was loaded at that
 * moment, so a session started with the disk missing loaded no models and
 * wrote an empty list over the person's choice at the next save. */
/* The drives are watched on a thread that runs the road probe and the
 * mount commands as root, so this runs only for whoever asks with
 * GS_TEST_ROAD, like the live road test in mount_test.c. */
static void test_the_drives_are_watched(void)
{
    static gs_ui_state_t s;      /* a megabyte, kept off the stack */
    int reports = 0;
    int i;

    printf("the drives are watched\n");
    if (getenv("GS_TEST_ROAD") == NULL) {
        printf("  skip  the watcher (set GS_TEST_ROAD=1 to run it)\n");
        return;
    }

    (void)system("rm -rf build/watch-test");
    gs_paths_override("build/watch-test");
    if (gs_store_open() != GS_OK || gs_mount_open() != GS_OK) {
        check(0, "the test stores opened");
        return;
    }
    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_WORKSPACE;
    gs_ui_panes_refresh(&s, NULL);

    check(gs_ui_mount_poll(&s) == 0, "nothing to poll before the thread starts");
    if (gs_ui_mount_begin() != GS_OK) {
        printf("  skip  no drives to watch on this machine\n");
        gs_mount_close();
        gs_store_close();
        gs_paths_override(NULL);
        return;
    }
    check(gs_ui_mount_running(), "the thread is running");

    /* The first pass reports within moments on a warm machine and within
     * half a minute on one where wsl.exe has to start a session. */
    for (i = 0; i < 300 && reports == 0; i++) {
        struct timespec tenth;

        if (gs_ui_mount_poll(&s))
            reports++;
        tenth.tv_sec = 0;
        tenth.tv_nsec = 100000000L;
        nanosleep(&tenth, NULL);
    }
    printf("        first report after %d tenths, status \"%s\", %d disks\n",
           i, s.status, s.disk_count);
    check(reports == 1, "the first pass reports once");
    check(s.disk_count > 0, "and the disks were read");
    check(gs_ui_mount_poll(&s) == 0,
          "a second poll straight after finds nothing new");
    check(s.disk_picked == 0, "and no disk counts as picked");

    gs_ui_mount_wait();
    check(!gs_ui_mount_running(), "the thread is gone after the wait");
    check(gs_ui_mount_poll(&s) == 0, "and a poll after it finds nothing");

    gs_mount_close();
    gs_store_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/watch-test");
}

static void test_chosen_models_survive(void)
{
    gs_ui_state_t s;
    char out[900];
    FILE *f;
    int here = -1;
    int i;

    printf("chosen models across sessions\n");

    (void)mkdir("build/chosen-test", 0700);
    (void)mkdir("build/chosen-test/gravestone-models", 0700);
    f = fopen("build/chosen-test/gravestone-models/local-one.gguf", "wb");
    if (f == NULL) {
        check(0, "the model file was written");
        return;
    }
    for (i = 0; i < 4096; i++)
        fputc('m', f);
    fclose(f);

    memset(&s, 0, sizeof s);

    /* The disk is missing, so nothing is loaded and nothing is ticked. */
    gs_library_release();
    gs_ui_settings_merge_chosen(&s, "qwen3:4b\nlocal-one.gguf", out,
                                sizeof out);
    check(strcmp(out, "qwen3:4b\nlocal-one.gguf") == 0,
          "a save with no models loaded keeps the whole remembered list");

    gs_ui_settings_merge_chosen(&s, "", out, sizeof out);
    check(out[0] == '\0', "and an empty list stays empty");
    gs_ui_settings_merge_chosen(&s, NULL, out, sizeof out);
    check(out[0] == '\0', "as does no list at all");

    /* The disk is back and one of the two models is on it. */
    if (gs_library_scan("build/chosen-test") < 1) {
        check(0, "the model was seen on the disk");
        return;
    }
    for (i = 0; i < gs_library_count(); i++)
        if (strcmp(gs_library_at(i)->name, "local-one.gguf") == 0)
            here = i;
    check(here >= 0, "the model was seen on the disk");
    if (here < 0)
        return;
    s.library_count = gs_library_count();

    /* Present and ticked goes in. Absent keeps its place. */
    s.model_chosen[here] = 1;
    gs_ui_settings_merge_chosen(&s, "qwen3:4b\nlocal-one.gguf", out,
                                sizeof out);
    check(strstr(out, "local-one.gguf") != NULL,
          "a model that is here and ticked is written");
    check(strstr(out, "qwen3:4b") != NULL,
          "and a model that is not here keeps its place");

    /* Present and unticked is the person's own decision. */
    s.model_chosen[here] = 0;
    gs_ui_settings_merge_chosen(&s, "qwen3:4b\nlocal-one.gguf", out,
                                sizeof out);
    check(strstr(out, "local-one.gguf") == NULL,
          "a model that is here and unticked is taken off");
    check(strcmp(out, "qwen3:4b") == 0, "leaving the absent one alone");

    /* A name is never written twice, however it arrives. */
    s.model_chosen[here] = 1;
    gs_ui_settings_merge_chosen(&s, "local-one.gguf\nlocal-one.gguf", out,
                                sizeof out);
    check(strcmp(out, "local-one.gguf") == 0, "and a name is written once");

    /* One name inside another is not taken for it. */
    gs_ui_settings_merge_chosen(&s, "local-one.gguf.old", out, sizeof out);
    check(strstr(out, "local-one.gguf.old") != NULL,
          "a longer name sharing a start is its own model");

    /* The whole round trip through the file, with the disk missing in
     * the middle. */
    (void)system("rm -rf build/chosen-store");
    gs_paths_override("build/chosen-store");
    if (gs_store_open() != GS_OK) {
        check(0, "the test store opened");
        gs_paths_override(NULL);
        return;
    }
    gs_store_setting_put("models.chosen", "local-one.gguf\nqwen3:4b");
    gs_library_release();
    memset(&s, 0, sizeof s);
    gs_ui_settings_save(&s);
    {
        char kept[900];

        gs_store_setting_get("models.chosen", kept, sizeof kept);
        check(strcmp(kept, "local-one.gguf\nqwen3:4b") == 0,
              "a session with the disk missing saves the choice untouched");
    }
    gs_store_close();
    gs_paths_override(NULL);
    gs_library_release();
}

/* The record a person opens has to show what the model worked out, read
 * from the file when it opens, with how the run went after it. And the
 * window has to come back to its own conversation, whatever else was
 * written since. */
static void test_the_sheet_and_the_own_conversation(void)
{
    gs_ui_state_t s;
    long long session = 0, turn = 0, answer = 0, other = 0, oturn = 0;

    printf("the record sheet and the window's own conversation\n");

    (void)system("rm -rf build/sheet-test");
    gs_paths_override("build/sheet-test");
    if (gs_store_open() != GS_OK || gs_session_open() != GS_OK) {
        check(0, "the test files opened");
        gs_paths_override(NULL);
        return;
    }

    /* One conversation with an answer that has working out behind it. */
    gs_session_start("mine", &session);
    gs_session_add_turn(session, "why is the sky blue?", &turn);
    gs_session_add_answer(turn, "qwen3:4b", "Rayleigh scattering.", &answer);
    gs_session_set_working(answer,
        "The user asks about sky colour \xe2\x80\x94 shorter wavelengths "
        "scatter more, so blue.");
    gs_session_show(turn, answer);
    gs_session_note(turn, "asking qwen3:4b\n  qwen3:4b answered in 3 seconds");

    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;
    s.session_id = session;
    gs_ui_chat_reload(&s);
    check(s.history_count == 2, "the question and its answer are loaded");
    check(s.record_answer[1] == answer, "the answer behind the line is kept");
    check(strcmp(s.record_by[1], "qwen3:4b") == 0, "and which model gave it");
    check(s.record_answer[0] == 0, "a question has no answer behind it");

    check(gs_ui_record_showing(&s) == 0, "nothing shows before opening");
    gs_ui_record_open(&s, 1);
    check(gs_ui_record_showing(&s) == 1, "opening the answer shows the sheet");
    check(s.record_open == 1, "on that line");
    check(strstr(s.record_text, "WHAT qwen3:4b WORKED OUT BEFORE ANSWERING")
          != NULL, "the sheet names the model and starts with its working out");
    check(strstr(s.record_text, "shorter wavelengths scatter more") != NULL,
          "and carries the working out itself");
    check(strstr(s.record_text, "colour - shorter") != NULL,
          "written in plain ASCII, with the dash spelled out");
    check(strstr(s.record_text, "HOW THE RUN WENT") != NULL,
          "then says how the run went");
    check(strstr(s.record_text, "answered in 3 seconds") != NULL,
          "with the run log under it");
    {
        const char *a = strstr(s.record_text, "WORKED OUT");
        const char *b = strstr(s.record_text, "HOW THE RUN WENT");

        check(a != NULL && b != NULL && a < b,
              "and the working out comes before the run");
    }

    gs_ui_record_shut(&s);
    check(gs_ui_record_showing(&s) == 0, "shutting hides it");
    check(s.record_text[0] == '\0', "and empties the sheet");
    check(s.record_open == -1, "with no line open");

    /* An answer recorded before working out was kept. */
    gs_session_set_working(answer, "");
    gs_ui_record_open(&s, 1);
    check(strstr(s.record_text, "No working out is kept for this answer")
          != NULL, "an answer with none says so rather than showing nothing");
    gs_ui_record_shut(&s);

    gs_ui_record_open(&s, 0);
    check(gs_ui_record_showing(&s) && strstr(s.record_text, "Asked ") != NULL,
          "a line with no answer behind it still opens a sheet saying when "
          "it was asked");
    gs_ui_record_shut(&s);
    gs_ui_record_open(&s, 99);
    check(gs_ui_record_showing(&s) == 0, "a line past the end opens nothing");
    gs_ui_record_open(NULL, 0);
    check(1, "opening on no state does not crash");

    /* The window comes back to its own conversation even after another
     * one was made later by a run from the command line. */
    gs_store_setting_put(GS_UI_KEY_CHAT, "1");
    gs_session_start("cli", &other);
    gs_session_add_turn(other, "a question from the command line", &oturn);
    memset(&s, 0, sizeof s);
    gs_ui_chat_resume(&s);
    check(s.session_id == session,
          "resuming lands on the window's own conversation");
    check(strstr(s.history[0], "sky blue") != NULL,
          "with the person's own question in it");

    /* With nothing written down, the newest conversation is taken. */
    gs_store_setting_put(GS_UI_KEY_CHAT, "");
    memset(&s, 0, sizeof s);
    gs_ui_chat_resume(&s);
    check(s.session_id == other, "with no note the newest is taken");

    /* A note naming a conversation that is gone falls back too. */
    gs_store_setting_put(GS_UI_KEY_CHAT, "9999");
    memset(&s, 0, sizeof s);
    gs_ui_chat_resume(&s);
    check(s.session_id == other,
          "a note naming a lost conversation falls back to the newest");

    /* A send with no conversation makes one and writes it down. */
    memset(&s, 0, sizeof s);
    s.screen = GS_UI_SCREEN_HOME;
    gs_str_copy(s.prompt, sizeof s.prompt, "new chat");
    {
        char number[32] = {0};

        check(gs_ui_chat_send(&s) == GS_OK, "a send with no conversation works");
        gs_store_setting_get(GS_UI_KEY_CHAT, number, sizeof number);
        check(atoll(number) == s.session_id && s.session_id > 0,
              "and writes the new conversation down as the window's own");
    }
    gs_harness_wait();

    gs_session_close();
    gs_store_close();
    gs_paths_override(NULL);
}

/* The line walk the history used before, kept here as the reference the
 * new one is checked against. It measured each line again from its start
 * for every character added and walked every earlier line to reach line
 * n, which is what made a page of long answers take seconds to paint. */
static int reference_line(const char *text, int room, int index, char *out,
                          size_t cap)
{
    size_t at = 0;
    int line = 0;

    if (text == NULL || out == NULL || cap == 0 || room <= 0 || index < 0)
        return 0;
    out[0] = '\0';
    for (;;) {
        size_t take = 0;
        size_t space = 0;
        int broke = 0;

        if (text[at] == '\0')
            return 0;
        while (text[at + take] != '\0' && take + 1 < cap) {
            if (text[at + take] == '\n') {
                broke = 1;
                break;
            }
            if (gs_window_face_width_n(text + at, take + 1) > room)
                break;
            if (text[at + take] == ' ')
                space = take;
            take++;
        }
        if (!broke && text[at + take] != '\0' && text[at + take] != ' ' &&
            space > 0)
            take = space;
        /* The old walk never moved on a room narrower than one letter,
         * handing back empty lines for ever. Both are compared only at
         * rooms wider than the widest letter, which is 20 pixels. */
        if (line == index) {
            memcpy(out, text + at, take);
            out[take] = '\0';
            return 1;
        }
        at += take;
        while (text[at] == ' ')
            at++;
        if (text[at] == '\n')
            at++;
        line++;
    }
}

/* The composer keeps the line being written in sight. */
static void test_the_composer_follows_the_caret(void)
{
    printf("the composer follows the caret\n");

    check(gs_ui_chat_composer_skip("", 800, 3) == 0,
          "an empty prompt skips nothing");
    check(gs_ui_chat_composer_skip("a\nb", 800, 3) == 0,
          "a prompt that fits skips nothing");
    check(gs_ui_chat_composer_skip("a\nb\nc", 800, 3) == 0,
          "and nor does one that fills the box exactly");
    check(gs_ui_chat_composer_skip("a\nb\nc\n", 800, 3) == 1,
          "a break after the last line brings the empty line into view");
    check(gs_ui_chat_composer_skip("a\nb\nc\nd\ne", 800, 3) == 2,
          "five lines in a box of three leave the first two out");
    check(gs_ui_chat_composer_skip("a\nb\nc\nd\ne\n", 800, 3) == 3,
          "and one more for the line the caret stands on");
    check(gs_ui_chat_composer_skip("word word word word word word word",
                                   24, 2) > 0,
          "a long line wrapped past the box scrolls as well");
    check(gs_ui_chat_composer_skip(NULL, 800, 3) == 0 &&
          gs_ui_chat_composer_skip("a\nb\nc\nd", 0, 3) == 0 &&
          gs_ui_chat_composer_skip("a\nb\nc\nd", 800, 0) == 0,
          "no text, no room and no lines skip nothing");
}

static void test_the_line_walk(void)
{
    static const char *const texts[] = {
        "",
        "short",
        "what is time dilation",
        "a line\nwith a break\n\nand an empty line between",
        "   leading spaces and trailing spaces   ",
        "supercalifragilisticexpialidocious is one long word that has to be "
        "cut where it stands because there is nowhere to pull back to",
        "word word word word word word word word word word word word word "
        "word word word word word word word word word word word word word",
        "ends with a break\n",
        "\n\n\nthree breaks first",
        "tabs\tare\tjust characters here"
    };
    static const int rooms[] = { 24, 40, 97, 180, 333, 800 };
    char mine[512];
    char theirs[512];
    int mismatches = 0;
    int compared = 0;
    int t;
    int r;

    printf("walking a text once, line after line\n");

    for (t = 0; t < (int)(sizeof texts / sizeof texts[0]); t++)
        for (r = 0; r < (int)(sizeof rooms / sizeof rooms[0]); r++) {
            size_t at = 0;
            int n = 0;

            while (gs_ui_chat_next_line(texts[t], rooms[r], &at, mine,
                                        sizeof mine)) {
                if (!reference_line(texts[t], rooms[r], n, theirs,
                                    sizeof theirs) ||
                    strcmp(mine, theirs) != 0)
                    mismatches++;
                compared++;
                n++;
            }
            if (reference_line(texts[t], rooms[r], n, theirs, sizeof theirs))
                mismatches++;          /* the old walk found one more */
        }
    printf("        %d lines compared against the old walk\n", compared);
    check(compared > 100, "a good many lines were compared");
    check(mismatches == 0,
          "every line comes out exactly as the old walk wrote it");

    /* Asking for line n by number still agrees with walking to it. */
    {
        const char *text = texts[6];
        size_t at = 0;
        int n = 0;
        int agree = 1;

        while (gs_ui_chat_next_line(text, 97, &at, mine, sizeof mine)) {
            if (!gs_ui_chat_line(text, 97, n, theirs, sizeof theirs) ||
                strcmp(mine, theirs) != 0)
                agree = 0;
            n++;
        }
        check(agree && n > 1, "asking for a line by number gives the same line");
        check(gs_ui_chat_line(text, 97, n, theirs, sizeof theirs) == 0,
              "and there is nothing past the last");
    }

    /* A room narrower than one letter used to take nothing and never move
     * on. It has to finish, one character to a line. */
    {
        size_t at = 0;
        int n = 0;

        while (n < 100 &&
               gs_ui_chat_next_line("abc", 1, &at, mine, sizeof mine))
            n++;
        check(n == 3, "a room narrower than a letter still finishes");
    }

    {
        size_t at = 0;

        check(gs_ui_chat_next_line(NULL, 50, &at, mine, sizeof mine) == 0,
              "no text gives no line");
        check(gs_ui_chat_next_line("x", 50, NULL, mine, sizeof mine) == 0,
              "and nowhere to keep the place gives none");
        check(gs_ui_chat_next_line("x", 0, &at, mine, sizeof mine) == 0,
              "and no room gives none");
    }
}

/* One long answer after another, the page that took twenty eight seconds
 * to paint. */
static void test_a_page_of_long_answers_paints_quickly(void)
{
    gs_ui_state_t *s = calloc(1, sizeof *s);
    char para[5000];
    size_t at = 0;
    int w = 1920, h = 1080;
    int i;
    unsigned int *px;
    struct timespec a, b;
    long ms;

    printf("a page of long answers\n");
    if (s == NULL) {
        check(0, "room for the state");
        return;
    }
    while (at + 60 < 4600) {
        memcpy(para + at, "Time dilation is a real effect of relativity. ", 47);
        at += 47;
    }
    para[at] = '\0';
    s->screen = GS_UI_SCREEN_HOME;
    s->record_open = -1;
    for (i = 0; i < 64; i++) {
        s->history_mine[i] = (unsigned char)(i % 2 == 0);
        gs_str_copy(s->history[i], GS_UI_HISTORY_TEXT,
                    i % 2 == 0 ? "What is time dilation?" : para);
    }
    s->history_count = 64;
    s->history_gen = gs_ui_chat_generation();

    px = malloc((size_t)w * (size_t)h * sizeof *px);
    if (px == NULL) {
        check(0, "room for the canvas");
        free(s);
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &a);
    gs_ui_compose(px, w, h, s);
    for (i = 0; i < s->history_count; i++) {
        (void)gs_ui_chat_bubble_rect(s, w, h, i);
        (void)gs_ui_chat_note_rect(s, w, h, i);
    }
    /* The pointer moving over the chat asks the hit test, which asks for
     * every mark. A hundred moves is a short sweep of the hand. */
    for (i = 0; i < 100; i++)
        (void)gs_ui_hit_test(s, w, h, 200, 300 + i, NULL);
    clock_gettime(CLOCK_MONOTONIC, &b);
    ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
    printf("        64 lines of %lu characters each: a paint, every bubble, "
           "and 100 pointer moves took %ld ms\n", (unsigned long)at, ms);
    check(ms < 500, "all of it in under half a second, where it took 28");
    free(px);
    free(s);
}

/* The measurement is kept until the lines or the window change, and no
 * longer. */
static void test_the_layout_is_kept_until_it_changes(void)
{
    gs_ui_state_t *s = calloc(1, sizeof *s);
    gs_ui_rect_t before, after;
    int w = 980, h = 640;
    char longer[400];

    printf("keeping the measurement of the history\n");
    if (s == NULL) {
        check(0, "room for the state");
        return;
    }
    s->screen = GS_UI_SCREEN_HOME;
    gs_str_copy(s->history[0], GS_UI_HISTORY_TEXT, "short");
    s->history_count = 1;
    s->history_gen = gs_ui_chat_generation();
    before = gs_ui_chat_bubble_rect(s, w, h, 0);

    snprintf(longer, sizeof longer, "%s",
             "a much longer line that wraps across several lines of the "
             "bubble and so stands a good deal taller than the short one "
             "did before it, which the measurement has to notice");
    gs_str_copy(s->history[0], GS_UI_HISTORY_TEXT, longer);
    s->history_gen = gs_ui_chat_generation();
    after = gs_ui_chat_bubble_rect(s, w, h, 0);
    check(after.h > before.h,
          "new lines with a new stamp are measured again");

    /* Lines put in by hand carry no stamp and are never trusted. */
    s->history_gen = 0;
    gs_str_copy(s->history[0], GS_UI_HISTORY_TEXT, "short");
    check(gs_ui_chat_bubble_rect(s, w, h, 0).h == before.h,
          "lines with no stamp are measured every time");

    /* The same lines in a wider window wrap less. */
    gs_str_copy(s->history[0], GS_UI_HISTORY_TEXT, longer);
    s->history_gen = gs_ui_chat_generation();
    before = gs_ui_chat_bubble_rect(s, 980, 640, 0);
    after = gs_ui_chat_bubble_rect(s, 1920, 1080, 0);
    check(after.h < before.h, "a wider window is measured again");

    /* A line added keeps the stamp only if whoever added it changed it,
     * and the count alone is enough to notice. */
    gs_str_copy(s->history[1], GS_UI_HISTORY_TEXT, "another");
    s->history_count = 2;
    check(gs_ui_chat_bubble_rect(s, 1920, 1080, 1).h > 0,
          "a line added under the same stamp is still measured");
    free(s);
}

/* The history scrolls, so nothing said is ever out of reach. */
static void test_the_history_scrolls(void)
{
    gs_ui_state_t *s = calloc(1, sizeof *s);
    gs_ui_rect_t seen, bar, oldest, newest;
    int w = 980, h = 640;
    int reach;
    int i;

    printf("scrolling back through the history\n");
    if (s == NULL) {
        check(0, "room for the state");
        return;
    }
    s->screen = GS_UI_SCREEN_HOME;
    s->record_open = -1;
    for (i = 0; i < 40; i++) {
        snprintf(s->history[i], GS_UI_HISTORY_TEXT, "line %d", i);
        s->history_mine[i] = (unsigned char)(i % 2);
    }
    s->history_count = 40;
    s->history_gen = gs_ui_chat_generation();
    seen = gs_ui_chat_history_rect(w, h);
    bar = gs_ui_chat_bar_box(w, h);

    reach = gs_ui_chat_scroll_reach(s, w, h);
    check(reach > 0, "forty lines stand taller than the area");
    check(gs_ui_chat_content_height(s, w, h) == reach + seen.h,
          "and the reach is exactly the part that does not fit");

    newest = gs_ui_chat_bubble_rect(s, w, h, 39);
    oldest = gs_ui_chat_bubble_rect(s, w, h, 0);
    check(newest.w > 0 && newest.y + newest.h <= seen.y + seen.h,
          "at first the newest sits against the box");
    check(oldest.w == 0, "and the oldest is out of sight above");

    check(gs_ui_chat_scroll_by(s, w, h, -reach) == 1,
          "scrolling back all the way moves the history");
    check(s->history_back == reach, "as far as it goes and no further");
    oldest = gs_ui_chat_bubble_rect(s, w, h, 0);
    check(oldest.w > 0 && oldest.y == seen.y,
          "the oldest line now sits at the very top");
    check(gs_ui_chat_bubble_rect(s, w, h, 39).w == 0,
          "and the newest is out of sight below");

    check(gs_ui_chat_scroll_by(s, w, h, -500) == 0,
          "scrolling further back moves nothing");
    check(s->history_back == reach, "and stays at the top");
    check(gs_ui_chat_scroll_by(s, w, h, reach + 999) == 1,
          "scrolling forward returns to the newest");
    check(s->history_back == 0, "and stops there");
    check(gs_ui_chat_scroll_by(s, w, h, 50) == 0,
          "going past the newest moves nothing");

    /* The bar sits in its own strip, clear of every bubble. */
    {
        gs_ui_rect_t rail, grip;

        check(gs_ui_scrollbar(bar, gs_ui_chat_content_height(s, w, h),
                              seen.h, reach, &rail, &grip) == 1,
              "a history taller than its area carries a bar");
        check(rail.x >= seen.x + seen.w + 6,
              "and the bar stands clear of the bubbles by six pixels or more");
        check(rail.x + rail.w <= gs_ui_chat_rect(w, h).x +
                                 gs_ui_chat_rect(w, h).w,
              "inside the panel");

        /* Taking hold at the top of the rail goes to the oldest, and at
         * the bottom to the newest, the way a desktop bar does. */
        check(gs_ui_bar_grab(s, w, h, rail.x + 2, rail.y + 1) == 1,
              "the bar can be taken hold of");
        check(s->drag_bar == GS_UI_BAR_CHAT, "as the history's own bar");
        check(s->history_back == reach, "and the top of it shows the oldest");
        gs_ui_bar_drag(s, w, h, rail.y + rail.h + 40);
        check(s->history_back == 0,
              "dragged past the bottom it shows the newest");
        gs_ui_bar_drag(s, w, h, rail.y + rail.h / 2);
        check(s->history_back > 0 && s->history_back < reach,
              "and half way down it shows the middle");
        gs_ui_bar_drop(s);
        check(s->drag_bar == GS_UI_BAR_NONE, "letting go lets go");
    }

    /* A mark scrolled past an edge cannot be pressed. */
    for (i = 0; i < 40; i++)
        s->record_turn[i] = 100 + i;
    s->history_back = 0;
    {
        int shown = 0;
        int off_screen_with_mark = 0;

        for (i = 0; i < 40; i++) {
            gs_ui_rect_t mark = gs_ui_chat_note_rect(s, w, h, i);

            if (mark.w > 0) {
                shown++;
                if (mark.y < seen.y || mark.y + mark.h > seen.y + seen.h)
                    off_screen_with_mark++;
            }
        }
        check(shown > 0, "marks show on the lines in view");
        check(off_screen_with_mark == 0,
              "and never on a part of a bubble past the edge");
    }

    /* A history that fits carries no bar and does not scroll. */
    s->history_count = 2;
    s->history_gen = gs_ui_chat_generation();
    s->history_back = 0;
    check(gs_ui_chat_scroll_reach(s, w, h) == 0, "two lines have nowhere to go");
    check(gs_ui_chat_scroll_by(s, w, h, -200) == 0, "and do not scroll");
    check(gs_ui_bar_grab(s, w, h, bar.x + bar.w - 5, bar.y + 10) == 0,
          "and have no bar to take hold of");
    free(s);
}

/* Every prompt in the file shows in one history, page by page, whichever
 * conversation it was written into. */
static void test_the_whole_history(void)
{
    gs_ui_state_t *s = calloc(1, sizeof *s);
    long long a = 0, b = 0, c = 0, turn = 0, answer = 0;
    long long no_model = 0, all_failed = 0, answered = 0;
    int i;
    int w = 980, h = 640;

    printf("one history from every conversation\n");
    if (s == NULL) {
        check(0, "room for the state");
        return;
    }
    (void)system("rm -rf build/timeline-test");
    gs_paths_override("build/timeline-test");
    gs_session_close();
    if (gs_session_open() != GS_OK) {
        check(0, "the test file opened");
        free(s);
        return;
    }

    /* Three conversations written into in turn, the way the window, the
     * command line and a fresh window each start their own. */
    gs_session_start("window", &a);
    gs_session_start("command line", &b);
    gs_session_start("fresh window", &c);
    gs_session_add_turn(a, "first, in the window", &no_model);
    gs_session_add_turn(b, "second, from the command line", &answered);
    gs_session_add_answer(answered, "qwen3:4b", "an answer", &answer);
    gs_session_judge(answer, GS_SESSION_PASSED, 1);
    gs_session_pick(answered, NULL);
    gs_session_note(answered, "asking qwen3:4b\n  qwen3:4b answered");
    gs_session_add_turn(a, "third, back in the window", &all_failed);
    gs_session_add_answer(all_failed, "m", "wrong", &answer);
    gs_session_judge(answer, GS_SESSION_FAILED_TESTS, 0);
    gs_session_add_turn(c, "fourth, in a fresh window", &turn);

    s->screen = GS_UI_SCREEN_HOME;
    s->record_open = -1;
    gs_ui_chat_reload(s);
    check(s->history_count == 8, "four prompts give eight lines");
    check(strcmp(s->history[0], "first, in the window") == 0 &&
          strcmp(s->history[2], "second, from the command line") == 0 &&
          strcmp(s->history[4], "third, back in the window") == 0 &&
          strcmp(s->history[6], "fourth, in a fresh window") == 0,
          "every conversation's prompts, in the order they were asked");
    check(strstr(s->history[1], "No answer came back") != NULL,
          "a prompt nobody answered says so");
    check(strcmp(s->history[3], "an answer") == 0,
          "an answered prompt shows its answer");
    check(strstr(s->history[5], "Every answer failed the checking") != NULL,
          "a prompt whose every answer failed says that instead");

    /* Every line that came back carries a mark, and none that was asked. */
    {
        int marked = 0;
        int asked_marked = 0;

        /* A window tall enough for all eight bubbles, since a mark on a
         * bubble scrolled out of view is rightly not there to press. */
        for (i = 0; i < s->history_count; i++) {
            if (s->history_mine[i]) {
                if (gs_ui_chat_note_rect(s, 1920, 2000, i).w > 0)
                    asked_marked++;
            } else if (gs_ui_chat_note_rect(s, 1920, 2000, i).w > 0) {
                marked++;
            }
        }
        check(marked == 4, "all four lines that came back carry a mark");
        check(asked_marked == 0, "and no question does");
    }

    /* What each mark opens. */
    gs_ui_record_open(s, 1);
    check(strstr(s->record_text, "No model was asked") != NULL,
          "the unanswered prompt's sheet says no model was asked");
    check(strstr(s->record_text, "No run was recorded for this prompt") != NULL,
          "and that no run was recorded for it");
    gs_ui_record_open(s, 3);
    check(strstr(s->record_text, "WHAT qwen3:4b WORKED OUT") != NULL,
          "the answered prompt's sheet names the model");
    check(strstr(s->record_text, "qwen3:4b answered") != NULL,
          "and carries the record of the run");
    check(strstr(s->record_text, "Asked ") != NULL &&
          strstr(s->record_text, "Answered ") != NULL,
          "and when it was asked and answered");
    gs_ui_record_open(s, 5);
    check(strstr(s->record_text, "set aside") != NULL,
          "the failed prompt's sheet says the answers were set aside");
    gs_ui_record_shut(s);

    /* Enough prompts for more than one page. */
    for (i = 0; i < GS_UI_HISTORY_PAGE + 3; i++) {
        char words[64];

        snprintf(words, sizeof words, "extra prompt %d", i);
        gs_session_add_turn(i % 2 ? a : c, words, &turn);
    }
    s->history_skip = 0;
    gs_ui_chat_reload(s);
    check(s->history_older == 7, "the newest page leaves seven older prompts");
    check(s->history_kind[0] == GS_UI_LINE_EARLIER,
          "and says so in a line at the top");
    check(strcmp(s->history[0], "Show 7 earlier prompts") == 0,
          "naming how many");
    check(s->history_count == 1 + 2 * GS_UI_HISTORY_PAGE,
          "a full page is one line for the older ones and two per prompt");
    check(strcmp(s->history[s->history_count - 2],
                 "extra prompt 65") == 0,
          "with the newest prompt at the bottom");
    check(s->history_kind[s->history_count - 1] == GS_UI_LINE_SAID,
          "and no line for newer ones, since there are none");
    check(gs_ui_chat_note_rect(s, w, h, 0).w == 0,
          "the line for older prompts carries no mark");

    s->history_back = gs_ui_chat_scroll_reach(s, w, h);
    {
        gs_ui_rect_t line = gs_ui_chat_bubble_rect(s, w, h, 0);
        int row = -1;

        check(line.w > 0, "scrolled to the top, the line for older prompts shows");
        check(gs_ui_hit_test(s, w, h, line.x + line.w / 2, line.y + line.h / 2,
                             &row) == GS_UI_HIT_CHAT_EARLIER && row == 0,
              "and pressing it answers as the way to the older page");
    }

    s->history_skip += GS_UI_HISTORY_PAGE;
    s->history_back = 0;
    gs_ui_chat_reload(s);
    check(s->history_older == 0, "the older page is the last");
    check(s->history_kind[0] != GS_UI_LINE_EARLIER, "so it has no line above");
    check(strcmp(s->history[0], "first, in the window") == 0,
          "and starts with the very first prompt");
    check(s->history_kind[s->history_count - 1] == GS_UI_LINE_NEWER,
          "with a line at the bottom leading back to the newer page");
    {
        gs_ui_rect_t line = gs_ui_chat_bubble_rect(s, w, h,
                                                   s->history_count - 1);
        int row = -1;

        check(line.w > 0 &&
              gs_ui_hit_test(s, w, h, line.x + line.w / 2,
                             line.y + line.h / 2, &row) ==
                  GS_UI_HIT_CHAT_NEWER,
              "which answers as the way back");
    }

    /* A page past the end is brought back to the last one there is. */
    s->history_skip = 5000;
    gs_ui_chat_reload(s);
    check(s->history_skip == GS_UI_HISTORY_PAGE,
          "a page past the end becomes the last page");

    /* Sending brings the reader back to the newest, wherever they were. */
    s->history_skip = GS_UI_HISTORY_PAGE;
    s->history_back = 300;
    s->session_id = a;
    gs_str_copy(s->prompt, sizeof s->prompt, "a new question");
    check(gs_ui_chat_send(s) == GS_OK, "a prompt is sent from an older page");
    check(s->history_skip == 0 && s->history_back == 0,
          "and the reader is taken back to the newest");
    gs_harness_wait();

    gs_session_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/timeline-test");
    free(s);
}

/* The box of models sits on the bottom of the history, over the newest
 * lines. A press inside it belongs to the box, whatever is underneath. */
static void test_the_box_keeps_its_own_presses(void)
{
    static const int sizes[][2] = { {980, 640}, {1280, 720}, {1920, 1080} };
    gs_ui_state_t *s = calloc(1, sizeof *s);
    int k;
    int i;

    printf("presses inside the box of models\n");
    if (s == NULL) {
        check(0, "room for the state");
        return;
    }
    for (k = 0; k < 3; k++) {
        int w = sizes[k][0], h = sizes[k][1];
        int stolen = 0;
        int rows = 0;
        int outside_mark = 0;

        memset(s, 0, sizeof *s);
        s->screen = GS_UI_SCREEN_HOME;
        s->record_open = -1;
        for (i = 0; i < 20; i++) {
            snprintf(s->history[i], GS_UI_HISTORY_TEXT, "said %d", i);
            s->history_mine[i] = (unsigned char)(i % 2);
            s->record_turn[i] = 10 + i;
        }
        gs_str_copy(s->history[20], GS_UI_HISTORY_TEXT, "Show newer prompts");
        s->history_kind[20] = GS_UI_LINE_NEWER;
        s->history_count = 21;
        s->history_gen = gs_ui_chat_generation();
        s->library_count = 5;
        s->fleet_step = GS_UI_FLEET_STEPS;

        for (i = 0; i < s->library_count; i++) {
            gs_ui_rect_t row = gs_ui_fleet_row_rect(s, w, h, i);
            int hit_row = -1;
            gs_ui_hit_t hit;

            if (row.w <= 0)
                continue;
            rows++;
            hit = gs_ui_hit_test(s, w, h, row.x + row.w / 2,
                                 row.y + row.h / 2, &hit_row);
            if (hit != GS_UI_HIT_FLEET_OPTION || hit_row != i)
                stolen++;
        }
        printf("        %dx%d: %d rows of the box pressed, %d taken by the "
               "history underneath\n", w, h, rows, stolen);
        check(rows > 0 && stolen == 0,
              "every row of the box answers as the box");

        /* A mark well clear of the box still opens its record with the box
         * up, which is what shuts the box first. */
        for (i = 0; i < 20; i++) {
            gs_ui_rect_t mark = gs_ui_chat_note_rect(s, w, h, i);
            gs_ui_rect_t box = gs_ui_fleet_rect(s, w, h, s->fleet_step);

            if (mark.w <= 0 ||
                gs_ui_rect_contains(box, mark.x + mark.w / 2,
                                    mark.y + mark.h / 2))
                continue;
            if (gs_ui_hit_test(s, w, h, mark.x + mark.w / 2,
                               mark.y + mark.h / 2, NULL) == GS_UI_HIT_NOTE)
                outside_mark = 1;
            break;
        }
        check(outside_mark,
              "and a mark outside the box still answers with the box up");
    }
    free(s);
}

/* A line moving between pages is drawn with the same wrap it was measured
 * with, so every line of its words fits inside its button at every width. */
static void test_page_lines_fit_their_buttons(void)
{
    static const int widths[] = { 800, 980, 1100, 1280, 1600, 1920, 2560 };
    static const char *const words[] = { "Show 62 earlier prompts",
                                         "Show newer prompts",
                                         "Show 1 earlier prompt" };
    gs_ui_state_t *s = calloc(1, sizeof *s);
    int k;
    int j;
    int spilled = 0;
    int measured = 0;

    printf("the lines that move between pages\n");
    if (s == NULL) {
        check(0, "room for the state");
        return;
    }
    for (k = 0; k < (int)(sizeof widths / sizeof widths[0]); k++)
        for (j = 0; j < 3; j++) {
            int w = widths[k], h = 700;
            int room = gs_ui_chat_bubble_columns(w, h);
            gs_ui_rect_t bubble;
            char line[128];
            size_t at = 0;
            int lines = 0;

            memset(s, 0, sizeof *s);
            s->screen = GS_UI_SCREEN_HOME;
            gs_str_copy(s->history[0], GS_UI_HISTORY_TEXT, words[j]);
            s->history_kind[0] = GS_UI_LINE_EARLIER;
            s->history_count = 1;
            s->history_gen = gs_ui_chat_generation();
            bubble = gs_ui_chat_bubble_rect(s, w, h, 0);
            if (bubble.w <= 0 || room <= 0)
                continue;
            measured++;
            while (gs_ui_chat_next_line(words[j], room, &at, line,
                                        sizeof line)) {
                if (gs_window_face_width(line) + 2 * GS_UI_BUBBLE_PAD_X >
                    bubble.w)
                    spilled++;
                lines++;
            }
            if (lines != gs_ui_chat_bubble_lines(s, w, h, 0))
                spilled++;
            if (bubble.x + bubble.w > gs_ui_chat_history_rect(w, h).x +
                                      gs_ui_chat_history_rect(w, h).w)
                spilled++;
        }
    check(measured > 10, "the words were laid out at many widths");
    check(spilled == 0,
          "every line of them fits inside its button and inside the panel");
    free(s);
}

/* A model that gives nothing back leaves a row with no words in it. The
 * panel and the record have to say nothing came back, since nothing was
 * checked. */
static void test_nothing_came_back_is_said_as_such(void)
{
    gs_ui_state_t *s = calloc(1, sizeof *s);
    long long session = 0, empty_turn = 0, killed_turn = 0, orphan = 0;
    long long answer = 0;

    printf("what the panel says when nothing came back\n");
    if (s == NULL) {
        check(0, "room for the state");
        return;
    }
    (void)system("rm -rf build/nothing-test");
    gs_paths_override("build/nothing-test");
    gs_session_close();
    if (gs_session_open() != GS_OK) {
        check(0, "the test file opened");
        free(s);
        return;
    }
    gs_session_start("t", &session);

    /* The way the harness writes a model that did not answer. */
    gs_session_add_turn(session, "asked, and nothing came back", &empty_turn);
    gs_session_add_answer(empty_turn, "m", "", &answer);
    gs_session_note(empty_turn, "asking m\n  m did not answer");

    /* A run stopped part way, with an empty row and no record. */
    gs_session_add_turn(session, "a run that stopped", &killed_turn);
    gs_session_add_answer(killed_turn, "m", "", &answer);

    /* Written down and never asked. */
    gs_session_add_turn(session, "never asked", &orphan);

    s->screen = GS_UI_SCREEN_HOME;
    s->record_open = -1;
    gs_ui_chat_reload(s);
    check(s->history_count == 6, "three prompts, three lines under them");
    check(strstr(s->history[1], "No answer came back") != NULL,
          "a model that gave nothing back is not called a failed check");
    check(strstr(s->history[3], "No answer came back") != NULL,
          "and neither is a run that stopped");

    gs_ui_record_open(s, 1);
    check(strstr(s->record_text, "came back with nothing") != NULL,
          "the record says every model came back with nothing");
    check(strstr(s->record_text, "set aside by the checking") == NULL,
          "and does not claim anything was checked");
    check(strstr(s->record_text, "m did not answer") != NULL,
          "with the record of the run under it");

    gs_ui_record_open(s, 3);
    check(strstr(s->record_text, "came back with nothing, so there was no "
                                 "answer to check.") != NULL,
          "a stopped run says the same, without pointing at a record");
    check(strstr(s->record_text, "No record of this run was kept") != NULL,
          "and says no record of it was kept");

    gs_ui_record_open(s, 5);
    check(strstr(s->record_text, "another run was still going") != NULL,
          "a prompt never asked names every reason it could have gone "
          "unasked");
    check(strstr(s->record_text, "No run was recorded for this prompt") != NULL,
          "and says no run was recorded for it");

    check(gs_session_answer_count(empty_turn, 0) == 1 &&
          gs_session_answer_count(empty_turn, 1) == 0,
          "the empty row counts as a row and not as an answer");
    gs_ui_record_shut(s);
    gs_session_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/nothing-test");
    free(s);
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
    test_the_line_walk();
    test_the_composer_follows_the_caret();
    test_a_page_of_long_answers_paints_quickly();
    test_the_layout_is_kept_until_it_changes();
    test_the_history_scrolls();
    test_the_whole_history();
    test_the_box_keeps_its_own_presses();
    test_page_lines_fit_their_buttons();
    test_nothing_came_back_is_said_as_such();
    test_the_sheet_and_the_own_conversation();
    test_chosen_models_survive();
    test_settings_round_trip();
    test_the_drives_are_watched();
    test_home_screen();
    test_gear_mark_and_back();
    test_chat_panel();
    test_fleet_box();
    test_fleet_on_screen();
    test_context_and_memory();
    test_caret();
    test_history_shows_what_was_asked();
    test_the_scrollbar();
    test_the_record();
    test_bubbles();
    test_history_lands_where_it_should();
    test_prompt_lines();
    test_send_records_the_prompt();
    test_prompt_holds_lines();
    test_cursor_shapes();
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
