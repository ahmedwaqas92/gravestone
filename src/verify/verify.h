/* verify.h
 *
 * Decides whether an answer written in plain sentences may be shown.
 *
 * A task with tests carries its own decider. A question answered in
 * sentences leaves nothing to run, so the answer is checked against
 * documents placed on disk. Every quotation it makes must appear in one
 * of them character for character, and every sentence must name the
 * source it came from.
 *
 * Nothing here asks a model whether an answer is correct. The searching
 * and the counting decide, and the counts pick the winner.
 */
#ifndef GS_VERIFY_H
#define GS_VERIFY_H

#include "gravestone.h"

#include <stddef.h>

#define GS_VERIFY_NOTE   2048   /* room for what the checking found */
#define GS_VERIFY_SPAN    512   /* longest quotation one sentence carries */

/* What the checking decided about one answer. */
typedef enum {
    GS_VERIFY_UNCHECKED = 0, /* no documents were on disk to check against */
    GS_VERIFY_PASSED,        /* every sentence cited, every quotation found */
    GS_VERIFY_UNCITED,       /* a sentence carried no source marker */
    GS_VERIFY_MISQUOTED,     /* a quotation appears in no document */
    GS_VERIFY_EMPTY          /* the model gave back nothing at all */
} gs_verify_outcome_t;

/* Everything the checking counted, which is what the ranking reads. */
typedef struct {
    gs_verify_outcome_t outcome;
    int sentences;        /* how many the answer holds */
    int cited;            /* how many carried a source marker */
    int quotations;       /* how many quotations were looked for */
    int verified;         /* how many were found, character for character */
    int overlap_failed;   /* sentences claiming what their source lacks */
    char note[GS_VERIFY_NOTE];
} gs_verify_result_t;

/* Points the checking at the documents to search. An empty or missing
 * directory leaves every answer unchecked rather than failing it, since a
 * question asked with no documents was never checkable. */
int  gs_verify_corpus(const char *directory);
int  gs_verify_ready(void);
void gs_verify_release(void);

/* Checks one answer and fills in what was counted. */
int gs_verify_answer(const char *text, gs_verify_result_t *out);

/* The score the ranking reads, which is how many quotations were found.
 * An answer that failed outright scores nothing. */
int gs_verify_score(const gs_verify_result_t *result);

/* Names an outcome for a person reading the record. */
const char *gs_verify_outcome_name(gs_verify_outcome_t outcome);

/* Prints what the documents on disk hold. */
extern const gs_module gs_verify_module;

#endif
