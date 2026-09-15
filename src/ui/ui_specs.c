/* ui_specs.c
 *
 * The labels drawn over the canvas, and the smaller window that lists what
 * was stored about the machine.
 *
 * Glyphs live on the X server rather than in this program, so text is sent
 * as a separate request after the image lands.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "detect.h"
#include "log.h"
#include "store.h"
#include "str.h"
#include "window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define SPECS_PAD       22
#define SPECS_ROW       21
#define SPECS_COL       148

#define SPECS_BACK      0x0016161Au
#define SPECS_HEAD      0x00D6DAE2u
#define SPECS_KEY       0x008A8F9Au
#define SPECS_VALUE     0x00C3C8D2u
#define SPECS_RULE      0x00303039u

/* Centres a label inside a control. */
static void label_in(gs_window_t *w, gs_ui_rect_t r, const char *text,
                     unsigned int colour)
{
    int tw = gs_window_text_width(w, text);
    int x = r.x + (r.w - tw) / 2;
    int y = r.y + (r.h + gs_window_font_ascent(w)) / 2 - 1;

    gs_window_text(w, x, y, text, colour);
}

void gs_ui_draw_labels(gs_window_t *window, const gs_ui_state_t *state)
{
    static const gs_ui_state_t blank;
    int width = gs_window_width(window);
    gs_ui_rect_t mount, specs;
    int status_w;

    if (window == NULL)
        return;
    if (state == NULL)
        state = &blank;
    if (width < GS_UI_MARGIN * 2 + 80)
        return;

    /* The password box covers whichever screen is underneath, and words
     * are drawn through the glass after the image, so anything else
     * written now would sit on top of the box rather than behind it. Its
     * words are the only words while it is up. */
    if (gs_ui_secret_visible(state)) {
        gs_ui_secret_labels(window, state);
        return;
    }

    if (state->screen == GS_UI_SCREEN_HOME) {
        gs_ui_home_labels(window, state);
        return;
    }

    /* With the confirm box up, its words are the only words. Text drawn
     * after the image would sit on top of the box, so everything else
     * stays unwritten until the box is answered. */
    if (gs_ui_confirm_visible(state)) {
        gs_ui_confirm_labels(window, state);
        return;
    }

    mount = gs_ui_mount_rect(width);
    specs = gs_ui_specs_rect(width);

    if (mount.x + mount.w < width - GS_UI_MARGIN)
        label_in(window, mount, GS_UI_MOUNT_LABEL, GS_UI_BTN_LABEL);
    if (specs.x + specs.w < width - GS_UI_MARGIN)
        label_in(window, specs, GS_UI_SPECS_LABEL,
                 state->mounted ? GS_UI_BTN_LABEL : GS_UI_BTN_OFF_LABEL);

    /* The status sits against the right edge, clear of the lamp. */
    status_w = gs_window_text_width(window, state->status);
    if (status_w > 0) {
        int x = width - GS_UI_MARGIN - 22 - status_w;
        int y = GS_UI_BAR_HEIGHT / 2 - 2;
        if (x > specs.x + specs.w + 16)
            gs_window_text(window, x, y, state->status, SPECS_HEAD);
    }

    if (state->detail[0] != '\0') {
        int dw = gs_window_text_width(window, state->detail);
        int x = width - GS_UI_MARGIN - 22 - dw;
        int y = GS_UI_BAR_HEIGHT / 2 + 14;
        if (x > specs.x + specs.w + 16)
            gs_window_text(window, x, y, state->detail, GS_UI_STATUS_TEXT);
    }

    gs_ui_panes_labels(window, state);
    gs_ui_confirm_labels(window, state);
}

