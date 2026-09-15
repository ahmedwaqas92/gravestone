/* ui_chat_actions.c
 *
 * The conversation half of the interface. Sending a prompt, collecting a
 * run, and reading a conversation back out of the file.
 *
 * The machine half, which mounts a disk and manages the model list, lives
 * in ui_actions.c. Both fill in the same state, so nothing above this
 * directory knows the work is split in two.
 */
#include "ui.h"
#include "ui_internal.h"
#include "gravestone.h"
#include "catalogue.h"
#include "detect.h"
#include "harness.h"
#include "provider.h"
#include "library.h"
#include "log.h"
#include "session.h"
#include "store.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A prompt is worth keeping the moment it is sent, so it goes into the
 * conversation before anything is asked of a model. A person closing the
 * program then finds their question where they left it. */
int gs_ui_chat_send(gs_ui_state_t *state)
{
    char asked[sizeof state->prompt];
    long long turn = 0;
    size_t i;

    if (state == NULL)
        return GS_ERR_ARG;

    /* A prompt of nothing but spaces and breaks asks nothing. */
    for (i = 0; state->prompt[i] != '\0'; i++)
        if (state->prompt[i] != ' ' && state->prompt[i] != '\n' &&
            state->prompt[i] != '\t')
            break;
    if (state->prompt[i] == '\0')
        return GS_ERR_ARG;

    /* One run at a time. A prompt sent while a model is still thinking
     * used to be written down and cleared from the box, and then the run
     * refused it, so it sat in the history never asked. It is refused
     * here instead, before anything is written, and the words stay in
     * the box to send again. */
    if (gs_harness_running()) {
        gs_str_copy(state->status, sizeof state->status, "BUSY");
        gs_str_copy(state->detail, sizeof state->detail,
                    "a run is still going, send again when it ends");
        return GS_ERR_ARG;
    }

    if (gs_session_open() != GS_OK)
        return GS_ERR;

    /* Whatever page the reader was on, the prompt they just sent is on
     * the newest one, against the box. */
    state->history_skip = 0;
    state->history_back = 0;

    if (state->session_id == 0) {
        char number[32];

        if (gs_session_start(NULL, &state->session_id) != GS_OK)
            return GS_ERR;
        /* Written down as this window's own, so the next start comes
         * back to it rather than to whichever conversation is newest. A
         * run from the command line makes a conversation of its own, and
         * without this the window opened on that one and the person's
         * chat looked lost. */
        snprintf(number, sizeof number, "%lld", state->session_id);
        (void)gs_store_setting_put(GS_UI_KEY_CHAT, number);
    }
    if (gs_session_add_turn(state->session_id, state->prompt, &turn) != GS_OK)
        return GS_ERR;

    gs_log_info("ui: prompt %lld recorded in conversation %lld",
                turn, state->session_id);
    /* The box is emptied so the person can type the next question, and
     * the words are kept here because the panel has not read the file
     * back yet and holds the line before this one. */
    gs_str_copy(asked, sizeof asked, state->prompt);
    state->prompt[0] = '\0';

    /* The question goes to the ticked models one at a time, since a
     * model filling the machine leaves no room for a second beside it.
     * The run is on a thread, so the window keeps painting while a model
     * thinks. */
    {
        char quick[GS_HARNESS_MODELS][GS_PROVIDER_MODEL];
        char slow[GS_HARNESS_MODELS][GS_PROVIDER_MODEL];
        const char (*chosen)[GS_PROVIDER_MODEL];
        const gs_detect_report_t *machine = gs_ui_machine();
        int n_quick = 0;
        int n_slow = 0;
        int too_large = 0;
        int over_cap = 0;
        int picked;
        int k;

        /* Every ticked model is sorted into one of the two lists, and the
         * walk runs to the end of the library even once both are full, so
         * the message a person reads accounts for every model they
         * ticked rather than for the first few. */
        for (k = 0; k < state->library_count; k++) {
            const gs_library_entry_t *entry;
            gs_catalogue_fit_t fit;

            if (!gs_ui_fleet_chosen(state, k))
                continue;
            entry = gs_library_at(k);
            if (entry == NULL)
                continue;

            /* Each model has the whole machine to itself while it is the
             * one being asked, so it only has to fit on its own. A
             * machine that would not report itself is taken at the
             * person's word rather than second guessed.
             *
             * gs_catalogue_fit alone decides here. Whether a model was
             * worth fetching is a question about the list somebody picks
             * from, and this model is already on the disk, so it runs
             * however slowly it runs. */
            fit = machine != NULL ? gs_catalogue_fit(entry->bytes, machine)
                                  : GS_FIT_GRAPHICS;
            if (fit == GS_FIT_NONE) {
                too_large++;
            } else if (gs_catalogue_conversational(fit)) {
                if (n_quick < GS_HARNESS_MODELS)
                    gs_str_copy(quick[n_quick++], GS_PROVIDER_MODEL,
                                entry->name);
                else
                    over_cap++;
            } else {
                if (n_slow < GS_HARNESS_MODELS)
                    gs_str_copy(slow[n_slow++], GS_PROVIDER_MODEL,
                                entry->name);
                else
                    over_cap++;
            }
        }

        /* Models held entirely in graphics memory answer at
         * conversational speed. Anything else reads part of itself out of
         * system memory once for every word, so a run of several of them
         * finishes after the person has gone home. The slow ones are
         * asked only when nothing quick was ticked. */
        if (n_quick > 0) {
            chosen = quick;
            picked = n_quick;
        } else {
            chosen = slow;
            picked = n_slow;
        }

        if (picked == 0) {
            gs_str_copy(state->status, sizeof state->status, "NO MODEL");
            if (too_large > 0)
                snprintf(state->detail, sizeof state->detail,
                         "%d ticked model%s too large for this machine",
                         too_large, too_large == 1 ? " is" : "s are");
            else
                gs_str_copy(state->detail, sizeof state->detail,
                            "tick a model in the box before sending");
            gs_log_warn("ui: nothing was asked, %d too large, %d over the "
                        "limit of %d", too_large, over_cap,
                        GS_HARNESS_MODELS);
            return GS_OK;
        }

        /* Whatever was left out is said on screen rather than only in the
         * log, since a person watching their ticks quietly ignored has no
         * way to find out why. */
        {
            char why[192];
            int at = 0;

            why[0] = '\0';
            if (n_quick == 0)
                at += snprintf(why + at, sizeof why - (size_t)at,
                               "%snothing fits the card, so this will be "
                               "slow", at > 0 ? ", " : "");
            if (n_quick > 0 && n_slow > 0)
                at += snprintf(why + at, sizeof why - (size_t)at,
                               "%s%d left out as too slow",
                               at > 0 ? ", " : "", n_slow);
            if (too_large > 0)
                at += snprintf(why + at, sizeof why - (size_t)at,
                               "%s%d too large", at > 0 ? ", " : "",
                               too_large);
            if (over_cap > 0)
                snprintf(why + at, sizeof why - (size_t)at,
                         "%s%d past the limit of %d", at > 0 ? ", " : "",
                         over_cap, GS_HARNESS_MODELS);

            if (why[0] != '\0') {
                snprintf(state->detail, sizeof state->detail,
                         "asking %d, %s", picked, why);
                gs_log_warn("ui: %s", state->detail);
            }
        }

        gs_harness_corpus("data/corpus");
        if (gs_harness_begin(state->session_id, turn, asked,
                             chosen, picked) != GS_OK) {
            /* Asking for one to be started returns as soon as it has
             * been set going, so the window never waits. The weights
             * take a few seconds to be read, and the next question finds
             * it up. */
            if (!gs_provider_available() && gs_provider_start(0) == GS_OK) {
                gs_str_copy(state->status, sizeof state->status,
                            "STARTING SERVER");
                gs_str_copy(state->detail, sizeof state->detail,
                            "ask again in a moment");
            } else {
                gs_str_copy(state->status, sizeof state->status, "NO SERVER");
                gs_str_copy(state->detail, sizeof state->detail,
                            "no inference server is answering");
            }
        }
    }
    return GS_OK;
}

