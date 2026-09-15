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
#include "ghost.h"
#include "harness.h"
#include "library.h"
#include "log.h"
#include "mount.h"
#include "provider.h"
#include "session.h"
#include "sig.h"
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
#define GS_CHAR_ESCAPE 27

/* The composition buffer is kept between repaints and only grown when the
 * window does, since a resize drag would otherwise churn it. */
static unsigned int *canvas;
static size_t canvas_len;

int gs_ui_key_closes(int ch)
{
    return ch == GS_CHAR_ESCAPE;
}

int gs_ui_key_sends(int ch, int mods)
{
    return ch == '\n' && (mods & GS_WINDOW_MOD_SHIFT) == 0;
}

int gs_ui_wait_ms(int planned, int paint_owed)
{
    /* A frame the loop already owes beats every animation clock. The
     * caret turns over twice a second, and the wait that carries it used
     * to be taken whole, so a keystroke was drawn on the caret's clock
     * rather than on its own. That put as much as half a second between
     * a key going down and its letter appearing, and anyone typing at
     * speed watched whole words land in one lump. */
    if (paint_owed)
        return 0;
    if (planned < -1)
        return -1;
    return planned;
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
    /* The Windows drives this machine had attached go back before
     * anything reads the disks, since a drive put back after the reading
     * would be missing from the list until the person pressed something.
     * A drive that is already working is left alone and costs nothing. A
     * drive that has left the machine gets a warning and the program
     * carries on without it. */
    if (gs_mount_open() == GS_OK) {
        /* The drives are put back on a thread of their own, since the
         * road to root can take ten seconds the first time and the
         * window is not made to wait for it. The disks are read again
         * the moment the thread reports. */
        if (gs_ui_mount_begin() != GS_OK)
            gs_log_debug("ui: no drives to watch on this machine");
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
        gs_ui_settings_restore(&state);
    }
    state.screen = GS_UI_SCREEN_HOME;
    state.record_open = -1;      /* nothing is open to begin with */

    /* The inference server holds the weights in memory and answers over
     * HTTP, and it does not survive the machine restarting. Starting it
     * now means it is ready by the time anybody has finished typing, and
     * the wait of nought means the window opens without waiting for it.
     * A server already answering is left alone. */
    if (gs_provider_start(0) != GS_OK)
        gs_log_debug("ui: no inference server could be started yet");

    /* The conversation this install was last in, so the panel opens on
     * the words the person left there. */
    gs_ui_chat_resume(&state);

    /* A leftover from an earlier run is cleared before this one opens
     * its own window, so nothing that belongs to us is ever in the way of
     * the sweep. */
    if (gs_ghost_available()) {
        int hidden = 0;
        int already = 0;

        if (gs_ghost_sweep(GS_UI_TITLE, &hidden, &already) == GS_OK &&
            (hidden > 0 || already > 0))
            gs_log_debug("ghost: %d hidden now, %d already hidden",
                         hidden, already);
    }

    /* Interrupt and terminate arrive down a pipe the wait watches, so a
     * stop request ends the loop the same way the close button does and
     * the window comes down properly. */
    if (gs_sig_install() != GS_OK)
        gs_log_debug("ui: stop signals could not be caught");
    gs_window_set_wake_fd(gs_sig_wake_fd());

    window = gs_window_open(GS_UI_TITLE, UI_WIDTH, UI_HEIGHT);
    if (window == NULL) {
        gs_log_error("ui: the window could not be opened");
        /* The drive thread is already running and may be half way
         * through putting a drive back, so it is waited for rather than
         * cut off with the drive detached. */
        gs_ui_mount_wait();
        gs_mount_close();
        gs_store_close();
        return GS_ERR;
    }
    /* The font is not the same width on every machine, so the layout is
     * told what this one measures before anything is drawn. */
    gs_ui_set_glyph_width(gs_window_text_width(window, "M"));

    /* Growing to the whole screen is asked for before anything is drawn,
     * so the first frame is already the right size. The frame stays, so
     * the buttons that shrink, grow and close it are still along the
     * edge. A manager that refuses leaves the window as it was made. */
    if (gs_window_maximise(window, 1) != GS_OK)
        gs_log_debug("ui: this screen manager would not grow the window");

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
        if (state.screen == GS_UI_SCREEN_HOME) {
            long long now = now_ms();

            /* The caret turns over twice a second, and the count box
             * moves on the same clock while it is growing. */
            if (now >= next_frame) {
                if (gs_ui_fleet_animating(&state)) {
                    gs_ui_fleet_advance(&state);
                    next_frame = now + GS_UI_SPECS_FRAME_MS;
                } else {
                    state.cursor_on = !state.cursor_on;
                    next_frame = now + GS_UI_CARET_MS;
                }
                need_paint = 1;
            }
            timeout = (int)(next_frame - now_ms());
            if (timeout < 0)
                timeout = 0;
        } else if (state.confirm_busy) {
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
        } else {
            timeout = -1;
        }

        /* Whatever the animation on screen asked for, a frame already
         * owed goes out now. */
        timeout = gs_ui_wait_ms(timeout, need_paint);

        /* While the drives are watched, the loop comes round at least
         * once a second to collect what the watcher found, since a
         * workspace with nothing moving would otherwise wait for ever. */
        if (gs_ui_mount_running() && (timeout < 0 || timeout > 1000))
            timeout = 1000;

        got = gs_window_wait_any(windows, count, &which, &event, timeout);

        /* The file box answers on its own thread, so the answer is
         * collected here rather than where it was asked for. */
        if (gs_ui_attach_poll(&state))
            need_paint = 1;

        /* A drive that came or went is reported by the watcher, and the
         * list of disks is read again when one did. */
        if (gs_ui_mount_poll(&state))
            need_paint = 1;

        /* A run puts the question to each model in turn, so the panel is
         * repainted while it works and once it is done. */
        {
            int ww = gs_window_width(window);
            int wh = gs_window_height(window);
            int before = gs_ui_chat_content_height(&state, ww, wh);

            if (gs_ui_harness_poll(&state)) {
                /* A reader scrolled back through older lines stays on
                 * what they were reading when an answer lands below it.
                 * The view is measured from the newest end, so it moves
                 * back by exactly what arrived. One already at the newest
                 * stays there. */
                if (state.history_back > 0)
                    state.history_back +=
                        gs_ui_chat_content_height(&state, ww, wh) - before;
                need_paint = 1;
            }
        }

        if (gs_sig_quit_requested()) {
            gs_sig_drain();
            gs_log_info("ui: closing because signal %d arrived",
                        gs_sig_quit_number());
            running = 0;
            continue;
        }

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
                if (gs_ui_key_closes(event.ch)) {
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

            /* A grip already held follows the pointer, and nothing else
             * answers while it does. */
            if (state.drag_bar != GS_UI_BAR_NONE) {
                if (gs_ui_bar_drag(&state, gs_window_width(window),
                                   gs_window_height(window), event.y))
                    need_paint = 1;
                break;
            }

            state.hover = gs_ui_hit_test(&state, gs_window_width(window),
                                         gs_window_height(window),
                                         event.x, event.y, &state.hover_row);

            /* The shape follows what the pointer is over, so a person
             * knows what a control does before pressing it. Asking for
             * the shape already showing costs nothing. */
            gs_window_set_cursor(window, gs_ui_cursor_for(state.hover));

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

        case GS_WINDOW_EVENT_RELEASE:
            /* Letting go ends the drag wherever the pointer happens to
             * be, including outside the window. */
            gs_ui_bar_drop(&state);
            break;

        case GS_WINDOW_EVENT_CLICK: {
            gs_ui_hit_t hit;
            int row = -1;

            /* A wheel notch arrives as a button, four for up and five for
             * down. It scrolls whichever list the pointer is over. */
            if (event.button == 4 || event.button == 5) {
                /* The record sits over everything while it is up, so the
                 * wheel moves it and nothing under it. */
                if (gs_ui_record_showing(&state)) {
                    int limit = gs_ui_record_scroll_limit(
                        &state, gs_window_width(window),
                        gs_window_height(window),
                        gs_window_font_height(window) + 3);
                    int want = state.record_scroll +
                               (event.button == 4 ? -3 : 3);

                    if (want < 0)
                        want = 0;
                    if (want > limit)
                        want = limit;
                    if (want != state.record_scroll) {
                        state.record_scroll = want;
                        need_paint = 1;
                    }
                    break;
                }
                if (gs_ui_fleet_visible(&state)) {
                    int limit = state.library_count -
                                gs_ui_fleet_rows(&state);
                    int want = state.fleet_scroll +
                               (event.button == 4 ? -2 : 2);

                    if (limit < 0)
                        limit = 0;
                    if (want < 0)
                        want = 0;
                    if (want > limit)
                        want = limit;
                    if (want != state.fleet_scroll) {
                        state.fleet_scroll = want;
                        need_paint = 1;
                    }
                    break;
                }
                if (gs_ui_scroll_under(&state, window, event.x, event.y,
                                 event.button == 4 ? -3 : 3))
                    need_paint = 1;
                break;
            }
            if (event.button != 1)
                break;

            /* A grip is taken hold of before anything else answers, so a
             * press that lands on a bar begins a drag rather than
             * pressing whatever sits under it. */
            if (gs_ui_bar_grab(&state, gs_window_width(window),
                               gs_window_height(window),
                               event.x, event.y)) {
                need_paint = 1;
                break;
            }

            hit = gs_ui_hit_test(&state, gs_window_width(window),
                                 gs_window_height(window),
                                 event.x, event.y, &row);
            state.pressed = hit;
            if (hit == GS_UI_HIT_SETTINGS) {
                state.screen = GS_UI_SCREEN_WORKSPACE;
            } else if (hit == GS_UI_HIT_FLEET) {
                state.fleet_step = 0;
                state.fleet_dir = 1;
                state.fleet_scroll = 0;
                next_frame = now_ms();
            } else if (hit == GS_UI_HIT_FLEET_OPTION) {
                /* The box stays open, since choosing several models is
                 * the point of it. */
                gs_ui_fleet_toggle(&state, row);
                gs_ui_settings_save(&state);
            } else if (hit == GS_UI_HIT_SCROLL) {
                /* The hit test worked out which row a press at that
                 * height puts at the top, so the list only has to be
                 * moved there. Which pane it was is settled by which one
                 * holds the point. */
                int ww = gs_window_width(window);
                int hh = gs_window_height(window);

                if (gs_ui_rect_contains(gs_ui_left_rect(ww, hh),
                                        event.x, event.y))
                    state.model_scroll = row;
                else if (gs_ui_rect_contains(gs_ui_right_rect(ww, hh),
                                             event.x, event.y))
                    state.library_scroll = row;
                need_paint = 1;
            } else if (hit == GS_UI_HIT_NOTE) {
                /* One panel at a time. The box of models is shut before
                 * the record opens, so the two never stack. It folds
                 * away rather than vanishing, since a panel that blinks
                 * out reads as a fault. */
                if (gs_ui_fleet_visible(&state))
                    state.fleet_dir = -1;
                gs_ui_record_open(&state, row);
                need_paint = 1;
            } else if (hit == GS_UI_HIT_RECORD_SHUT) {
                gs_ui_record_shut(&state);
                need_paint = 1;
            } else if (hit == GS_UI_HIT_CHAT_EARLIER) {
                /* The next page back. It opens at its newest line, which
                 * is the prompt asked just before the oldest one the
                 * reader was looking at, so reading carries straight on
                 * upwards. */
                if (gs_ui_fleet_visible(&state))
                    state.fleet_dir = -1;
                state.history_skip += GS_UI_HISTORY_PAGE;
                state.history_back = 0;
                gs_ui_chat_reload(&state);
                need_paint = 1;
            } else if (hit == GS_UI_HIT_CHAT_NEWER) {
                /* The page after. It opens at its oldest line, which is
                 * the prompt asked just after the newest one the reader
                 * was looking at. */
                if (gs_ui_fleet_visible(&state))
                    state.fleet_dir = -1;
                state.history_skip -= GS_UI_HISTORY_PAGE;
                if (state.history_skip < 0)
                    state.history_skip = 0;
                gs_ui_chat_reload(&state);
                state.history_back = gs_ui_chat_scroll_reach(
                    &state, gs_window_width(window),
                    gs_window_height(window));
                need_paint = 1;
            } else if (hit == GS_UI_HIT_CHAT_SEND) {
                if (gs_ui_chat_send(&state) == GS_OK) {
                    gs_ui_chat_reload(&state);
                    need_paint = 1;
                }
            } else if (hit == GS_UI_HIT_CHAT_ATTACH) {
                gs_ui_attach_begin(&state);
            } else if (hit == GS_UI_HIT_FLEET_CLOSE ||
                       hit == GS_UI_HIT_FLEET_AWAY) {
                /* The cross, or anywhere outside the box. Both fold it
                 * away the same way escape does. */
                state.fleet_dir = -1;
                next_frame = now_ms();
            } else if (hit == GS_UI_HIT_BACK) {
                gs_ui_settings_save(&state);
                state.screen = GS_UI_SCREEN_HOME;
            } else if (hit == GS_UI_HIT_CHOOSER) {
                state.chooser_open = !state.chooser_open;
            } else if (hit == GS_UI_HIT_DROP_ROW) {
                gs_ui_panes_pick_category(&state, row);
                state.chooser_open = 0;
                gs_ui_settings_save(&state);
            } else if (hit == GS_UI_HIT_LEFT_ROW) {
                state.model_sel = row;
                state.chooser_open = 0;
                gs_ui_settings_save(&state);
            } else if (hit == GS_UI_HIT_MID_ROW) {
                gs_ui_panes_pick_disk(&state, row);
                state.disk_picked = 1;
                state.chooser_open = 0;
                state.alert[0] = '\0';
                gs_ui_settings_save(&state);
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
            } else if (hit == GS_UI_HIT_SECRET_OK) {
                if (!state.secret_busy) {
                    state.secret_busy = 1;
                    gs_ui_paint(window, &state);
                    (void)gs_ui_secret_apply(&state);
                    state.secret_busy = 0;
                    need_paint = 1;
                }
            } else if (hit == GS_UI_HIT_SECRET_NO) {
                /* Turned down once is turned down for good, so the box
                 * does not come back at every start. */
                (void)gs_mount_decline(state.secret_letter);
                gs_ui_secret_close(&state);
                need_paint = 1;
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
            /* The key number maps straight back to a letter on any
             * known layout, so while the password box is up not even
             * the debug log learns what was pressed. */
            if (!gs_ui_secret_visible(&state))
                gs_log_debug("ui: key %d pressed", event.key);
            if (gs_ui_key_closes(event.ch)) {
                /* Escape unwinds one layer at a time. The confirm box
                 * first, then typed search, then an open list, and the
                 * window only when nothing else is left to let go of. */
                if (gs_ui_secret_visible(&state)) {
                    /* Nothing typed survives the box closing, and escape
                     * counts as turning it down. */
                    if (!state.secret_busy) {
                        (void)gs_mount_decline(state.secret_letter);
                        gs_ui_secret_close(&state);
                        need_paint = 1;
                    }
                    break;
                }
                if (gs_ui_confirm_visible(&state)) {
                    /* A removal in flight cannot be called back, so the
                     * box stays until the thread reports. */
                    if (!state.confirm_busy) {
                        state.confirm_dir = -1;
                        next_frame = now_ms();
                    }
                    break;
                }
                if (gs_ui_fleet_visible(&state)) {
                    state.fleet_dir = -1;
                    next_frame = now_ms();
                    need_paint = 1;
                    break;
                }
                if (state.screen == GS_UI_SCREEN_HOME &&
                    state.prompt[0] != '\0') {
                    state.prompt[0] = '\0';
                    need_paint = 1;
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
                if (state.screen == GS_UI_SCREEN_WORKSPACE) {
                    gs_ui_settings_save(&state);
                    state.screen = GS_UI_SCREEN_HOME;
                    need_paint = 1;
                    break;
                }
                gs_log_info("ui: closing because key %d was pressed",
                            event.key);
                running = 0;
                break;
            }
            /* While the password box is open it takes every keystroke,
             * so nothing typed into it can reach the prompt and be sent
             * to a model or written into the conversation. */
            if (gs_ui_secret_visible(&state)) {
                if (event.ch == '\n') {
                    if (!state.secret_busy) {
                        state.secret_busy = 1;
                        need_paint = 1;
                        gs_ui_paint(window, &state);
                        (void)gs_ui_secret_apply(&state);
                        state.secret_busy = 0;
                    }
                } else if (gs_ui_secret_type(&state, event.ch)) {
                    state.cursor_on = 1;
                    next_frame = now_ms() + GS_UI_CARET_MS;
                }
                need_paint = 1;
                break;
            }

            /* On the home screen every keystroke belongs to the prompt
             * box, which is the only field there. */
            if (state.screen == GS_UI_SCREEN_HOME) {
                if (gs_ui_fleet_visible(&state))
                    break;
                /* Return on its own sends what is written. Shift and
                 * return goes to the next line instead, since a question
                 * worth asking often needs more than one. Every chat box
                 * the user already has open works this way round. */
                if (gs_ui_key_sends(event.ch, event.mods)) {
                    if (gs_ui_chat_send(&state) == GS_OK) {
                        gs_ui_chat_reload(&state);
                        need_paint = 1;
                    }
                    break;
                }
                if (gs_ui_chat_type(&state, event.ch)) {
                    /* The caret is held on while the keyboard is busy. A
                     * shape flashing on and off under the letter being
                     * written reads as a stutter, so the blink clock is
                     * pushed out and only starts again once the typing
                     * stops. */
                    state.cursor_on = 1;
                    next_frame = now_ms() + GS_UI_CARET_MS;
                    need_paint = 1;
                }
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

    /* A removal or a download runs on its own thread and reads the
     * library table as it goes. The loop can now be left at any moment,
     * by the close button or by a stop signal, so the worker is waited
     * for here before anything below empties the table under it. */
    gs_ui_remove_wait();
    gs_ui_attach_wait();
    gs_harness_wait();

    if (gs_window_error_count(window) > 0)
        gs_log_warn("ui: the server reported %d protocol errors",
                    gs_window_error_count(window));

    if (gs_store_setting_count() > 0 ||
        state.screen == GS_UI_SCREEN_WORKSPACE)
        gs_ui_settings_save(&state);

    /* The panel goes with the main window, so closing one closes both. */
    gs_ui_specs_destroy(&panel);
    gs_window_close(window);
    gs_window_set_wake_fd(-1);

    /* The windows are down, so anything still mirrored on the Windows
     * side is a leftover. A child does the sweep and this process exits
     * without waiting for it. */
    gs_ghost_sweep_detached(GS_UI_TITLE);

    /* The drive thread is waited for only now that the window is down.
     * A drive that has gone to sleep can hold a mount command for twenty
     * seconds, and a window frozen for that long looks broken, while a
     * process that lingers for it does not. */
    gs_ui_mount_wait();

    gs_ui_secret_forget();
    gs_ui_release();
    gs_mount_close();
    gs_session_close();
    gs_library_release();
    gs_catalogue_release();
    gs_store_close();
    gs_log_info("ui: closed");

    /* A program stopped by a signal should look stopped by that signal to
     * whatever started it, so the same signal is raised again now that
     * the tidying is done. */
    if (gs_sig_quit_requested())
        gs_sig_reraise();
    gs_sig_release();
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
