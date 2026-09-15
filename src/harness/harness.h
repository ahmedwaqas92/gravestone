/* harness.h
 *
 * Puts one question to every chosen model, one at a time, and records
 * what each said.
 *
 * One at a time because a model that fits the machine only by using the
 * card and the memory together leaves no room for a second beside it.
 * Each one answers, its answer is written down, and the next begins.
 *
 * Every answer is then checked, and the counts pick which is shown.
 * Nothing here asks a model whether an answer is correct.
 *
 * The work runs on a thread of its own, so the window keeps painting
 * while a model thinks.
 */
#ifndef GS_HARNESS_H
#define GS_HARNESS_H

#include "gravestone.h"
#include "provider.h"

#include <stddef.h>

#define GS_HARNESS_MODELS  16     /* models one run may put a question to */

/* How many tokens a model may write. A token is roughly a short piece of
 * a word. The allowance is a ceiling rather than a target, so a model
 * that finishes early stops early and a larger first allowance costs it
 * nothing. The first one was 512, and a model that writes its working
 * out before its answer used most of that thinking and was cut off four
 * words into its reply. A model that still runs out is asked once more
 * with twice as much. */
#define GS_HARNESS_FIRST_LIMIT  2048
#define GS_HARNESS_SECOND_LIMIT 4096
#define GS_HARNESS_LOG   8192     /* the record kept of the whole run */

/* The standing instructions every model is given ahead of the question.
 * The one %s is the name of the model being asked. Everything here is
 * true of the program as it stands, since a model repeats what it is told
 * and a claim written here reaches the person as a fact. */
#define GS_HARNESS_SYSTEM \
    "You are the Gravestone agent. Gravestone is a desktop application " \
    "that runs open weight language models on this person's own " \
    "computer, through an inference server on the same machine, so the " \
    "conversation is not sent to any online service. The person is " \
    "talking to you through the Gravestone chat box. The model answering " \
    "this time is %s. Gravestone can put the same question to several " \
    "models and checks their answers with real tools before one is " \
    "shown, so give one complete answer. When asked who you are, say you " \
    "are the Gravestone agent running %s. Answer directly and plainly."


/* Where a run has reached. */
typedef enum {
    GS_HARNESS_IDLE = 0,
    GS_HARNESS_ASKING,      /* a model is thinking */
    GS_HARNESS_CHECKING,    /* every answer is in, the checking runs */
    GS_HARNESS_DONE,
    GS_HARNESS_FAILED
} gs_harness_stage_t;

typedef struct {
    gs_harness_stage_t stage;
    int   models;           /* how many were asked */
    int   answered;         /* how many have answered so far */
    int   passed;           /* how many survived the checking */
    int   empty;            /* how many gave back nothing */
    long long turn_id;
    long long shown;        /* the answer that won, 0 while none has */
    char  now[GS_PROVIDER_MODEL];   /* the model thinking right now */
    char  log[GS_HARNESS_LOG];      /* the whole record, line by line */
} gs_harness_state_t;

/* Starts a run. The prompt is already written down as a turn, and every
 * answer is recorded against it.
 *
 * Returns GS_OK once the thread is out, or an error when a run is already
 * going, when no models were named, or when no server is answering. */
int gs_harness_begin(long long session_id, long long turn_id,
                     const char *prompt,
                     const char names[][GS_PROVIDER_MODEL], int count);

/* Copies where the run has reached, which the panel reads every frame.
 * Safe to call while the thread is out. */
void gs_harness_read(gs_harness_state_t *out);

/* Non zero once the run has finished and the record is complete. Reading
 * it lets go of the thread, so it is called until it answers. */
int gs_harness_poll(void);

/* Waits for the run to finish and lets go of the thread. */
void gs_harness_wait(void);

/* Non zero while a run is going. */
int gs_harness_running(void);

/* Points the checking at the documents to search, kept here so a caller
 * sets it once for the whole run. */
void gs_harness_corpus(const char *directory);


/* Puts one question to every model the server holds. */
extern const gs_module gs_harness_module;

#endif