/* Collects a finished run and puts what it decided into the panel. */
int gs_ui_harness_poll(gs_ui_state_t *state)
{
    gs_harness_state_t run;

    if (state == NULL || !gs_harness_running())
        return 0;

    gs_harness_read(&run);
    /* A prompt refused because this run was going stays refused on the
     * screen until the run ends, since the words left in the box are only
     * half the answer and the reason is the other half. The progress
     * moves into the line under it. */
    if (strcmp(state->status, "BUSY") == 0) {
        if (run.stage == GS_HARNESS_ASKING && run.now[0] != '\0')
            snprintf(state->detail, sizeof state->detail,
                     "still asking %.40s, send after", run.now);
        else if (run.stage == GS_HARNESS_CHECKING)
            gs_str_copy(state->detail, sizeof state->detail,
                        "still checking, send after");
    } else if (run.stage == GS_HARNESS_ASKING && run.now[0] != '\0') {
        snprintf(state->status, sizeof state->status, "ASKING %d OF %d",
                 run.answered + 1, run.models);
        gs_str_copy(state->detail, sizeof state->detail, run.now);
    } else if (run.stage == GS_HARNESS_CHECKING) {
        gs_str_copy(state->status, sizeof state->status, "CHECKING");
        snprintf(state->detail, sizeof state->detail,
                 "%d answer%s collected", run.answered,
                 run.answered == 1 ? "" : "s");
    }

    if (!gs_harness_poll())
        return 1;                      /* still going, the panel repaints */

    gs_harness_read(&run);

    /* The record of the whole run is kept against the turn that produced
     * it, so pressing the note on that line shows every model's part in
     * it, today and after a restart. */
    /* The run wrote its own record against the prompt, so the reload
     * brings back the answer, or the line saying why none is shown, with
     * the record behind it. */
    gs_ui_chat_reload(state);

    if (run.stage == GS_HARNESS_DONE) {
        snprintf(state->status, sizeof state->status, "ANSWERED");
        snprintf(state->detail, sizeof state->detail,
                 "%d of %d survived the checking", run.passed, run.models);
    } else {
        gs_str_copy(state->status, sizeof state->status, "NOTHING SURVIVED");
        snprintf(state->detail, sizeof state->detail,
                 "%d model%s answered, none passed", run.answered,
                 run.answered == 1 ? "" : "s");
    }
    return 1;
}