int gs_ui_specs_gather(gs_ui_row_t *rows, int cap)
{
    gs_detect_report_t report;
    char print[17] = {0};
    char id[17] = {0};
    long long mounted_at = 0, last_seen = 0;
    int n = 0;

    if (gs_store_latest(&report, print, sizeof print, &mounted_at,
                        &last_seen) != GS_OK)
        return 0;
    gs_store_install_id(id, sizeof id);

#define ROW(k, ...)                                                    \
    do {                                                               \
        if (n < cap) {                                                 \
            gs_str_copy(rows[n].key, sizeof rows[n].key, (k));         \
            snprintf(rows[n].value, sizeof rows[n].value, __VA_ARGS__); \
            n++;                                                       \
        }                                                              \
    } while (0)

    {
        char ram[32], total[32], freed[32], vram[32];
        int i;

        gs_str_bytes(report.ram_total_bytes, ram, sizeof ram);
        gs_str_bytes(report.disk_total_bytes, total, sizeof total);
        gs_str_bytes(report.disk_free_bytes, freed, sizeof freed);
        gs_str_bytes(report.gpu_memory_bytes, vram, sizeof vram);

        (void)id;
        (void)print;
        ROW("operating system", "%s", report.os);
        ROW("kernel", "%s", report.kernel);
        ROW("architecture", "%s", report.arch);
        ROW("processor", "%s", report.cpu_model);
        ROW("cores", "%d", report.cpu_cores);
        ROW("memory", "%s", ram);
        if (report.gpu_memory_bytes > 0)
            ROW("graphics", "%s, %s", report.gpu, vram);
        else
            ROW("graphics", "%s", report.gpu);

        /* One line per drive, named by where it is attached. */
        for (i = 0; i < report.disk_count; i++) {
            char dtotal[32], dfree[32];
            char key[32];

            gs_str_bytes(report.disk[i].total_bytes, dtotal, sizeof dtotal);
            gs_str_bytes(report.disk[i].free_bytes, dfree, sizeof dfree);
            snprintf(key, sizeof key, "disk %s", report.disk[i].mount);
            ROW(key, "%s free of %s  (%s)", dfree, dtotal,
                report.disk[i].fs);
        }
        if (report.disk_count > 1)
            ROW("all disks", "%s free of %s", freed, total);

        ROW("configurations", "%d", gs_store_snapshot_count());
    }
#undef ROW
    return n;
}

static int target_size(int step, int *w, int *h)
{
    if (step < 0) step = 0;
    if (step > GS_UI_SPECS_STEPS) step = GS_UI_SPECS_STEPS;
    *w = GS_UI_SPECS_START_W +
         (GS_UI_SPECS_W - GS_UI_SPECS_START_W) * step / GS_UI_SPECS_STEPS;
    *h = GS_UI_SPECS_START_H +
         (GS_UI_SPECS_H - GS_UI_SPECS_START_H) * step / GS_UI_SPECS_STEPS;
    return step;
}

/* Kept between frames, since the animation repaints ten times in a row and
 * a fresh allocation each time is churn for nothing. */
static unsigned int *panel_canvas;
static size_t panel_canvas_len;

static void paint_rows(gs_window_t *w, const gs_ui_row_t *rows, int count,
                       int width, int height)
{
    unsigned int *px;
    size_t needed;
    int x, y, i;
    int ascent = gs_window_font_ascent(w);

    if (width <= 0 || height <= 0)
        return;

    needed = (size_t)width * (size_t)height;
    if (needed > panel_canvas_len) {
        unsigned int *grown = realloc(panel_canvas, needed * sizeof *grown);
        if (grown == NULL)
            return;
        panel_canvas = grown;
        panel_canvas_len = needed;
    }
    px = panel_canvas;

    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            px[(size_t)y * (size_t)width + (size_t)x] = SPECS_BACK;

    /* One line under the heading. */
    for (x = SPECS_PAD; x < width - SPECS_PAD && x < width; x++) {
        int ry = SPECS_PAD + SPECS_ROW + 4;
        if (ry >= 0 && ry < height)
            px[(size_t)ry * (size_t)width + (size_t)x] = SPECS_RULE;
    }

    /* A one pixel edge all the way round, so the panel reads as a panel
     * while it grows rather than bleeding into whatever is behind it. */
    for (x = 0; x < width; x++) {
        px[(size_t)0 * (size_t)width + (size_t)x] = SPECS_RULE;
        px[(size_t)(height - 1) * (size_t)width + (size_t)x] = SPECS_RULE;
    }
    for (y = 0; y < height; y++) {
        px[(size_t)y * (size_t)width + 0] = SPECS_RULE;
        px[(size_t)y * (size_t)width + (size_t)(width - 1)] = SPECS_RULE;
    }

    gs_window_blit(w, px, width, height);

    gs_window_text(w, SPECS_PAD, SPECS_PAD + ascent, "STORED DEVICE",
                   SPECS_HEAD);

    for (i = 0; i < count; i++) {
        int ry = SPECS_PAD + SPECS_ROW * 2 + 6 + i * SPECS_ROW + ascent;
        if (ry > height - 6)
            break;
        gs_window_text(w, SPECS_PAD, ry, rows[i].key, SPECS_KEY);
        gs_window_text(w, SPECS_PAD + SPECS_COL, ry, rows[i].value,
                       SPECS_VALUE);
    }

    gs_window_present(w);
}

