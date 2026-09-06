/* ui.c
 *
 * Opens the window, keeps it painted, and turns clicks into actions.
 * Nothing here knows what an X server is, because every call goes through
 * src/lib/window/.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "catalogue.h"
#include "detect.h"
#include "library.h"
#include "log.h"
#include "store.h"
#include "str.h"
#include "window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Milliseconds since some fixed point, used only to pace the animation. */
static long long now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

#define UI_WIDTH   980
#define UI_HEIGHT  640
#define KEYCODE_ESCAPE 9

/* The composition buffer is kept between repaints and only grown when the
 * window does, since a resize drag would otherwise churn it. */
static unsigned int *canvas;
static size_t canvas_len;

int gs_ui_key_closes(int keycode)
{
    return keycode == KEYCODE_ESCAPE;
}

void gs_ui_release(void)
{
    free(canvas);
    canvas = NULL;
    canvas_len = 0;
}

int gs_ui_paint(gs_window_t *window, const gs_ui_state_t *state)
{
    int w = gs_window_width(window);
    int h = gs_window_height(window);
    size_t needed;
    int rc;

    if (window == NULL || w <= 0 || h <= 0)
        return GS_ERR_ARG;

    needed = (size_t)w * (size_t)h;
    if (needed > canvas_len) {
        unsigned int *grown = realloc(canvas, needed * sizeof *grown);
        if (grown == NULL) {
            gs_log_error("ui: no memory for a %dx%d canvas", w, h);
            return GS_ERR_MEM;
        }
        canvas = grown;
        canvas_len = needed;
    }

    gs_ui_compose(canvas, w, h, state);
    rc = gs_window_blit(window, canvas, w, h);
    if (rc == GS_OK) {
        gs_ui_draw_labels(window, state);
        rc = gs_window_present(window);
    }
    return rc;
}

/* Repaints only the strips of the canvas that a hover change touched.
 * The whole canvas is recomposed in memory, which costs milliseconds,
 * and only the changed rows cross the socket, which is where the time
 * went. The labels are drawn again in full because the strips wiped the
 * ones inside them, and glyph requests are a few bytes each. */
static int paint_hover(gs_window_t *window, const gs_ui_state_t *state,
                       gs_ui_rect_t was, gs_ui_rect_t now)
{
    int w = gs_window_width(window);
    int h = gs_window_height(window);
    int rc;

    if (window == NULL || w <= 0 || h <= 0)
        return GS_ERR_ARG;
    if ((size_t)w * (size_t)h > canvas_len || canvas == NULL)
        return gs_ui_paint(window, state);

    gs_ui_compose(canvas, w, h, state);
    rc = gs_window_blit_rect(window, canvas, w, h, was.y, was.h);
    if (rc == GS_OK)
        rc = gs_window_blit_rect(window, canvas, w, h, now.y, now.h);
    if (rc == GS_OK) {
        gs_ui_draw_labels(window, state);
        rc = gs_window_present(window);
    }
    return rc;
}

static int ui_init(void)
{
    return GS_OK;
}

