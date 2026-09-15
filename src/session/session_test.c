/* session_test.c
 *
 * Adversarial. A conversation has to come back exactly as it was left,
 * because a person closing the program and opening it again expects to
 * find their words where they put them. Every check here works on a
 * database in build/, so nothing of the person's own is touched.
 */
#include "session.h"
#include "gravestone.h"
#include "db.h"
#include "paths.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void fresh(void)
{
    gs_session_close();
    (void)system("rm -rf build/session-test");
    gs_paths_override("build/session-test");
}

static void test_closed(void)
{
    gs_session_t list[2];
    gs_session_turn_t turns[2];
    gs_session_answer_t answers[2];
    long long id = 0;

    printf("before the file is open\n");
    gs_session_close();
    check(gs_session_start("x", &id) == GS_ERR, "starting one is refused");
    check(gs_session_add_turn(1, "hello", &id) == GS_ERR_ARG,
          "adding a prompt is refused");
    check(gs_session_add_answer(1, "m", "a", &id) == GS_ERR_ARG,
          "adding an answer is refused");
    check(gs_session_list(list, 2) == 0, "nothing lists");
    check(gs_session_turns(1, turns, 2) == 0, "no turns come back");
    check(gs_session_answers(1, answers, 2) == 0, "no answers come back");
    check(gs_session_shown(1, answers) == GS_ERR_ARG, "nothing is shown");
    check(gs_session_forget(1) == GS_ERR_ARG, "forgetting is refused");
    gs_session_close();
    check(1, "closing twice does not crash");
}

static void test_bad_arguments(void)
{
    long long id = 0;
    gs_session_answer_t one;

    printf("arguments that make no sense\n");
    check(gs_session_add_turn(0, "hello", &id) == GS_ERR_ARG,
          "a conversation numbered zero is refused");
    check(gs_session_add_turn(-4, "hello", &id) == GS_ERR_ARG,
          "and a negative one");
    check(gs_session_add_turn(1, NULL, &id) == GS_ERR_ARG,
          "no prompt is refused");
    check(gs_session_add_turn(1, "", &id) == GS_ERR_ARG,
          "an empty prompt is refused, since it asks nothing");
    check(gs_session_add_answer(0, "m", "a", &id) == GS_ERR_ARG,
          "an answer against no turn is refused");
    check(gs_session_add_answer(1, NULL, "a", &id) == GS_ERR_ARG,
          "an answer from no model is refused");
    check(gs_session_add_answer(1, "", "a", &id) == GS_ERR_ARG,
          "nor from a model with no name");
    check(gs_session_add_answer(1, "m", NULL, &id) == GS_ERR_ARG,
          "no text at all is refused");
    check(gs_session_judge(0, GS_SESSION_PASSED, 1) == GS_ERR_ARG,
          "judging nothing is refused");
    check(gs_session_judge(1, (gs_session_verdict_t)99, 1) == GS_ERR_ARG,
          "a verdict that does not exist is refused");
    check(gs_session_judge(1, GS_SESSION_PASSED, -1) == GS_ERR_ARG,
          "a score below nothing is refused");
    check(gs_session_show(0, 1) == GS_ERR_ARG, "showing against no turn");
    check(gs_session_show(1, 0) == GS_ERR_ARG, "and showing no answer");
    check(gs_session_shown(0, &one) == GS_ERR_ARG, "reading against no turn");
    check(gs_session_shown(1, NULL) == GS_ERR_ARG, "into nowhere");
}

/* The whole point. Words written down have to come back in the order they
 * were written, after the file has been closed and opened again. */