void gs_ui_say(gs_ui_state_t *state, const char *text)
{
    int at;

    if (state == NULL || text == NULL)
        return;
    if (state->history_count >= GS_UI_HISTORY_MAX)
        return;

    at = state->history_count;
    /* The screen draws ASCII alone, so the line is written that way here
     * and every measurement of it agrees with what is drawn. */
    gs_str_to_ascii(text, state->history[at], GS_UI_HISTORY_TEXT);
    state->history_mine[at] = 0;
    state->history_kind[at] = GS_UI_LINE_SAID;
    state->record_turn[at] = 0;
    state->record_answer[at] = 0;
    state->record_by[at][0] = '\0';
    /* Said now, since this line comes from the panel rather than the
     * file and has no recorded moment of its own. */
    state->history_at[at] = (long long)time(NULL);
    state->history_count++;
    state->history_gen = gs_ui_chat_generation();
}

long long gs_ui_chat_generation(void)
{
    static long long generation;

    return ++generation;
}

/* Puts one line on the page. Returns where it went, or -1 once the page
 * is full. */
static int add_line(gs_ui_state_t *state, gs_ui_line_t kind, int mine,
                    const char *text, long long at, long long turn)
{
    int i = state->history_count;

    if (i >= GS_UI_HISTORY_MAX)
        return -1;
    /* The database keeps what was written, and the screen gets its plain
     * spelling, since the glyph code draws ASCII alone and dropped a
     * dash's three bytes without a trace. */
    gs_str_to_ascii(text, state->history[i], GS_UI_HISTORY_TEXT);
    state->history_kind[i] = (unsigned char)kind;
    state->history_mine[i] = (unsigned char)(mine != 0);
    state->history_at[i] = at;
    state->record_turn[i] = turn;
    state->history_count++;
    return i;
}

