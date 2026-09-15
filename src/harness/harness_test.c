/* harness_test.c
 *
 * Adversarial. The loop must refuse a run it cannot honour rather than
 * start one and fail halfway, since a half finished run leaves answers
 * against a turn with no way to tell they are incomplete.
 *
 * A real run needs an inference server, so that part is reported as a
 * skip when nothing is answering. Everything else runs anywhere.
 */
#include "harness.h"
#include "harness_internal.h"
#include "gravestone.h"
#include "paths.h"
#include "provider.h"
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;
static int skipped;

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

static void test_bad_runs(void)
{
    char names[2][GS_PROVIDER_MODEL];
    gs_harness_state_t state;

    printf("runs that cannot be honoured\n");
    strcpy(names[0], "a");
    strcpy(names[1], "b");

    check(gs_harness_running() == 0, "nothing is running to start with");
    check(gs_harness_begin(1, 1, NULL, names, 2) == GS_ERR_ARG,
          "no prompt is refused");
    check(gs_harness_begin(1, 1, "", names, 2) == GS_ERR_ARG,
          "an empty prompt is refused");
    check(gs_harness_begin(1, 1, "x", NULL, 2) == GS_ERR_ARG,
          "no models are refused");
    check(gs_harness_begin(1, 1, "x", names, 0) == GS_ERR_ARG,
          "a run of no models is refused");
    check(gs_harness_begin(1, 1, "x", names, -3) == GS_ERR_ARG,
          "and a negative count");
    check(gs_harness_begin(1, 1, "x", names, GS_HARNESS_MODELS + 1)
          == GS_ERR_ARG, "more models than a run may hold is refused");
    check(gs_harness_begin(0, 1, "x", names, 1) == GS_ERR_ARG,
          "a conversation numbered zero is refused");
    check(gs_harness_begin(1, 0, "x", names, 1) == GS_ERR_ARG,
          "and a turn numbered zero");
    check(gs_harness_running() == 0, "and none of them started anything");

    gs_harness_read(&state);
    check(state.stage == GS_HARNESS_IDLE, "the record still reads idle");
    check(state.answered == 0, "with nothing answered");
    gs_harness_read(NULL);
    check(1, "reading into nowhere does not crash");
    check(gs_harness_poll() == 0, "polling an idle run reports nothing");
    gs_harness_wait();
    check(1, "waiting on an idle run returns at once");
}

/* The whole point. Every model is asked in turn, every answer is written
 * down against the turn, and the counts pick which is shown.
 *
 * Models that do not exist are used rather than real ones, because a real
 * model on a machine this size takes minutes for a single word and a test
 * that waited for one would never finish. The loop does the same work
 * either way, since a refusal is recorded exactly as an answer is. */
static void test_the_loop(void)
{
    char names[3][GS_PROVIDER_MODEL];
    gs_harness_state_t state;
    gs_session_answer_t answers[GS_HARNESS_MODELS];
    long long session = 0;
    long long turn = 0;
    int n;

    printf("the loop, one model at a time\n");
    if (!gs_provider_available()) {
        printf("  skip  nothing answering, so no model can be asked\n");
        skipped++;
        strcpy(names[0], "zz-no-such-model-xyzzy:1b");
        check(gs_harness_begin(1, 1, "x", names, 1) == GS_ERR,
              "and a run is refused rather than started");
        return;
    }

    strcpy(names[0], "zz-no-such-model-one:1b");
    strcpy(names[1], "zz-no-such-model-two:1b");
    strcpy(names[2], "zz-no-such-model-three:1b");

    (void)system("rm -rf build/harness-test");
    gs_paths_override("build/harness-test");
    gs_session_close();
    check(gs_session_open() == GS_OK, "a place to write it down");
    gs_session_start("harness", &session);
    check(gs_session_add_turn(session, "What is time dilation?",
                              &turn) == GS_OK, "the question is on record");

    check(gs_harness_begin(session, turn, "What is time dilation?",
                           names, 3) == GS_OK, "the run started");
    check(gs_harness_running() == 1, "and it is going");
    check(gs_harness_begin(session, turn, "x", names, 3) == GS_ERR,
          "a second run while one is going is refused");

    gs_harness_wait();
    check(gs_harness_running() == 0, "and it finished");
    check(gs_harness_poll() == 0, "with nothing left to collect");

    gs_harness_read(&state);
    check(state.models == 3, "every model was counted");
    check(state.log[0] != '\0', "and a record was kept");
    check(strstr(state.log, "one question, 3 models") != NULL,
          "the record opens with what was asked of how many");
    check(strstr(state.log, names[0]) != NULL, "and names the first");
    check(strstr(state.log, names[2]) != NULL, "and the last");
    check(strstr(state.log, "did not answer") != NULL,
          "saying plainly which gave nothing back");
    check(strstr(state.log, "checking") != NULL,
          "and saying when the checking began");

    /* One row per model, whatever each said, so a run can be looked at
     * afterwards and every model accounted for. */
    n = gs_session_answers(turn, answers, GS_HARNESS_MODELS);
    check(n == 3, "one answer was written down for each model asked");

    /* The run writes its own record against the prompt, so a run with no
     * window watching it keeps one. Only the window used to write it. */
    {
        static char kept[GS_HARNESS_LOG];

        check(gs_session_turn_note(turn, kept, sizeof kept) == GS_OK,
              "the record of the run is in the file");
        check(strstr(kept, "one question, 3 models") != NULL,
              "written by the run itself, with no window to write it");
        check(strcmp(kept, state.log) == 0,
              "and it is the whole record, word for word");
    }
    check(answers[0].verdict == GS_SESSION_EMPTY,
          "a model that would not answer is marked as giving nothing");
    check(strcmp(answers[0].model, names[0]) == 0,
          "and each row names the model it came from");
    check(answers[1].id != answers[0].id, "each row has its own number");

    /* Nothing survived, so nothing is shown. Showing an answer that
     * failed would put words on screen the checking rejected. */
    check(state.stage == GS_HARNESS_FAILED,
          "a run where nothing survived says so");
    check(state.shown == 0, "and shows no answer");
    {
        gs_session_answer_t shown;

        check(gs_session_shown(turn, &shown) != GS_OK,
              "the turn shows nothing at all");
    }

    gs_session_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/harness-test");
}