static void test_survives_a_restart(void)
{
    long long session = 0;
    long long first = 0, second = 0, third = 0;
    gs_session_turn_t turns[8];
    int n;

    printf("a conversation outlives the program\n");
    fresh();
    check(gs_session_open() == GS_OK, "the file opened");
    check(gs_session_start(NULL, &session) == GS_OK, "a conversation started");
    check(session > 0, "and it has a number");

    check(gs_session_add_turn(session, "what is a pointer", &first) == GS_OK,
          "the first prompt went in");
    check(gs_session_add_turn(session, "and a reference", &second) == GS_OK,
          "the second");
    check(gs_session_add_turn(session, "and a slice", &third) == GS_OK,
          "the third");
    check(first != second && second != third, "each turn has its own number");

    gs_session_close();
    check(gs_session_open() == GS_OK, "the file opened again");

    n = gs_session_turns(session, turns, 8);
    check(n == 3, "all three prompts came back");
    check(turns[0].ordinal == 1 && turns[1].ordinal == 2 &&
          turns[2].ordinal == 3, "numbered in the order they were asked");
    check(strcmp(turns[0].prompt, "what is a pointer") == 0,
          "the first prompt is word for word what went in");
    check(strcmp(turns[2].prompt, "and a slice") == 0, "and so is the last");
    check(turns[0].asked_at > 0, "each one carries when it was asked");
    check(turns[0].shown_answer == 0,
          "and shows nothing yet, since nothing has answered");
}

/* The record of a run is written against the turn it belongs to, so the
 * note behind an answer still opens after the program has been shut. */
static void test_the_note(void)
{
    long long session = 0;
    long long turn = 0;
    gs_session_turn_t turns[4];
    const char *whole =
        "one question, 2 models, one at a time\n"
        "asking glm4\n  glm4 answered in 4 seconds\n";
    int n;

    printf("the record kept behind an answer\n");
    fresh();
    check(gs_session_open() == GS_OK, "the file opened");
    check(gs_session_start(NULL, &session) == GS_OK, "a conversation started");
    check(gs_session_add_turn(session, "what is time dilation", &turn)
          == GS_OK, "a prompt went in");

    n = gs_session_turns(session, turns, 4);
    check(n == 1 && turns[0].note[0] == '\0',
          "a turn starts with no record against it");

    check(gs_session_note(turn, whole) == GS_OK, "the record went in");
    gs_session_close();
    check(gs_session_open() == GS_OK, "the file opened again");

    n = gs_session_turns(session, turns, 4);
    check(n == 1, "the turn came back");
    check(strcmp(turns[0].note, whole) == 0,
          "carrying the record line for line");

    check(gs_session_note(turn, NULL) == GS_OK, "nothing clears it");
    n = gs_session_turns(session, turns, 4);
    check(n == 1 && turns[0].note[0] == '\0', "and it comes back empty");

    check(gs_session_note(0, whole) != GS_OK, "no turn, no record");
    check(gs_session_note(-1, whole) != GS_OK, "nor a turn before the first");
}

/* A conversation with no title takes its name from the first prompt, so a
 * list of conversations reads as a list of questions. */
static void test_title(void)
{
    long long a = 0, b = 0;
    long long turn = 0;
    gs_session_t list[8];
    int n;

    printf("what a conversation is called\n");
    fresh();
    gs_session_open();

    gs_session_start(NULL, &a);
    gs_session_add_turn(a, "how does a compiler decide what to inline", &turn);
    n = gs_session_list(list, 8);
    check(n == 1, "one conversation lists");
    check(strlen(list[0].title) > 0, "it took a title from the prompt");
    check(strncmp(list[0].title, "how does a compiler", 19) == 0,
          "which is the words the prompt opened with");
    check(list[0].turns == 1, "and it counts its one turn");

    gs_session_start("a name of its own", &b);
    gs_session_add_turn(b, "something else entirely", &turn);
    n = gs_session_list(list, 8);
    check(n == 2, "both conversations list");
    check(strcmp(list[0].title, "a name of its own") == 0,
          "a conversation given a name keeps it");
    check(list[0].id == b, "and the newest is first");

    /* A prompt longer than the title cuts at a word rather than mid word,
     * so the name still reads. */
    {
        char lengthy[600];
        long long c = 0;

        memset(lengthy, 0, sizeof lengthy);
        for (n = 0; n < 40; n++)
            strcat(lengthy, "elephant ");
        gs_session_start(NULL, &c);
        gs_session_add_turn(c, lengthy, &turn);
        gs_session_list(list, 8);
        check(strlen(list[0].title) < GS_SESSION_TITLE,
              "a long prompt is cut to fit");
        check(list[0].title[strlen(list[0].title) - 1] != ' ',
              "with no space left hanging on the end");
        check(strncmp(list[0].title, lengthy, strlen(list[0].title)) == 0,
              "what is kept is the opening of the prompt, character for "
              "character");
        /* The prompt carries a space exactly where the title stops, which
         * is what makes the cut fall between words rather than through
         * one, so the name still reads. */
        check(lengthy[strlen(list[0].title)] == ' ',
              "and the cut lands where the prompt has a space");
    }
}