void gs_ui_chat_reload(gs_ui_state_t *state)
{
    gs_session_line_t *page;
    gs_session_answer_t shown;
    long long running = 0;
    int total;
    int n;
    int i;

    if (state == NULL)
        return;
    state->history_count = 0;
    state->history_older = 0;
    memset(state->record_answer, 0, sizeof state->record_answer);
    memset(state->record_turn, 0, sizeof state->record_turn);
    memset(state->record_by, 0, sizeof state->record_by);
    memset(state->history_at, 0, sizeof state->history_at);
    memset(state->history_kind, 0, sizeof state->history_kind);
    memset(state->history_mine, 0, sizeof state->history_mine);
    state->history_gen = gs_ui_chat_generation();

    /* One history, drawn from every conversation in the file in the order
     * the prompts were asked. The panel used to show one conversation,
     * and a run from the command line or a fresh conversation started by
     * the window each split the history, so prompts a person had sent sat
     * in a conversation the panel was not showing. */
    total = gs_session_prompt_count();
    if (total <= 0) {
        state->history_skip = 0;
        return;
    }
    if (state->history_skip < 0)
        state->history_skip = 0;
    if (state->history_skip >= total)
        state->history_skip = (total - 1) / GS_UI_HISTORY_PAGE *
                              GS_UI_HISTORY_PAGE;

    page = malloc(sizeof *page * GS_UI_HISTORY_PAGE);
    if (page == NULL)
        return;
    n = gs_session_timeline(page, state->history_skip, GS_UI_HISTORY_PAGE);
    state->history_older = total - state->history_skip - n;
    if (state->history_older < 0)
        state->history_older = 0;

    /* The prompt a run is still answering has nothing to show yet, and
     * saying no answer came back while the model is thinking would be
     * false. The header says what the run is doing instead. */
    if (gs_harness_running()) {
        static gs_harness_state_t run;

        gs_harness_read(&run);
        running = run.turn_id;
    }

    if (state->history_older > 0) {
        char words[96];

        snprintf(words, sizeof words, "Show %d earlier prompt%s",
                 state->history_older,
                 state->history_older == 1 ? "" : "s");
        add_line(state, GS_UI_LINE_EARLIER, 0, words, 0, 0);
    }

    for (i = 0; i < n; i++) {
        int line;

        add_line(state, GS_UI_LINE_SAID, 1, page[i].prompt, page[i].asked_at,
                 page[i].id);

        if (page[i].shown_answer > 0 &&
            gs_session_shown(page[i].id, &shown) == GS_OK) {
            line = add_line(state, GS_UI_LINE_SAID, 0, shown.text,
                            shown.sampled_at, page[i].id);
            if (line < 0)
                break;
            /* A reply that filled the whole of the reading was longer than
             * the panel holds. The end is said out loud rather than left
             * looking like the last sentence of the answer. */
            if (strlen(shown.text) + 1 >= sizeof shown.text) {
                const char *mark = "\n\n[longer than the panel holds]";
                char *text = state->history[line];
                size_t room = GS_UI_HISTORY_TEXT - 1 - strlen(mark);

                if (strlen(text) > room)
                    text[room] = '\0';
                strcat(text, mark);
            }
            state->record_answer[line] = shown.id;
            gs_str_copy(state->record_by[line], sizeof state->record_by[0],
                        shown.model);
            continue;
        }

        if (page[i].id == running)
            continue;

        /* No answer is shown for this prompt, and the panel says so in a
         * line of its own, carrying the record that explains why. */
        add_line(state, GS_UI_LINE_SAID, 0,
                 gs_session_answer_count(page[i].id, 1) > 0
                     ? "Every answer failed the checking, so none is shown."
                     : "No answer came back for this prompt.",
                 page[i].asked_at, page[i].id);
    }
    free(page);

    if (state->history_skip > 0)
        add_line(state, GS_UI_LINE_NEWER, 0,
                 "Show newer prompts", 0, 0);
}

void gs_ui_chat_resume(gs_ui_state_t *state)
{
    gs_session_t recent[1];

    if (state == NULL || state->session_id != 0)
        return;
    if (gs_session_open() != GS_OK)
        return;

    /* Where a new prompt goes. The conversation this window wrote down as
     * its own comes first, and the newest is only a fallback, since a run
     * from the command line makes a conversation of its own. The history
     * shown is every conversation either way. */
    {
        char number[32] = {0};
        long long own = 0;

        if (gs_store_setting_get(GS_UI_KEY_CHAT, number, sizeof number)
                == GS_OK)
            own = atoll(number);
        if (own > 0 && gs_session_exists(own))
            state->session_id = own;
    }
    if (state->session_id == 0 && gs_session_list(recent, 1) == 1)
        state->session_id = recent[0].id;

    state->history_skip = 0;
    state->history_back = 0;
    gs_ui_chat_reload(state);
    gs_log_info("ui: %d line%s of history, new prompts go to conversation "
                "%lld", state->history_count,
                state->history_count == 1 ? "" : "s", state->session_id);
}
