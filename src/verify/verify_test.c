/* verify_test.c
 *
 * Adversarial. An answer that reaches the top has to have earned it, so
 * every check here tries to smuggle a wrong answer past rather than
 * confirm an easy one. Documents are written into build/ and taken away
 * again, so nothing of the person's own is touched.
 */
#include "verify.h"
#include "verify_internal.h"
#include "gravestone.h"

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

static void write_corpus(void)
{
    FILE *f;

    (void)system("rm -rf build/corpus-test && mkdir -p build/corpus-test");
    f = fopen("build/corpus-test/clocks.txt", "w");
    if (f != NULL) {
        fputs("A clock that is moving ticks more slowly than a clock at "
              "rest. The effect grows with speed and becomes large near "
              "the speed of light. Gravity slows a clock in the same "
              "way.\n", f);
        fclose(f);
    }
    f = fopen("build/corpus-test/lengths.txt", "w");
    if (f != NULL) {
        fputs("A moving object is measured as shorter along the direction "
              "of its travel. This shortening is called length "
              "contraction.\n", f);
        fclose(f);
    }
}

static void test_sentences(void)
{
    char out[256];

    printf("taking an answer apart\n");
    check(gs_verify_sentence("One. Two. Three.", 0, out, sizeof out) == 1 &&
          strcmp(out, "One.") == 0, "the first sentence comes out whole");
    check(gs_verify_sentence("One. Two. Three.", 2, out, sizeof out) == 1 &&
          strcmp(out, "Three.") == 0, "and so does the last");
    check(gs_verify_sentence("One. Two.", 2, out, sizeof out) == 0,
          "and there is no third");

    check(gs_verify_sentence("Ask why? Then stop!", 0, out, sizeof out) == 1 &&
          strcmp(out, "Ask why?") == 0, "a question ends a sentence");
    check(gs_verify_sentence("Ask why? Then stop!", 1, out, sizeof out) == 1 &&
          strcmp(out, "Then stop!") == 0, "and so does an exclamation");

    /* A stop inside a number ends nothing, or every figure would split a
     * sentence in two and the counting would be nonsense. */
    check(gs_verify_sentence("It grew 3.5 times over. Then it stopped.", 0,
                             out, sizeof out) == 1 &&
          strcmp(out, "It grew 3.5 times over.") == 0,
          "a stop inside a number does not end a sentence");

    check(gs_verify_sentence("A sentence with no stop", 0, out,
                             sizeof out) == 1 &&
          strcmp(out, "A sentence with no stop") == 0,
          "an answer ending without a stop still gives its last sentence");
    check(gs_verify_sentence("", 0, out, sizeof out) == 0, "nothing gives none");
    check(gs_verify_sentence(NULL, 0, out, sizeof out) == 0, "nor does no text");
    check(gs_verify_sentence("x.", -1, out, sizeof out) == 0,
          "a sentence before the first is refused");
    check(gs_verify_sentence("x.", 0, NULL, 10) == 0, "nowhere to write");
    check(gs_verify_sentence("x.", 0, out, 0) == 0, "and no room");
}

static void test_markers_and_quotations(void)
{
    char out[256];

    printf("what a sentence names and what it quotes\n");
    check(gs_verify_marker("A claim [clocks].", out, sizeof out) == 1 &&
          strcmp(out, "clocks") == 0, "a marker is read");
    check(gs_verify_marker("No marker here.", out, sizeof out) == 0,
          "a sentence without one says so");
    check(gs_verify_marker("Empty [] marker.", out, sizeof out) == 0,
          "an empty marker names nothing");
    /* A marker follows the words it belongs to, so the last pair wins. */
    check(gs_verify_marker("[first] and then [second].", out,
                           sizeof out) == 1 &&
          strcmp(out, "second") == 0, "the last pair is the marker");
    check(gs_verify_marker("An open [ bracket.", out, sizeof out) == 0,
          "a bracket never closed names nothing");

    check(gs_verify_quotation("It says \"a clock\" here.", out,
                              sizeof out) == 1 &&
          strcmp(out, "a clock") == 0, "a quotation is read");
    check(gs_verify_quotation("No quotation here.", out, sizeof out) == 0,
          "a sentence without one says so");
    check(gs_verify_quotation("An empty \"\" quotation.", out,
                              sizeof out) == 0, "an empty one quotes nothing");
    check(gs_verify_quotation("One \"quote\" only.", out, sizeof out) == 1 &&
          strcmp(out, "quote") == 0, "the first pair is the quotation");
    check(gs_verify_quotation("An unclosed \"quotation.", out,
                              sizeof out) == 0,
          "a quotation never closed quotes nothing");
}

/* The whole point. An answer that cites nothing, or quotes what no
 * document holds, must never reach the top. */