/* Every answer is kept, so a choice can be looked at afterwards. */
static void test_answers(void)
{
    long long session = 0, turn = 0;
    long long one = 0, two = 0, three = 0;
    gs_session_answer_t got[8];
    int n;

    printf("every answer a turn collected\n");
    fresh();
    gs_session_open();
    gs_session_start("x", &session);
    gs_session_add_turn(session, "a question", &turn);

    check(gs_session_add_answer(turn, "glm4", "the first", &one) == GS_OK,
          "one model answered");
    check(gs_session_add_answer(turn, "deepseek", "the second", &two) == GS_OK,
          "and another");
    check(gs_session_add_answer(turn, "qwen", "", &three) == GS_OK,
          "and one gave back nothing");

    n = gs_session_answers(turn, got, 8);
    check(n == 3, "all three were kept");
    check(strcmp(got[0].model, "glm4") == 0, "each one names the model");
    check(strcmp(got[0].text, "the first") == 0, "and carries its words");
    check(got[0].verdict == GS_SESSION_PENDING,
          "an answer starts with nothing decided about it");
    /* An empty reply is a failure of its own rather than an absence. */
    check(got[2].verdict == GS_SESSION_EMPTY,
          "an empty reply is marked empty the moment it arrives");
    check(got[0].id != got[1].id, "each answer has its own number");
    check(got[0].sampled_at > 0, "and carries when it arrived");

    check(gs_session_answers(turn, got, 2) == 2,
          "a reader smaller than the list fills and stops");
}

/* Nothing that failed may be shown, and the same answers must give the
 * same winner every time. */
static void test_picking(void)
{
    long long session = 0, turn = 0;
    long long weak = 0, strong = 0, failed = 0, tie = 0;
    long long picked = 0;
    gs_session_answer_t shown;

    printf("choosing which answer a turn shows\n");
    fresh();
    gs_session_open();
    gs_session_start("x", &session);
    gs_session_add_turn(session, "a question", &turn);

    check(gs_session_pick(turn, &picked) == GS_ERR,
          "a turn with no answers picks nothing");

    gs_session_add_answer(turn, "a", "a longer answer that passed", &weak);
    gs_session_add_answer(turn, "b", "short and right", &strong);
    gs_session_add_answer(turn, "c", "wrong", &failed);

    check(gs_session_judge(weak, GS_SESSION_PASSED, 2) == GS_OK,
          "one answer was judged");
    check(gs_session_judge(strong, GS_SESSION_PASSED, 5) == GS_OK, "and another");
    check(gs_session_judge(failed, GS_SESSION_FAILED_TESTS, 9) == GS_OK,
          "and one failed its tests despite a high count");

    check(gs_session_pick(turn, &picked) == GS_OK, "a winner was picked");
    check(picked == strong, "the highest count among what passed");
    check(picked != failed,
          "and nothing that failed its tests, whatever it counted");

    check(gs_session_shown(turn, &shown) == GS_OK, "the turn shows one now");
    check(shown.id == strong, "and it is the one that was picked");
    check(strcmp(shown.text, "short and right") == 0, "carrying its words");

    /* Two answers on the same count break on length, then on which came
     * first, so the same answers always give the same winner. */
    gs_session_add_answer(turn, "d", "tiny", &tie);
    gs_session_judge(tie, GS_SESSION_PASSED, 5);
    check(gs_session_pick(turn, &picked) == GS_OK, "picked again");
    check(picked == tie, "a tie on count goes to the shorter answer");

    check(gs_session_show(turn, failed) == GS_OK,
          "an answer of this turn can be shown by hand");
    check(gs_session_show(turn, 999999) == GS_ERR_ARG,
          "an answer that does not exist is refused");

    /* An answer belonging to another turn would put words from one
     * question underneath a different one. */
    {
        long long other_turn = 0;
        long long other_answer = 0;

        gs_session_add_turn(session, "a different question", &other_turn);
        gs_session_add_answer(other_turn, "a", "elsewhere", &other_answer);
        check(gs_session_show(turn, other_answer) == GS_ERR_ARG,
              "an answer from another turn is refused");
    }
}

