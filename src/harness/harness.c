/* harness.c
 *
 * The loop. One model, one answer, written down, then the next. When all
 * have answered the checking runs over the lot and the counts pick which
 * is shown.
 *
 * The thread writes into a record the panel reads every frame, guarded by
 * a lock, since a half written line reaching the screen would read as
 * nonsense.
 */
#include "harness.h"
#include "harness_internal.h"
#include "gravestone.h"
#include "log.h"
#include "provider.h"
#include "session.h"
#include "str.h"
#include "verify.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static struct {
    pthread_t       thread;
    pthread_mutex_t lock;
    atomic_int      done;
    int             running;

    long long session_id;
    long long turn_id;
    char      prompt[4096];
    char      names[GS_HARNESS_MODELS][GS_PROVIDER_MODEL];
    int       count;
    char      corpus[512];
    gs_harness_history_t history;   /* what was said before, oldest first */

    gs_harness_state_t state;
} run;

static long long now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Adds one line to the record a person opens to see what happened. */
static void record(const char *line)
{
    size_t at;
    size_t len = strlen(line);

    pthread_mutex_lock(&run.lock);
    at = strlen(run.state.log);
    if (at + len + 2 < sizeof run.state.log) {
        memcpy(run.state.log + at, line, len);
        run.state.log[at + len] = '\n';
        run.state.log[at + len + 1] = '\0';
    }
    pthread_mutex_unlock(&run.lock);
}

static void say(const char *shape, ...)
{
    char line[512];
    va_list args;

    va_start(args, shape);
    vsnprintf(line, sizeof line, shape, args);
    va_end(args);
    record(line);
}

/* Writes a whole answer into the record, indented under the model that
 * gave it, so the note shows what was said rather than only how long it
 * took. Long answers are cut, since the record holds every model of the
 * run and one talkative model would fill it. */
static void record_words(const char *text, size_t most)
{
    char line[256];
    size_t at = 0;
    size_t taken = 0;

    if (text == NULL || text[0] == '\0') {
        record("    (nothing)");
        return;
    }

    while (text[at] != '\0' && taken < most) {
        size_t out = 0;

        line[out++] = ' ';
        line[out++] = ' ';
        line[out++] = ' ';
        line[out++] = ' ';
        while (text[at] != '\0' && text[at] != '\n' &&
               out + 1 < sizeof line && taken < most) {
            line[out++] = text[at++];
            taken++;
        }
        line[out] = '\0';
        record(line);
        /* A line ends either at a newline, which is stepped over, or at
         * the width of the record, which carries on where it stopped. */
        if (text[at] == '\n')
            at++;
    }
    if (text[at] != '\0')
        record("    ...");
}

void gs_harness_corpus(const char *directory)
{
    if (directory == NULL)
        run.corpus[0] = '\0';
    else
        gs_str_copy(run.corpus, sizeof run.corpus, directory);
}

