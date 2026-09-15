/* harness_internal.h
 *
 * Shared between the files of this module and its test. Nothing outside
 * src/harness/ may include this.
 */
#ifndef GS_HARNESS_INTERNAL_H
#define GS_HARNESS_INTERNAL_H

#include <stddef.h>

#include "provider.h"
#include "session.h"

/* The most earlier exchanges ever sent with a question. */
#define GS_HARNESS_HISTORY_TURNS 16

/* The bytes the standing instructions, the earlier exchanges and the
 * question may fill between them.
 *
 * The server holds 4096 tokens of conversation for a model, a token
 * being the piece of a word the model reads and writes one at a time,
 * and the first answer may take 2048 of them, which leaves 2048 for
 * everything sent. English runs to about four bytes a token, so 6000
 * bytes is near 1500 tokens and leaves room over. The four is a rule of
 * thumb and has not been measured against this tokeniser, which is the
 * part of the model that cuts text into tokens. Code cuts finer, near
 * three bytes a token, and 6000 bytes of it is still under 2048. */
#define GS_HARNESS_ROOM_BYTES 6000

/* What goes to the model ahead of the question. The turns point into
 * storage kept by harness_history.c, which stays good until the next
 * load. */
typedef struct {
    gs_provider_turn_t turns[GS_HARNESS_HISTORY_TURNS];
    int count;       /* exchanges sent, oldest first */
    int left_out;    /* exchanges read and left out for room */
    int cut;         /* the newest answer was cut to fit */
} gs_harness_history_t;

/* Reads the conversation asked before turn_id and keeps the newest
 * exchanges that fit in the room left once the standing instructions
 * and the question are counted. Returns how many were kept.
 *
 * The newest exchange is kept even when it alone is too long, with the
 * end of its answer cut, since a question about what was just said is
 * the one most likely to depend on it. Called from the worker thread
 * alone, one run at a time. */
int gs_harness_history_load(long long session_id, long long turn_id,
                            const char *system, const char *prompt,
                            gs_harness_history_t *out);

#endif