static void test_checking(void)
{
    gs_verify_result_t got;

    printf("checking an answer against the documents\n");
    write_corpus();
    check(gs_verify_corpus("build/corpus-test") == GS_OK,
          "the documents were read");
    check(gs_verify_ready() == 1, "and there is something to check against");

    /* Every sentence cites, every quotation is real. */
    gs_verify_answer("A clock that is moving ticks more slowly "
                     "\"ticks more slowly\" [clocks]. "
                     "A moving object is measured as shorter "
                     "\"measured as shorter\" [lengths].", &got);
    check(got.outcome == GS_VERIFY_PASSED, "a cited, quoted answer passes");
    check(got.sentences == 2, "both sentences were counted");
    check(got.cited == 2, "both named a source");
    check(got.verified == 2, "both quotations were found");
    check(got.overlap_failed == 0, "and neither claimed what its source lacks");
    check(gs_verify_score(&got) == 2, "its score is the quotations found");

    /* A sentence with no marker puts the whole answer out before any
     * ranking happens. */
    gs_verify_answer("A clock ticks more slowly \"ticks more slowly\" "
                     "[clocks]. And gravity is made of cheese.", &got);
    check(got.outcome == GS_VERIFY_UNCITED,
          "one uncited sentence puts the whole answer out");
    check(gs_verify_score(&got) == 0,
          "and it scores nothing, whatever else it quoted");
    check(strstr(got.note, "names no source") != NULL,
          "the record says which sentence");

    /* A quotation no document holds fails, however well it reads. */
    gs_verify_answer("A clock \"ticks in reverse when observed\" [clocks].",
                     &got);
    check(got.outcome == GS_VERIFY_MISQUOTED,
          "words no document holds fail");
    check(got.verified == 0, "with nothing verified");
    check(gs_verify_score(&got) == 0, "and a score of nothing");

    /* A sentence naming a real document while claiming something it never
     * mentions is counted against the answer. */
    gs_verify_answer("Penguins migrate southward carrying luggage "
                     "annually [clocks].", &got);
    check(got.overlap_failed == 1,
          "a claim its source never mentions is counted");
    check(strstr(got.note, "does not mention") != NULL,
          "and the record says so");

    /* Reversing a meaning costs one word, so a negation carries the
     * vocabulary of the claim it denies and is not caught here. */
    gs_verify_answer("A moving clock does not tick more slowly than a "
                     "clock at rest [clocks].", &got);
    check(got.overlap_failed == 0,
          "a negation carries its source's words and passes the overlap");

    gs_verify_answer("", &got);
    check(got.outcome == GS_VERIFY_EMPTY, "nothing back is its own outcome");
    check(gs_verify_score(&got) == 0, "scoring nothing");

    check(gs_verify_answer(NULL, &got) == GS_ERR_ARG, "no text is refused");
    check(gs_verify_answer("x.", NULL) == GS_ERR_ARG, "nowhere to report");

    gs_verify_release();
    check(gs_verify_ready() == 0, "letting go leaves nothing to check against");
}

/* A question asked with no documents was never checkable, so its answers
 * are left unchecked rather than failed. */
static void test_no_corpus(void)
{
    gs_verify_result_t got;

    printf("a question with no documents behind it\n");
    gs_verify_release();
    check(gs_verify_corpus("build/there-is-no-such-place") != GS_OK,
          "a missing place holds nothing");
    check(gs_verify_ready() == 0, "so there is nothing to check against");

    gs_verify_answer("Clocks run slower when moving.", &got);
    check(got.outcome == GS_VERIFY_UNCHECKED,
          "an answer is left unchecked rather than failed");
    check(got.sentences == 1, "its sentences are still counted");
    check(gs_verify_score(&got) == 0, "with nothing verified to score");
    check(strstr(got.note, "no documents") != NULL, "and the record says why");

    check(gs_verify_corpus(NULL) == GS_ERR_ARG, "no place is refused");
    check(gs_verify_corpus("") == GS_ERR_ARG, "nor an empty one");
}

static void test_names(void)
{
    printf("what each outcome is called\n");
    check(strcmp(gs_verify_outcome_name(GS_VERIFY_PASSED), "passed") == 0,
          "passing is named");
    check(strlen(gs_verify_outcome_name(GS_VERIFY_UNCITED)) > 0,
          "and so is an uncited sentence");
    check(strlen(gs_verify_outcome_name(GS_VERIFY_MISQUOTED)) > 0,
          "and a quotation nothing holds");
    check(strlen(gs_verify_outcome_name(GS_VERIFY_EMPTY)) > 0,
          "and nothing coming back");
    check(strlen(gs_verify_outcome_name(GS_VERIFY_UNCHECKED)) > 0,
          "and having nothing to check against");
    check(gs_verify_score(NULL) == 0, "no result scores nothing");
}

int main(void)
{
    printf("verify\n\n");

    test_sentences();
    test_markers_and_quotations();
    test_checking();
    test_no_corpus();
    test_names();

    gs_verify_release();
    (void)system("rm -rf build/corpus-test");
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