/* Asks one model and writes down what it said. */
static void ask_one(int which)
{
    char system[1024];
    gs_provider_ask_t ask;
    gs_provider_note_t note;
    static char answer[65536];
    static char working[65536];
    long long started;
    long long answer_id = 0;
    int rc;

    pthread_mutex_lock(&run.lock);
    run.state.stage = GS_HARNESS_ASKING;
    gs_str_copy(run.state.now, sizeof run.state.now, run.names[which]);
    pthread_mutex_unlock(&run.lock);

    say("asking %s", run.names[which]);

    memset(&ask, 0, sizeof ask);
    memset(&note, 0, sizeof note);
    ask.model = run.names[which];
    /* Every model is told what it is part of, so whichever one answers
     * speaks as the Gravestone agent rather than as the product its
     * makers trained it to announce. It is told which model it is as
     * well, so asking what is underneath gets a true answer. */
    snprintf(system, sizeof system, GS_HARNESS_SYSTEM, run.names[which],
             run.names[which]);
    ask.system = system;
    ask.history = run.history.turns;
    ask.history_count = run.history.count;
    ask.prompt = run.prompt;
    /* Every model is asked the same way, so the answers differ by the
     * model rather than by how it was asked. A temperature of nought
     * gives the same answer every time, which is what a first pass
     * wants. */
    ask.temperature = 0.0;
    ask.seed = 1 + which;
    ask.limit = GS_HARNESS_FIRST_LIMIT;

    started = now_ms();
    answer[0] = '\0';
    working[0] = '\0';
    rc = gs_provider_ask_full(&ask, answer, sizeof answer, working,
                              sizeof working, &note);

    /* A model that ran out of room is asked again with more of it,
     * whether or not it had started its answer. Only a reply of nothing
     * used to count, so a reply cut four words in was taken as finished
     * and shown ending in the middle of a sentence. An answer cut short
     * is not a whole answer. */
    if (rc == GS_OK && note.stop == GS_PROVIDER_STOP_LIMIT) {
        if (answer[0] == '\0')
            say("  %s spent all %d of its allowance on working out, "
                "asking again with %d",
                run.names[which], ask.limit, GS_HARNESS_SECOND_LIMIT);
        else
            say("  %s ran out of its allowance of %d part way through its "
                "answer, asking again with %d",
                run.names[which], ask.limit, GS_HARNESS_SECOND_LIMIT);
        ask.limit = GS_HARNESS_SECOND_LIMIT;
        answer[0] = '\0';
        working[0] = '\0';
        rc = gs_provider_ask_full(&ask, answer, sizeof answer, working,
                                  sizeof working, &note);
    }

    if (rc != GS_OK) {
        say("  %s did not answer", run.names[which]);
        gs_session_add_answer(run.turn_id, run.names[which], "", &answer_id);
        pthread_mutex_lock(&run.lock);
        run.state.empty++;
        pthread_mutex_unlock(&run.lock);
        return;
    }

    say("  %s answered in %lld seconds, %d characters",
        run.names[which], (now_ms() - started) / 1000, (int)strlen(answer));
    if (note.working_len > 0)
        say("    after %d characters of working out, kept in the record "
            "behind the answer", note.working_len);
    if (answer[0] == '\0' && note.stop == GS_PROVIDER_STOP_LIMIT)
        say("    the allowance ran out before it began its answer");

    /* Still cut after the second allowance. It is kept, because a person
     * who asked one model would otherwise see nothing at all, and it says
     * where it stops so nobody takes the last sentence for the end. */
    if (answer[0] != '\0' && note.stop == GS_PROVIDER_STOP_LIMIT) {
        size_t len = strlen(answer);
        const char *mark = "\n\n[cut short: the model reached its limit]";

        say("    still cut short after %d tokens, and marked as such",
            ask.limit);
        if (len + strlen(mark) + 1 < sizeof answer)
            memcpy(answer + len, mark, strlen(mark) + 1);
    }
    record_words(answer, 1500);

    if (gs_session_add_answer(run.turn_id, run.names[which], answer,
                              &answer_id) != GS_OK) {
        say("  the answer could not be written down");
        return;
    }
    /* What it worked out is kept beside the answer, so the record a
     * person opens shows the reasoning rather than a count of it. */
    if (working[0] != '\0' &&
        gs_session_set_working(answer_id, working) != GS_OK)
        say("  the working out could not be written down");

    pthread_mutex_lock(&run.lock);
    run.state.answered++;
    if (answer[0] == '\0')
        run.state.empty++;
    pthread_mutex_unlock(&run.lock);
}