static void test_forget(void)
{
    long long session = 0, other = 0, turn = 0, answer = 0;
    gs_session_t list[8];
    gs_session_turn_t turns[8];
    gs_session_answer_t answers[8];

    printf("forgetting a conversation\n");
    fresh();
    gs_session_open();
    gs_session_start("going", &session);
    gs_session_add_turn(session, "a question", &turn);
    gs_session_add_answer(turn, "m", "an answer", &answer);
    gs_session_start("staying", &other);

    check(gs_session_list(list, 8) == 2, "two conversations exist");
    check(gs_session_forget(session) == GS_OK, "one was forgotten");
    check(gs_session_list(list, 8) == 1, "and one is left");
    check(list[0].id == other, "the right one is left");

    /* Its turns and its answers go with it, or the file fills up with
     * rows nothing can ever reach. */
    check(gs_session_turns(session, turns, 8) == 0, "its turns went with it");
    check(gs_session_answers(turn, answers, 8) == 0, "and its answers");
}

/* What a model worked out before its answer is kept beside the answer,
 * and has to come back after a restart and after a file made before the
 * column existed is opened again. */
static void test_the_working_out(void)
{
    long long session = 0, turn = 0, answer = 0;
    char out[512];

    printf("\nthe working out behind an answer\n");
    fresh();
    check(gs_session_working(1, out, sizeof out) == GS_ERR_ARG,
          "nothing is read while the file is closed");
    check(gs_session_open() == GS_OK, "the file opens");
    check(gs_session_start("w", &session) == GS_OK, "a conversation starts");
    check(gs_session_add_turn(session, "why?", &turn) == GS_OK, "a turn goes in");
    check(gs_session_add_answer(turn, "m", "because", &answer) == GS_OK,
          "an answer goes in");

    check(gs_session_working(answer, out, sizeof out) == GS_OK &&
          out[0] == '\0', "an answer starts with no working out");
    check(gs_session_set_working(answer, "first I considered x") == GS_OK,
          "the working out is written");
    check(gs_session_working(answer, out, sizeof out) == GS_OK &&
          strcmp(out, "first I considered x") == 0, "and comes back whole");

    check(gs_session_set_working(answer, "then y") == GS_OK,
          "writing again replaces it");
    check(gs_session_working(answer, out, sizeof out) == GS_OK &&
          strcmp(out, "then y") == 0, "with the newer text");

    check(gs_session_set_working(0, "x") == GS_ERR_ARG, "no answer is refused");
    check(gs_session_set_working(answer, NULL) == GS_ERR_ARG,
          "and no text");
    check(gs_session_working(answer + 99, out, sizeof out) == GS_ERR,
          "an answer that is not there reads as missing");
    check(out[0] == '\0', "leaving the buffer empty");
    check(gs_session_working(answer, NULL, 8) == GS_ERR_ARG,
          "nowhere to write is refused");

    /* A long one is cut to fit rather than overrunning. */
    check(gs_session_working(answer, out, 4) == GS_OK && strlen(out) == 3,
          "a small buffer is filled and terminated");

    gs_session_close();
    check(gs_session_open() == GS_OK, "the file opens again");
    check(gs_session_working(answer, out, sizeof out) == GS_OK &&
          strcmp(out, "then y") == 0, "and the working out survived");
    gs_session_close();

    /* A file written before the column existed. The table is made by
     * hand without it, and opening has to add it rather than fail. */
    (void)system("rm -rf build/session-old");
    gs_paths_override("build/session-old");
    {
        char path[512];
        gs_db_t *raw;

        gs_paths_db_file(path, sizeof path);
        (void)system("mkdir -p build/session-old");
        raw = gs_db_open(path);
        check(raw != NULL, "an old style file is made");
        if (raw != NULL) {
            gs_db_exec(raw,
                "CREATE TABLE answer (id INTEGER PRIMARY KEY, turn_id INTEGER"
                " NOT NULL, sampled_at INTEGER NOT NULL, verdict INTEGER NOT"
                " NULL DEFAULT 0, score INTEGER NOT NULL DEFAULT 0, model TEXT"
                " NOT NULL, text TEXT NOT NULL);");
            gs_db_exec(raw,
                "INSERT INTO answer (turn_id, sampled_at, model, text)"
                " VALUES (1, 1, 'old', 'kept');");
            gs_db_close(raw);
        }
    }
    check(gs_session_open() == GS_OK, "and opens under the new build");
    check(gs_session_working(1, out, sizeof out) == GS_OK && out[0] == '\0',
          "an answer from before reads as having no working out");
    check(gs_session_set_working(1, "added later") == GS_OK,
          "and can be given one now");
    gs_session_close();
    gs_paths_override(NULL);
}

