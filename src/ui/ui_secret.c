/* ui_secret.c
 *
 * The box that asks for the machine password, once, so a drive can be
 * given a line in the system table of filesystems. After that line
 * exists the drive mounts with nothing asked of anybody, so this box is
 * shown once per drive and never again.
 *
 * What is typed lives in this file alone. It is deliberately kept out of
 * the interface state, because that state is handed whole to every
 * drawing routine and to the composer, and a buffer nothing outside this
 * file can name is a buffer nothing outside this file can print. The
 * rest of the program learns only how many characters have been typed,
 * which is what draws the dots.
 *
 * The characters are wiped the moment they have been used, through a
 * pointer marked volatile so the compiler cannot decide the writing is
 * pointless and remove it. They are never written to the log, never put
 * in a command argument, and never placed in the surroundings, because
 * the arguments and the surroundings of every running program can be
 * read by anyone on the machine through /proc.
 */
#include "ui.h"
#include "ui_internal.h"

#include "gravestone.h"
#include "log.h"
#include "mount.h"
#include "str.h"
#include "window.h"

#include <stdio.h>
#include <string.h>

/* Long enough for any passphrase somebody actually uses. */
#define SECRET_MAX 256

static char   typed[SECRET_MAX];
static size_t typed_len;

/* Writing through a volatile pointer, since a plain memset over a buffer
 * nothing reads again is something a compiler is allowed to delete. */
static void wipe(void *start, size_t bytes)
{
    volatile unsigned char *at = start;

    while (bytes-- > 0)
        *at++ = 0;
}

void gs_ui_secret_forget(void)
{
    wipe(typed, sizeof typed);
    typed_len = 0;
}

int gs_ui_secret_visible(const gs_ui_state_t *state)
{
    return state != NULL && state->secret_open;
}

void gs_ui_secret_open(gs_ui_state_t *state, char letter)
{
    if (state == NULL || !gs_mount_letter_ok(letter))
        return;
    gs_ui_secret_forget();
    state->secret_open = 1;
    state->secret_letter = letter;
    state->secret_len = 0;
    state->secret_busy = 0;
    state->secret_note[0] = '\0';
}

void gs_ui_secret_close(gs_ui_state_t *state)
{
    /* The characters go before anything else, so a close from any road
     * out leaves nothing behind. */
    gs_ui_secret_forget();
    if (state == NULL)
        return;
    state->secret_open = 0;
    state->secret_letter = '\0';
    state->secret_len = 0;
    state->secret_busy = 0;
}

int gs_ui_secret_type(gs_ui_state_t *state, int ch)
{
    if (state == NULL || !state->secret_open || state->secret_busy)
        return 0;

    if (ch == 8) {
        if (typed_len == 0)
            return 0;
        typed[--typed_len] = '\0';
        state->secret_len = (int)typed_len;
        return 1;
    }
    /* A line break is the person asking for it to be done, which is
     * answered elsewhere. Everything below a space is left alone. */
    if (ch < 0x20 || ch > 0x7e)
        return 0;
    if (typed_len + 1 >= sizeof typed)
        return 0;
    typed[typed_len++] = (char)ch;
    typed[typed_len] = '\0';
    state->secret_len = (int)typed_len;
    return 1;
}

int gs_ui_secret_length(void)
{
    return (int)typed_len;
}

int gs_ui_secret_apply(gs_ui_state_t *state)
{
    char why[256];
    int rc;

    if (state == NULL || !state->secret_open)
        return GS_ERR_ARG;
    if (typed_len == 0) {
        gs_str_copy(state->secret_note, sizeof state->secret_note,
                    "TYPE THE PASSWORD FIRST");
        return GS_ERR_ARG;
    }

    /* sudo wants the line ended, the same as somebody pressing return. */
    if (typed_len + 1 < sizeof typed) {
        typed[typed_len] = '\n';
        typed[typed_len + 1] = '\0';
    }

    rc = gs_mount_fstab_grant(state->secret_letter, typed, typed_len + 1,
                              why, sizeof why);

    /* Gone the moment it has been used, whatever the answer was. */
    gs_ui_secret_forget();
    state->secret_len = 0;

    gs_str_copy(state->secret_note, sizeof state->secret_note, why);
    if (rc != GS_OK) {
        gs_log_warn("mount: %s", why);
        return rc;
    }
    gs_log_info("mount: %s", why);

    /* The line is in the table, so the drive goes up now rather than at
     * the next start. Mounting needs no password once the line is there,
     * which is the whole point of having written it. */
    {
        char letter = state->secret_letter;
        gs_mount_outcome_t outcome = gs_mount_restore(letter, why,
                                                      sizeof why);

        if (outcome == GS_MOUNT_OK_REMOUNTED ||
            outcome == GS_MOUNT_OK_ALREADY) {
            gs_log_info("mount: %s", why);
            gs_str_copy(state->status, sizeof state->status,
                        "DRIVE ATTACHED");
        } else {
            gs_log_warn("mount: %s", why);
        }
    }
    gs_ui_secret_close(state);
    return GS_OK;
}

gs_ui_rect_t gs_ui_secret_rect(int width, int height)
{
    gs_ui_rect_t r;

    r.w = GS_UI_SECRET_W;
    r.h = GS_UI_SECRET_H;
    if (width < r.w + 20)
        r.w = width - 20;
    if (r.w < 0)
        r.w = 0;
    r.x = (width - r.w) / 2;
    r.y = (height - r.h) / 2;
    if (r.y < 0)
        r.y = 0;
    return r;
}