int gs_ui_specs_open(gs_ui_specs_t *panel, gs_window_t *centre_on)
{
    int host_x = 0, host_y = 0;

    if (panel == NULL)
        return GS_ERR_ARG;

    memset(panel, 0, sizeof *panel);
    panel->count = gs_ui_specs_gather(panel->rows, GS_UI_SPECS_MAX_ROWS);
    if (panel->count == 0) {
        gs_log_warn("specs: nothing is stored for this install");
        return GS_ERR;
    }

    /* The middle of the window it belongs to, worked out before the panel
     * exists so the very first frame lands in the right place. */
    panel->centred = 0;
    if (centre_on != NULL &&
        gs_window_position(centre_on, &host_x, &host_y) == GS_OK) {
        panel->centre_x = host_x + gs_window_width(centre_on) / 2;
        panel->centre_y = host_y + gs_window_height(centre_on) / 2;
        panel->centred = 1;
    }

    panel->window = gs_window_open(GS_UI_SPECS_TITLE, GS_UI_SPECS_START_W,
                                   GS_UI_SPECS_START_H);
    if (panel->window == NULL)
        return GS_ERR;

    /* The manager is told the size the panel is heading for, so it does
     * not choose one of its own on the way. */
    /* The manager is told the size the panel is heading for and the place
     * it wants, since it reads both when it puts the window up and not
     * afterwards. The smallest useful size is set low, so folding away is
     * not stopped part of the way down. */
    gs_window_set_hints(panel->window,
                        panel->centred
                            ? panel->centre_x - GS_UI_SPECS_START_W / 2 : -1,
                        panel->centred
                            ? panel->centre_y - GS_UI_SPECS_START_H / 2 : -1,
                        GS_UI_SPECS_W, GS_UI_SPECS_H,
                        GS_UI_SPECS_START_W, GS_UI_SPECS_START_H);

    if (panel->centred)
        gs_window_place(panel->window,
                        panel->centre_x - GS_UI_SPECS_START_W / 2,
                        panel->centre_y - GS_UI_SPECS_START_H / 2,
                        GS_UI_SPECS_START_W, GS_UI_SPECS_START_H);

    panel->step = 0;
    panel->direction = 1;
    panel->want_w = GS_UI_SPECS_START_W;
    panel->want_h = GS_UI_SPECS_START_H;
    return GS_OK;
}

void gs_ui_specs_begin_close(gs_ui_specs_t *panel)
{
    if (panel != NULL && panel->window != NULL)
        panel->direction = -1;
}

int gs_ui_specs_settled(const gs_ui_specs_t *panel)
{
    if (panel == NULL || panel->window == NULL)
        return 1;
    return gs_window_width(panel->window) == panel->want_w &&
           gs_window_height(panel->window) == panel->want_h;
}

int gs_ui_specs_animating(const gs_ui_specs_t *panel)
{
    return panel != NULL && panel->window != NULL && panel->direction != 0;
}

int gs_ui_specs_spent(const gs_ui_specs_t *panel)
{
    return panel != NULL && panel->window != NULL &&
           panel->direction == -1 && panel->step <= 0;
}

int gs_ui_specs_advance(gs_ui_specs_t *panel)
{
    int w, h;

    if (!gs_ui_specs_animating(panel))
        return 0;

    if (panel->direction > 0) {
        panel->step++;
        if (panel->step >= GS_UI_SPECS_STEPS) {
            panel->step = GS_UI_SPECS_STEPS;
            panel->direction = 0;
        }
    } else {
        panel->step -= GS_UI_SPECS_CLOSE_STRIDE;
        /* Stopping short of nothing leaves one fewer request to travel to
         * the window manager and back before the window can go. */
        if (panel->step <= GS_UI_SPECS_CLOSE_FLOOR)
            panel->step = 0;
    }

    target_size(panel->step, &w, &h);
    panel->want_w = w;
    panel->want_h = h;

    /* Moving and resizing together keeps the middle of the panel still
     * while its edges travel, so it grows out of one point rather than
     * out of its top left corner. */
    if (panel->centred)
        gs_window_place(panel->window, panel->centre_x - w / 2,
                        panel->centre_y - h / 2, w, h);
    else
        gs_window_resize(panel->window, w, h);
    return panel->direction != 0;
}

void gs_ui_specs_paint(gs_ui_specs_t *panel)
{
    int want_w, want_h, have_w, have_h;

    if (panel == NULL || panel->window == NULL)
        return;

    /* The window reports the size the server last confirmed, which lags
     * behind while it grows. Painting the larger of the two covers the
     * area already revealed, and the server clips anything past the
     * edge. */
    target_size(panel->step, &want_w, &want_h);
    have_w = gs_window_width(panel->window);
    have_h = gs_window_height(panel->window);

    paint_rows(panel->window, panel->rows, panel->count,
               want_w > have_w ? want_w : have_w,
               want_h > have_h ? want_h : have_h);
}

void gs_ui_specs_destroy(gs_ui_specs_t *panel)
{
    if (panel == NULL || panel->window == NULL)
        return;
    gs_window_close(panel->window);
    panel->window = NULL;
    panel->direction = 0;
    panel->step = 0;

    free(panel_canvas);
    panel_canvas = NULL;
    panel_canvas_len = 0;

    gs_log_info("specs: panel closed");
}