/* Writes one exchange down the way a finished run leaves it. */
static long long exchange(long long session, const char *asked,
                          const char *answered)
{
    long long turn = 0;
    long long answer = 0;

    gs_session_add_turn(session, asked, &turn);
    if (answered != NULL) {
        gs_session_add_answer(turn, "m", answered, &answer);
        gs_session_show(turn, answer);
    }
    return turn;
}

static void test_what_was_said_before(void)
{
    static char big[7000];
    gs_harness_history_t history;
    long long session = 0;
    long long other = 0;
    long long now;
    size_t i;

    printf("what was said before goes with the question\n");

    (void)system("rm -rf build/harness-history-test");
    gs_paths_override("build/harness-history-test");
    gs_session_close();
    check(gs_session_open() == GS_OK, "a place to write it down");
    gs_session_start("history", &session);
    gs_session_start("another", &other);

    now = exchange(session, "first question", NULL);
    check(gs_harness_history_load(session, now, "sys", "first question",
                                  &history) == 0 && history.count == 0,
          "the first question of a conversation goes alone");

    exchange(session, "My name is Waqas.", "Hello Waqas.");
    exchange(session, "a question nobody answered", NULL);
    exchange(session, "silence", "");
    exchange(other, "a question in another conversation", "its answer");
    exchange(session, "I write C.", "Noted.");
    now = exchange(session, "What is my name?", NULL);
    exchange(session, "asked after", "answered after");

    check(gs_harness_history_load(session, now, "sys", "What is my name?",
                                  &history) == 2,
          "two exchanges with answers came before it");
    check(history.count == 2 && strcmp(history.turns[0].prompt,
                                       "My name is Waqas.") == 0 &&
          strcmp(history.turns[0].answer, "Hello Waqas.") == 0,
          "the oldest first");
    check(strcmp(history.turns[1].prompt, "I write C.") == 0 &&
          strcmp(history.turns[1].answer, "Noted.") == 0,
          "the newest last, just ahead of the question");
    check(history.left_out == 0 && history.cut == 0,
          "with nothing left out and nothing cut");

    /* A long newest answer is cut to the room, and the one before it,
     * which no longer fits, is left out. */
    for (i = 0; i + 1 < sizeof big; i++)
        big[i] = (char)('a' + i % 26);
    big[sizeof big - 1] = '\0';
    exchange(session, "write a lot", big);
    now = exchange(session, "and now?", NULL);
    check(gs_harness_history_load(session, now, "sys", "and now?",
                                  &history) == 1 && history.cut == 1,
          "an answer longer than the room is cut to fit");
    check(strcmp(history.turns[0].prompt, "write a lot") == 0 &&
          strstr(history.turns[0].answer, "left out") != NULL,
          "and says where it was cut");
    check(strlen(history.turns[0].prompt) + strlen(history.turns[0].answer) +
          strlen("sys") + strlen("and now?") <= GS_HARNESS_ROOM_BYTES,
          "everything sent fits the room");
    check(history.left_out >= 3, "the older exchanges are left out");

    check(gs_harness_history_load(session, now, "sys", big, &history) == 0,
          "a question filling the room alone goes with nothing");
    check(gs_harness_history_load(0, now, "sys", "x", &history) == 0 &&
          gs_harness_history_load(session, now, NULL, NULL, NULL) == 0,
          "no conversation and nowhere to write are both taken calmly");

    gs_session_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/harness-history-test");
}

static void test_corpus_setting(void)
{
    printf("where the checking looks\n");
    gs_harness_corpus("data/corpus");
    check(1, "a place can be named");
    gs_harness_corpus(NULL);
    check(1, "and taken away again");
}

int main(void)
{
    printf("harness\n\n");

    test_bad_runs();
    test_corpus_setting();
    test_what_was_said_before();
    test_the_loop();

    gs_harness_wait();
    printf("\n%d checks, %d failures, %d skipped\n", checks, failures, skipped);
    return failures == 0 ? 0 : 1;
}