/* The window writes a prompt on its own thread while a run writes answers
 * on another, through the one handle both share. A number read back after
 * an insert has to be the number of that insert, whichever thread wrote
 * something in between. */
static long long race_session;
static long long race_turn;
static int race_wrong_turns;
static int race_wrong_answers;

static void *race_turns(void *unused)
{
    int i;

    (void)unused;
    for (i = 0; i < 400; i++) {
        char words[64];
        long long id = 0;
        gs_session_turn_t back[1];

        snprintf(words, sizeof words, "turn from the window %d", i);
        if (gs_session_add_turn(race_session, words, &id) != GS_OK) {
            race_wrong_turns++;
            continue;
        }
        (void)back;
        {
            char note[128];

            /* The number handed back names the row just written, which
             * the note written against it proves. */
            snprintf(note, sizeof note, "%s", words);
            gs_session_note(id, note);
            if (gs_session_turn_note(id, note, sizeof note) != GS_OK ||
                strcmp(note, words) != 0)
                race_wrong_turns++;
        }
    }
    return NULL;
}

static void *race_answers(void *unused)
{
    int i;

    (void)unused;
    for (i = 0; i < 400; i++) {
        char words[64];
        long long id = 0;

        snprintf(words, sizeof words, "answer from the run %d", i);
        if (gs_session_add_answer(race_turn, "m", words, &id) != GS_OK) {
            race_wrong_answers++;
            continue;
        }
        gs_session_set_working(id, words);
        {
            char back[128];

            if (gs_session_working(id, back, sizeof back) != GS_OK ||
                strcmp(back, words) != 0)
                race_wrong_answers++;
        }
    }
    return NULL;
}

static void test_two_threads_writing(void)
{
    pthread_t a, b;

    printf("\ntwo threads writing through one handle\n");
    fresh();
    check(gs_session_open() == GS_OK, "the file opens");
    gs_session_start("race", &race_session);
    gs_session_add_turn(race_session, "the prompt a run answers", &race_turn);
    race_wrong_turns = 0;
    race_wrong_answers = 0;

    pthread_create(&a, NULL, race_turns, NULL);
    pthread_create(&b, NULL, race_answers, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);

    printf("        %d wrong prompt numbers, %d wrong answer numbers, "
           "out of 400 each\n", race_wrong_turns, race_wrong_answers);
    check(race_wrong_turns == 0,
          "every prompt number handed back names that prompt");
    check(race_wrong_answers == 0,
          "every answer number handed back names that answer");
    gs_session_close();
}

int main(void)
{
    printf("session\n\n");

    test_closed();
    fresh();
    gs_session_open();
    test_bad_arguments();
    test_survives_a_restart();
    test_the_note();
    test_the_working_out();
    test_two_threads_writing();
    test_title();
    test_answers();
    test_picking();
    test_forget();

    gs_session_close();
    gs_paths_override(NULL);
    (void)system("rm -rf build/session-test");
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