static int ui_run(int argc, char **argv)
{
    gs_window_t *window;
    gs_window_t *windows[2];
    gs_ui_specs_t panel;
    gs_window_event_t event;
    gs_ui_state_t state;
    int running = 1;
    int rc = GS_OK;
    int need_paint = 0;
    int which = 0;
    long long next_frame = 0;
    long long frame_limit = 0;

    (void)argc;
    (void)argv;

    memset(&state, 0, sizeof state);
    memset(&panel, 0, sizeof panel);

    if (gs_store_open() != GS_OK) {
        gs_log_error("ui: the local database would not open");
        return GS_ERR;
    }
    /* A machine already mounted gets read again at startup, so free space
     * stays current and a card swapped since the last run is noticed
     * without the user pressing anything. An install that already existed
     * and yet holds no reading has had one taken away by an upgrade, so it
     * takes a fresh one rather than sitting unmounted. */
    {
        gs_detect_report_t fresh;
        int have_reading = gs_detect_read(&fresh) == GS_OK;
        int changed = 0;

        if (have_reading &&
            (gs_store_is_mounted() || !gs_store_install_is_new()) &&
            gs_store_mount(&fresh, &changed) == GS_OK && changed)
            gs_log_info("ui: the hardware differs from the last run");

        gs_ui_refresh_state(&state);
        gs_ui_panes_refresh(&state, have_reading ? &fresh : NULL);
    }

    window = gs_window_open(GS_UI_TITLE, UI_WIDTH, UI_HEIGHT);
    if (window == NULL) {
        gs_log_error("ui: the window could not be opened");
        gs_store_close();
        return GS_ERR;
    }

    while (running) {
        int count = 1;
        int timeout;
        int got;

        /* Both windows are served from this one loop, so the main window
         * keeps answering while the panel is open and while it animates. */
        windows[0] = window;
        if (panel.window != NULL)
            windows[count++] = panel.window;

        /* The animation runs on a clock rather than on quiet moments,
         * since a window being resized sends a steady stream of events and
         * a wait that keeps returning early would never advance a frame.
         * Events are still read every pass, so neither window stops
         * answering while the panel moves. */
        if (state.confirm_busy) {
            long long now = now_ms();

            if (now >= next_frame) {
                state.spin_phase++;
                state.busy_percent = gs_ui_add_progress();
                need_paint = 1;
                next_frame = now + 66;
                if (gs_ui_remove_poll(&state)) {
                    state.confirm_busy = 0;
                    state.confirm_dir = -1;
                    state.busy_percent = -1;
                }
            }
            timeout = (int)(next_frame - now_ms());
            if (timeout < 0)
                timeout = 0;
        } else if (gs_ui_confirm_animating(&state)) {
            long long now = now_ms();

            if (now >= next_frame) {
                gs_ui_confirm_advance(&state);
                need_paint = 1;
                next_frame = now + GS_UI_SPECS_FRAME_MS;
            }
            timeout = (int)(next_frame - now_ms());
            if (timeout < 0)
                timeout = 0;
        } else if (gs_ui_specs_animating(&panel)) {
            long long now = now_ms();

            /* A frame waits for the window to reach the size the last one
             * asked for, which keeps one request outstanding rather than a
             * queue the manager works through long after the animation has
             * finished. A manager that clamps a size would never confirm
             * it exactly, so the limit moves the fold along regardless. */
            if ((now >= next_frame && gs_ui_specs_settled(&panel)) ||
                now >= frame_limit) {
                gs_ui_specs_advance(&panel);
                /* A panel folding away is repainted by nobody. Every blit
                 * queues work in the compositor ahead of the destroy, and
                 * that queue is the delay left between the last frame and
                 * the window leaving the screen. */
                if (panel.direction >= 0)
                    gs_ui_specs_paint(&panel);
                next_frame = now + GS_UI_SPECS_FRAME_MS;
                frame_limit = now + GS_UI_SPECS_FRAME_MS * 4;
                gs_log_debug("ui: panel frame, step %d of %d, direction %d",
                             panel.step, GS_UI_SPECS_STEPS, panel.direction);
                if (gs_ui_specs_spent(&panel)) {
                    gs_ui_specs_destroy(&panel);
                    continue;
                }
                now = now_ms();
            }
            timeout = (int)(next_frame - now);
            if (timeout < 0)
                timeout = GS_UI_SPECS_FRAME_MS;
        } else if (need_paint) {
            timeout = 0;
        } else {
            timeout = -1;
        }

        got = gs_window_wait_any(windows, count, &which, &event, timeout);

        if (got < 0) {
            gs_log_error("ui: lost the connection to the display");
            rc = GS_ERR;
            break;
        }

        if (got == 0) {
            if (need_paint) {
                need_paint = 0;
                if (gs_ui_paint(window, &state) != GS_OK) {
                    gs_log_error("ui: painting failed");
                    rc = GS_ERR;
                    running = 0;
                }
            }
            continue;
        }

        /* Anything from the panel is handled apart from the main window. */
        if (which == 1) {
            switch (event.kind) {
            case GS_WINDOW_EVENT_EXPOSE:
            case GS_WINDOW_EVENT_RESIZE:
                if (panel.direction >= 0)
                    gs_ui_specs_paint(&panel);
                break;
            case GS_WINDOW_EVENT_KEY:
                if (gs_ui_key_closes(event.key)) {
                    gs_ui_specs_begin_close(&panel);
                    next_frame = now_ms();
                    frame_limit = next_frame + GS_UI_SPECS_FRAME_MS * 4;
                }
                break;
            case GS_WINDOW_EVENT_CLOSE:
                gs_ui_specs_begin_close(&panel);
                next_frame = now_ms();
                frame_limit = next_frame + GS_UI_SPECS_FRAME_MS * 4;
                break;
            default:
                break;
            }
            continue;
        }

        switch (event.kind) {
        case GS_WINDOW_EVENT_EXPOSE:
            /* The finished frame is still on the server, so an exposure
             * is answered by copying it across again. Only a window with
             * no frame yet has to be drawn from scratch. */
            if (gs_window_has_frame(window)) {
                if (gs_window_present(window) != GS_OK)
                    need_paint = 1;
            } else {
                need_paint = 1;
            }
            break;

        case GS_WINDOW_EVENT_RESIZE:
            need_paint = 1;
            break;

        case GS_WINDOW_EVENT_MOVE: {
            gs_ui_hit_t was = state.hover;
            int was_row = state.hover_row;

            state.hover = gs_ui_hit_test(&state, gs_window_width(window),
                                         gs_window_height(window),
                                         event.x, event.y, &state.hover_row);
            if (state.hover != was || state.hover_row != was_row) {
                int w = gs_window_width(window);
                int h = gs_window_height(window);

                if (paint_hover(window, &state,
                                gs_ui_hit_rect(&state, w, h, was, was_row),
                                gs_ui_hit_rect(&state, w, h, state.hover,
                                               state.hover_row)) != GS_OK)
                    need_paint = 1;
            }
            break;
        }

        case GS_WINDOW_EVENT_CLICK: {
            gs_ui_hit_t hit;
            int row = -1;

            /* A wheel notch arrives as a button, four for up and five for
             * down. It scrolls whichever list the pointer is over. */
            if (event.button == 4 || event.button == 5) {
                if (gs_ui_scroll_under(&state, window, event.x, event.y,
                                 event.button == 4 ? -3 : 3))
                    need_paint = 1;
                break;
            }
            if (event.button != 1)
                break;
            hit = gs_ui_hit_test(&state, gs_window_width(window),
                                 gs_window_height(window),
                                 event.x, event.y, &row);
            state.pressed = hit;
            if (hit == GS_UI_HIT_CHOOSER) {
                state.chooser_open = !state.chooser_open;
            } else if (hit == GS_UI_HIT_DROP_ROW) {
                gs_ui_panes_pick_category(&state, row);
                state.chooser_open = 0;
            } else if (hit == GS_UI_HIT_LEFT_ROW) {
                state.model_sel = row;
                state.chooser_open = 0;
            } else if (hit == GS_UI_HIT_MID_ROW) {
                gs_ui_panes_pick_disk(&state, row);
                state.chooser_open = 0;
            } else if (hit == GS_UI_HIT_RIGHT_ROW) {
                /* A model already on the disk points back at its row in
                 * the list, when the list has one to point at. */
                if (gs_ui_panes_locate(&state, gs_library_at(row)) != GS_OK)
                    gs_log_info("ui: that model has no row in the list");
            } else if (hit == GS_UI_HIT_RIGHT_REMOVE) {
                state.confirm_row = row;
                state.confirm_add = 0;
                state.confirm_step = 0;
                state.confirm_dir = 1;
                next_frame = now_ms();
            } else if (hit == GS_UI_HIT_LEFT_ADD) {
                state.confirm_row = row;
                state.confirm_add = 1;
                state.confirm_step = 0;
                state.confirm_dir = 1;
                next_frame = now_ms();
            } else if (hit == GS_UI_HIT_CONFIRM_OK) {
                gs_log_info("ui: %s a model",
                            state.confirm_add ? "pulling" : "removing");
                if ((state.confirm_add ? gs_ui_add_begin(&state)
                                       : gs_ui_remove_begin(&state))
                    == GS_OK) {
                    state.confirm_busy = 1;
                    state.spin_phase = 0;
                } else {
                    state.confirm_dir = -1;
                }
                next_frame = now_ms();
            } else if (hit == GS_UI_HIT_CONFIRM_NO) {
                state.confirm_dir = -1;
                next_frame = now_ms();
            } else if (state.chooser_open) {
                /* A click anywhere else puts the open list away. */
                state.chooser_open = 0;
            }
            if (hit == GS_UI_HIT_MOUNT) {
                gs_log_info("ui: mounting this device");
                gs_ui_do_mount(&state);
            } else if (hit == GS_UI_HIT_SPECS && panel.window == NULL) {
                gs_log_info("ui: opening the specs panel");
                if (gs_ui_specs_open(&panel, window) == GS_OK) {
                    next_frame = now_ms();
                    frame_limit = next_frame + GS_UI_SPECS_FRAME_MS * 4;
                }
                else
                    gs_log_warn("ui: the panel would not open");
            }
            state.pressed = GS_UI_HIT_NONE;
            need_paint = 1;
            break;
        }

        case GS_WINDOW_EVENT_KEY:
            gs_log_debug("ui: key %d pressed", event.key);
            if (gs_ui_key_closes(event.key)) {
                /* Escape unwinds one layer at a time. The confirm box
                 * first, then typed search, then an open list, and the
                 * window only when nothing else is left to let go of. */
                if (gs_ui_confirm_visible(&state)) {
                    /* A removal in flight cannot be called back, so the
                     * box stays until the thread reports. */
                    if (!state.confirm_busy) {
                        state.confirm_dir = -1;
                        next_frame = now_ms();
                    }
                    break;
                }
                if (state.search[0] != '\0') {
                    state.search[0] = '\0';
                    gs_ui_panes_pick_category(&state, state.category_sel);
                    need_paint = 1;
                    break;
                }
                if (state.chooser_open) {
                    state.chooser_open = 0;
                    need_paint = 1;
                    break;
                }
                gs_log_info("ui: closing because key %d was pressed",
                            event.key);
                running = 0;
                break;
            }
            if (gs_ui_confirm_visible(&state))
                break;
            if (gs_ui_search_type(&state, event.ch)) {
                gs_ui_panes_pick_category(&state, state.category_sel);
                need_paint = 1;
            }
            break;

        case GS_WINDOW_EVENT_CLOSE:
            gs_log_info("ui: closing because the window manager asked");
            running = 0;
            break;

        default:
            break;
        }
    }

    if (gs_window_error_count(window) > 0)
        gs_log_warn("ui: the server reported %d protocol errors",
                    gs_window_error_count(window));

    /* The panel goes with the main window, so closing one closes both. */
    gs_ui_specs_destroy(&panel);
    gs_window_close(window);
    gs_ui_release();
    gs_library_release();
    gs_catalogue_release();
    gs_store_close();
    gs_log_info("ui: closed");
    return rc;
}

static void ui_shutdown(void)
{
    gs_ui_release();
    gs_store_close();
}

const gs_module gs_ui_module = {
    "ui",
    "open the window and paint the canvas",
    ui_init,
    ui_run,
    ui_shutdown
};
