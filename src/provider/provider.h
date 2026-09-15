/* provider.h
 *
 * Asks a model a question and takes back what it said.
 *
 * The model runs behind an inference server, which is a program holding
 * the weights in memory and answering over HTTP on this machine. Ollama
 * is that program here, and it answers in the shape OpenAI defined, so
 * swapping it for llama.cpp later changes nothing above this line.
 *
 * Nothing here judges an answer. It asks, and it reports what came back.
 */
#ifndef GS_PROVIDER_H
#define GS_PROVIDER_H

#include "gravestone.h"

#include <stddef.h>

#define GS_PROVIDER_HOST  "127.0.0.1"
#define GS_PROVIDER_PORT  11434
#define GS_PROVIDER_MODEL 96
#define GS_PROVIDER_MAX   64     /* models one listing carries */

/* One question put to one model.
 *
 * Temperature is the dial deciding how much randomness goes into picking
 * each next word, from 0 for the same answer every time up to about 2 for
 * wild ones. Seed is the number making a given randomness repeatable, so
 * the same seed and the same temperature give the same answer twice.
 * Running one question many times at a high temperature with a different
 * seed each time is what produces the many answers this program then
 * checks.
 *
 * A token is roughly a short piece of a word, and limit is how many the
 * model may write before it has to stop.
 */
/* One earlier exchange, handed back to the model ahead of the question.
 * A model remembers nothing between requests, so a conversation it can
 * follow is one sent again, whole, every time it is asked. */
typedef struct {
    const char *prompt;
    const char *answer;
} gs_provider_turn_t;

typedef struct {
    const char *model;
    const char *system;   /* who the model is speaking as, NULL for nobody */
    const gs_provider_turn_t *history;  /* oldest first, NULL for none */
    int         history_count;
    const char *prompt;
    double      temperature;
    int         seed;
    int         limit;
} gs_provider_ask_t;

/* Why a model stopped writing.
 *
 * Some models write their working out before their answer, and the server
 * hands that back in a field of its own. A model that spends the whole
 * limit on its working out returns an answer of nothing while having
 * written a great deal, which is a different thing from a model that had
 * nothing to say. Telling the two apart is what lets the harness ask
 * again with more room rather than record a silence. */
typedef enum {
    GS_PROVIDER_STOP_UNKNOWN = 0,
    GS_PROVIDER_STOP_DONE,      /* the model finished on its own */
    GS_PROVIDER_STOP_LIMIT      /* the limit ran out first */
} gs_provider_stop_t;

typedef struct {
    gs_provider_stop_t stop;
    int  answer_len;       /* characters of answer */
    int  working_len;      /* characters of working out, shown to nobody */
} gs_provider_note_t;

/* Non zero when the inference server is answering. */
int gs_provider_available(void);

/* Starts the inference server when nothing is answering.
 *
 * A server holds the weights in memory and answers over HTTP, so it has
 * to be running before a single question can be asked. It does not
 * survive the machine restarting, and a person opening this program on a
 * cold machine has no reason to know that, so it is started here rather
 * than left to them.
 *
 * A server that already answers is left alone, which is the ordinary
 * case and costs one request. Nothing is ever started twice, because the
 * question asked first is whether the port answers rather than whether a
 * program of that name is running. Two copies of this program starting
 * together leave the second server unable to take the port, which it
 * reports and gives up on.
 *
 * wait_seconds bounds how long to keep asking before giving up. Zero
 * starts it and returns at once, which suits a caller that has other
 * work to do first.
 *
 * Returns GS_OK when the server answers, or when it was started and the
 * caller chose not to wait.
 */
int gs_provider_start(int wait_seconds);

/* Where the started server writes what it says, for reading afterwards.
 * Empty when no server has been started by this program. */
const char *gs_provider_log_path(void);

/* The models the server is holding, newest listing each time. Returns how
 * many names were written. */
int gs_provider_models(char names[][GS_PROVIDER_MODEL], int max);

/* Puts one question and writes the answer into out.
 *
 * Returns GS_OK with an answer, or an error when the server refused or
 * could not be reached. An answer of nothing is reported as GS_OK with an
 * empty buffer, since a model saying nothing is a real outcome the
 * harness records rather than a fault.
 */
int gs_provider_ask(const gs_provider_ask_t *ask, char *out, size_t cap);

/* The same, with a note on why the model stopped and how much of what it
 * wrote was working out rather than answer. Passing NULL for the note is
 * the same as calling gs_provider_ask. */
int gs_provider_ask_noted(const gs_provider_ask_t *ask, char *out,
                          size_t cap, gs_provider_note_t *note);

/* The same again, keeping the working out as well. Some models write out
 * their reasoning before the answer, and the server hands that back in a
 * field of its own. It is what a person asking how an answer was arrived
 * at wants to read, so it is copied into working, cut to fit, and the
 * note carries its whole length whether or not it fitted. A NULL working
 * measures without keeping. */
int gs_provider_ask_full(const gs_provider_ask_t *ask, char *out, size_t cap,
                         char *working, size_t working_cap,
                         gs_provider_note_t *note);

/* Prints what the server is holding. */
extern const gs_module gs_provider_module;

#endif
