/* verify.c
 *
 * The checking itself, in the order the design gives it. Every sentence
 * must name where it came from, every quotation must appear in a document
 * character for character, and a sentence claiming what its source never
 * mentions is counted against the answer.
 */
#include "verify.h"
#include "verify_internal.h"
#include "gravestone.h"
#include "log.h"
#include "str.h"

#include <stdio.h>
#include <string.h>

/* A sentence claiming something its source never mentions fails. Half the
 * content words of the sentence appearing in the document it names is
 * enough, since reversing a meaning costs one word and a negation carries
 * the vocabulary of the claim it denies. */
#define OVERLAP_NUM 1
#define OVERLAP_DEN 2

static void note(gs_verify_result_t *out, const char *line)
{
    size_t at = strlen(out->note);
    size_t len = strlen(line);

    if (at + len + 2 >= sizeof out->note)
        return;
    memcpy(out->note + at, line, len);
    out->note[at + len] = '\n';
    out->note[at + len + 1] = '\0';
}

int gs_verify_answer(const char *text, gs_verify_result_t *out)
{
    char sentence[1024];
    char span[GS_VERIFY_SPAN];
    char marker[128];
    char line[256];
    int index;

    if (out == NULL)
        return GS_ERR_ARG;
    memset(out, 0, sizeof *out);
    if (text == NULL)
        return GS_ERR_ARG;

    if (text[0] == '\0') {
        out->outcome = GS_VERIFY_EMPTY;
        note(out, "the model gave back nothing");
        return GS_OK;
    }

    /* A question asked with no documents was never checkable, so an
     * answer to it is left unchecked rather than failed. */
    if (!gs_verify_ready()) {
        out->outcome = GS_VERIFY_UNCHECKED;
        for (index = 0; gs_verify_sentence(text, index, sentence,
                                           sizeof sentence); index++)
            out->sentences++;
        note(out, "no documents were on disk, so nothing was checked");
        return GS_OK;
    }

    out->outcome = GS_VERIFY_PASSED;

    for (index = 0; gs_verify_sentence(text, index, sentence,
                                       sizeof sentence); index++) {
        int has_marker;
        int has_span;

        out->sentences++;
        has_marker = gs_verify_marker(sentence, marker, sizeof marker);
        has_span = gs_verify_quotation(sentence, span, sizeof span);

        /* A sentence without a marker puts the whole answer out before
         * any ranking happens, so an answer of five perfect quotations
         * joined by invented claims never reaches the top. */
        if (!has_marker) {
            out->outcome = GS_VERIFY_UNCITED;
            snprintf(line, sizeof line,
                     "sentence %d names no source", index + 1);
            note(out, line);
            continue;
        }
        out->cited++;

        if (has_span) {
            out->quotations++;
            if (gs_verify_span_found(span)) {
                out->verified++;
            } else {
                if (out->outcome == GS_VERIFY_PASSED)
                    out->outcome = GS_VERIFY_MISQUOTED;
                snprintf(line, sizeof line,
                         "sentence %d quotes words no document holds",
                         index + 1);
                note(out, line);
            }
        }

        /* A sentence naming a real quotation can still misstate it, so
         * its own words are counted against the document it names. */
        {
            int looked_at = 0;
            int shared = gs_verify_overlap(sentence, marker, &looked_at);

            if (looked_at > 0 &&
                shared * OVERLAP_DEN < looked_at * OVERLAP_NUM) {
                out->overlap_failed++;
                snprintf(line, sizeof line,
                         "sentence %d claims what %s does not mention, "
                         "%d of %d words shared",
                         index + 1, marker, shared, looked_at);
                note(out, line);
            }
        }
    }

    if (out->sentences == 0) {
        out->outcome = GS_VERIFY_EMPTY;
        note(out, "the answer holds no sentences");
        return GS_OK;
    }

    snprintf(line, sizeof line,
             "%d sentence%s, %d cited, %d of %d quotations found, "
             "%d failed the overlap",
             out->sentences, out->sentences == 1 ? "" : "s", out->cited,
             out->verified, out->quotations, out->overlap_failed);
    note(out, line);
    return GS_OK;
}

int gs_verify_score(const gs_verify_result_t *result)
{
    if (result == NULL)
        return 0;
    /* An answer that failed outright scores nothing, whatever it quoted,
     * since it never reaches the ranking. */
    if (result->outcome == GS_VERIFY_UNCITED ||
        result->outcome == GS_VERIFY_MISQUOTED ||
        result->outcome == GS_VERIFY_EMPTY)
        return 0;
    return result->verified;
}

const char *gs_verify_outcome_name(gs_verify_outcome_t outcome)
{
    switch (outcome) {
    case GS_VERIFY_PASSED:    return "passed";
    case GS_VERIFY_UNCITED:   return "a sentence named no source";
    case GS_VERIFY_MISQUOTED: return "quoted words no document holds";
    case GS_VERIFY_EMPTY:     return "gave back nothing";
    default:                  return "nothing to check against";
    }
}

/* ---- what the command line shows ---- */

static int verify_init(void)
{
    return GS_OK;
}

static int verify_run(int argc, char **argv)
{
    const char *where = argc > 2 ? argv[2] : "data/corpus";

    if (gs_verify_corpus(where) != GS_OK) {
        printf("no documents under %s to check answers against\n", where);
        return GS_ERR;
    }
    printf("checking answers against the documents under %s\n", where);
    gs_verify_release();
    return GS_OK;
}

static void verify_shutdown(void)
{
    gs_verify_release();
}

const gs_module gs_verify_module = {
    "verify",
    "report the documents answers are checked against",
    verify_init,
    verify_run,
    verify_shutdown
};