/* Checks every answer this turn collected and lets the counts pick. */
static void check_all(void)
{
    gs_session_answer_t answers[GS_HARNESS_MODELS];
    int n;
    int i;

    pthread_mutex_lock(&run.lock);
    run.state.stage = GS_HARNESS_CHECKING;
    run.state.now[0] = '\0';
    pthread_mutex_unlock(&run.lock);

    if (run.corpus[0] != '\0')
        gs_verify_corpus(run.corpus);

    say("checking %d answer%s", run.state.answered,
        run.state.answered == 1 ? "" : "s");

    n = gs_session_answers(run.turn_id, answers, GS_HARNESS_MODELS);
    for (i = 0; i < n; i++) {
        gs_verify_result_t got;
        gs_session_verdict_t verdict;
        int score;

        gs_verify_answer(answers[i].text, &got);
        score = gs_verify_score(&got);

        switch (got.outcome) {
        case GS_VERIFY_EMPTY:     verdict = GS_SESSION_EMPTY; break;
        case GS_VERIFY_UNCITED:
        case GS_VERIFY_MISQUOTED: verdict = GS_SESSION_FAILED_CHECK; break;
        default:                  verdict = GS_SESSION_PASSED; break;
        }

        gs_session_judge(answers[i].id, verdict, score);
        say("  %s: %s, %d sentence%s, %d of %d quotations found",
            answers[i].model, gs_verify_outcome_name(got.outcome),
            got.sentences, got.sentences == 1 ? "" : "s",
            got.verified, got.quotations);
        if (got.note[0] != '\0')
            record(got.note);

        if (verdict == GS_SESSION_PASSED) {
            pthread_mutex_lock(&run.lock);
            run.state.passed++;
            pthread_mutex_unlock(&run.lock);
        }
    }
    gs_verify_release();
}

static void *worker(void *unused)
{
    long long winner = 0;
    int i;

    (void)unused;

    say("one question, %d model%s, one at a time", run.count,
        run.count == 1 ? "" : "s");
    gs_harness_history_load(run.session_id, run.turn_id, GS_HARNESS_SYSTEM,
                            run.prompt, &run.history);
    say("%d earlier exchange%s sent with it, %d left out for room%s",
        run.history.count, run.history.count == 1 ? "" : "s",
        run.history.left_out, run.history.cut ? ", the newest cut" : "");

    for (i = 0; i < run.count; i++)
        ask_one(i);

    check_all();

    /* The counts pick the winner, in the order the design gives. Most
     * quotations found, fewest overlap failures, shortest answer, then
     * the earliest sample so ties break the same way every run. */
    if (gs_session_pick(run.turn_id, &winner) == GS_OK) {
        gs_session_answer_t shown;

        /* Picking already marks the turn, so reading it back names the
         * model whose words the panel is about to show. */
        if (gs_session_shown(run.turn_id, &shown) == GS_OK)
            say("the answer shown is %s, counting %d",
                shown.model, shown.score);
        else
            say("the answer shown is the one that counted highest");
        pthread_mutex_lock(&run.lock);
        run.state.shown = winner;
        run.state.stage = GS_HARNESS_DONE;
        pthread_mutex_unlock(&run.lock);
    } else {
        say("nothing survived the checking, so no answer is shown");
        pthread_mutex_lock(&run.lock);
        run.state.stage = GS_HARNESS_FAILED;
        pthread_mutex_unlock(&run.lock);
    }

    /* The record is written by the run that made it, so a run started
     * from the command line keeps one, and so does a run still going when
     * the window closed. Only the window used to write it, once it saw the
     * run finish. */
    if (run.turn_id > 0) {
        static char kept[GS_HARNESS_LOG];

        /* Copied out under the lock the reader takes, so the window
         * reading the log at the same moment never sees half a line. */
        pthread_mutex_lock(&run.lock);
        memcpy(kept, run.state.log, sizeof kept);
        pthread_mutex_unlock(&run.lock);
        kept[sizeof kept - 1] = '\0';
        if (gs_session_note(run.turn_id, kept) != GS_OK)
            gs_log_warn("harness: the record of the run could not be "
                        "written");
    }

    atomic_store(&run.done, 1);
    return NULL;
}