gs_ui_rect_t gs_ui_secret_field_rect(int width, int height)
{
    gs_ui_rect_t box = gs_ui_secret_rect(width, height);
    gs_ui_rect_t r;

    r.x = box.x + 16;
    r.y = box.y + box.h - 78;
    r.w = box.w - 32;
    r.h = 28;
    if (r.w < 0)
        r.w = 0;
    return r;
}

gs_ui_rect_t gs_ui_secret_ok_rect(int width, int height)
{
    gs_ui_rect_t box = gs_ui_secret_rect(width, height);
    gs_ui_rect_t r;

    r.w = 96;
    r.h = 28;
    r.x = box.x + box.w - 16 - r.w;
    r.y = box.y + box.h - 16 - r.h;
    return r;
}

gs_ui_rect_t gs_ui_secret_no_rect(int width, int height)
{
    gs_ui_rect_t ok = gs_ui_secret_ok_rect(width, height);
    gs_ui_rect_t r = ok;

    r.w = 84;
    r.x = ok.x - 10 - r.w;
    return r;
}

void gs_ui_secret_compose(unsigned int *px, int w, int h,
                          const gs_ui_state_t *state)
{
    gs_ui_rect_t box, field, ok, no;
    size_t i, n;

    if (px == NULL || !gs_ui_secret_visible(state))
        return;

    /* Everything under the box drops to half brightness, so the box is
     * unmistakably the only thing awake. */
    n = (size_t)w * (size_t)h;
    for (i = 0; i < n; i++)
        px[i] = (px[i] >> 1) & 0x007f7f7fu;

    box = gs_ui_secret_rect(w, h);
    if (box.w <= 0)
        return;
    gs_ui_px_rounded(px, w, h, box, 5, GS_UI_BAR_FILL, GS_UI_BTN_EDGE);

    field = gs_ui_secret_field_rect(w, h);
    gs_ui_px_rounded(px, w, h, field, 4, GS_UI_BTN_FACE_DOWN, GS_UI_BTN_EDGE);

    ok = gs_ui_secret_ok_rect(w, h);
    no = gs_ui_secret_no_rect(w, h);
    gs_ui_px_rounded(px, w, h, ok, 4,
                     state->secret_busy ? GS_UI_BTN_FACE_DOWN
                     : (state->hover == GS_UI_HIT_SECRET_OK
                        ? GS_UI_BTN_FACE_HOVER : GS_UI_BTN_FACE),
                     GS_UI_BTN_EDGE);
    gs_ui_px_rounded(px, w, h, no, 4,
                     state->hover == GS_UI_HIT_SECRET_NO
                         ? GS_UI_BTN_FACE_HOVER : GS_UI_BTN_FACE,
                     GS_UI_BTN_EDGE);
}

void gs_ui_secret_labels(gs_window_t *win, const gs_ui_state_t *state)
{
    gs_ui_rect_t box, field, ok, no;
    char line[200];
    char dots[GS_UI_SECRET_DOTS + 1];
    int w, h, y, step, shown, i;

    if (win == NULL || !gs_ui_secret_visible(state))
        return;
    w = gs_window_width(win);
    h = gs_window_height(win);
    box = gs_ui_secret_rect(w, h);
    if (box.w <= 0)
        return;

    step = gs_window_font_height(win) + 4;
    y = box.y + 14 + gs_window_font_ascent(win);

    snprintf(line, sizeof line, "DRIVE %c: NEEDS SETTING UP ONCE",
             state->secret_letter);
    gs_window_text(win, box.x + 16, y, line, GS_UI_BTN_LABEL);
    y += step + 4;

    gs_window_text(win, box.x + 16, y,
                   "One line goes into /etc/fstab so this drive",
                   GS_UI_BTN_OFF_LABEL);
    y += step;
    gs_window_text(win, box.x + 16, y,
                   "mounts from now on with no password at all.",
                   GS_UI_BTN_OFF_LABEL);
    y += step;
    gs_window_text(win, box.x + 16, y,
                   "The old file is kept beside it.",
                   GS_UI_BTN_OFF_LABEL);

    /* One dot per character, counted rather than shown, and the count is
     * all this file ever hands out. */
    field = gs_ui_secret_field_rect(w, h);
    shown = state->secret_len;
    if (shown > GS_UI_SECRET_DOTS)
        shown = GS_UI_SECRET_DOTS;
    for (i = 0; i < shown; i++)
        dots[i] = '*';
    dots[shown] = '\0';
    gs_window_text(win, field.x + 8,
                   field.y + (field.h - gs_window_font_height(win)) / 2 +
                       gs_window_font_ascent(win),
                   shown > 0 ? dots : "machine password",
                   shown > 0 ? GS_UI_BTN_LABEL : GS_UI_BTN_OFF_LABEL);

    if (state->secret_note[0] != '\0')
        gs_window_text(win, box.x + 16, field.y - 8, state->secret_note,
                       GS_UI_BTN_OFF_LABEL);

    ok = gs_ui_secret_ok_rect(w, h);
    no = gs_ui_secret_no_rect(w, h);
    y = ok.y + (ok.h - gs_window_font_height(win)) / 2 +
        gs_window_font_ascent(win);
    gs_window_text(win, ok.x + 24, y,
                   state->secret_busy ? "WORKING" : "SET UP",
                   state->secret_busy ? GS_UI_BTN_OFF_LABEL : GS_UI_BTN_LABEL);
    gs_window_text(win, no.x + 20, y, "NOT NOW", GS_UI_BTN_LABEL);
}