int gs_harness_begin(long long session_id, long long turn_id,
                     const char *prompt,
                     const char names[][GS_PROVIDER_MODEL], int count)
{
    int i;

    if (run.running)
        return GS_ERR;
    if (prompt == NULL || prompt[0] == '\0' || names == NULL)
        return GS_ERR_ARG;
    if (count <= 0 || count > GS_HARNESS_MODELS)
        return GS_ERR_ARG;
    if (session_id <= 0 || turn_id <= 0)
        return GS_ERR_ARG;
    /* Nothing is started from here. A caller wanting a server brought up
     * asks for that outright, because this call is reached from tests
     * and from the interface, and a library entry point that starts a
     * daemon starts one every time anybody checks what it does with bad
     * arguments. */
    if (!gs_provider_available()) {
        gs_log_warn("harness: no inference server is answering");
        return GS_ERR;
    }

    pthread_mutex_init(&run.lock, NULL);
    memset(&run.state, 0, sizeof run.state);
    run.session_id = session_id;
    run.turn_id = turn_id;
    run.state.turn_id = turn_id;
    run.state.models = count;
    gs_str_copy(run.prompt, sizeof run.prompt, prompt);
    for (i = 0; i < count; i++)
        gs_str_copy(run.names[i], GS_PROVIDER_MODEL, names[i]);
    run.count = count;

    atomic_store(&run.done, 0);
    if (pthread_create(&run.thread, NULL, worker, NULL) != 0)
        return GS_ERR;
    run.running = 1;
    return GS_OK;
}

void gs_harness_read(gs_harness_state_t *out)
{
    if (out == NULL)
        return;
    pthread_mutex_lock(&run.lock);
    *out = run.state;
    pthread_mutex_unlock(&run.lock);
}

int gs_harness_poll(void)
{
    if (!run.running)
        return 0;
    if (!atomic_load(&run.done))
        return 0;
    pthread_join(run.thread, NULL);
    run.running = 0;
    return 1;
}

void gs_harness_wait(void)
{
    if (!run.running)
        return;
    pthread_join(run.thread, NULL);
    run.running = 0;
}

int gs_harness_running(void)
{
    return run.running;
}

/* ---- what the command line shows ---- */

static int harness_init(void)
{
    return GS_OK;
}

static int harness_run(int argc, char **argv)
{
    char names[GS_PROVIDER_MAX][GS_PROVIDER_MODEL];
    gs_harness_state_t state;
    long long session = 0;
    long long turn = 0;
    /* app.c hands a module its arguments with the program name taken
     * off, so argv[0] is the word harness and the question follows it. */
    const char *prompt = argc > 1 && argv[1][0] != '\0'
                             ? argv[1] : "What is time dilation?";
    int have;
    int i;

    if (!gs_provider_available() && gs_provider_start(30) != GS_OK) {
        printf("no inference server answering, so nothing can be asked\n");
        return GS_ERR;
    }
    have = gs_provider_models(names, GS_PROVIDER_MAX);
    if (have == 0) {
        printf("the server holds no models\n");
        return GS_ERR;
    }
    if (have > GS_HARNESS_MODELS)
        have = GS_HARNESS_MODELS;

    if (gs_session_open() != GS_OK ||
        gs_session_start(NULL, &session) != GS_OK ||
        gs_session_add_turn(session, prompt, &turn) != GS_OK) {
        printf("the conversation could not be written down\n");
        return GS_ERR;
    }

    gs_harness_corpus("data/corpus");
    if (gs_harness_begin(session, turn, prompt, names, have) != GS_OK) {
        printf("the run would not start\n");
        return GS_ERR;
    }

    /* One question to each model in turn, so this waits. */
    gs_harness_wait();
    gs_harness_read(&state);

    printf("%s", state.log);
    printf("\n");
    {
        gs_session_answer_t shown;

        if (gs_session_shown(turn, &shown) == GS_OK) {
            printf("shown, from %s:\n%s\n", shown.model, shown.text);
        } else {
            printf("nothing survived the checking\n");
        }
    }
    (void)i;
    gs_session_close();
    return GS_OK;
}

static void harness_shutdown(void)
{
    gs_harness_wait();
}

const gs_module gs_harness_module = {
    "harness",
    "put one question to every model and pick what survives",
    harness_init,
    harness_run,
    harness_shutdown
};
