/* provider_internal.h
 *
 * Shared between the asking and the reading of what came back. Nothing
 * outside src/provider/ may include this.
 */
#ifndef GS_PROVIDER_INTERNAL_H
#define GS_PROVIDER_INTERNAL_H

#include <stddef.h>

#include "provider.h"

/* Writes text into out as a piece of JSON, with every character JSON
 * gives a meaning to written the way JSON expects. A quote inside a
 * prompt would otherwise end the string early and the rest would be read
 * as part of the request.
 *
 * Returns GS_OK, or an error when it will not fit.
 */
int gs_provider_escape(const char *text, char *out, size_t cap);

/* Reads the value of a named string out of a JSON body, undoing what the
 * escaping above did. Finds the first key of that name at any depth,
 * which is enough because the shape of the reply is known.
 *
 * Returns GS_OK when the key was found, or an error when it was not.
 */
int gs_provider_field(const char *body, const char *key, char *out,
                      size_t cap);

/* Builds the body of the request put to the server. */
/* The same, with a system message in front of the question. A system
 * message is the part of a chat request the model reads as standing
 * instructions rather than as something the person said. A NULL or empty
 * system leaves it out, which is exactly what gs_provider_body sends. */
int gs_provider_body_system(const char *model, const char *system,
                            const char *prompt, double temperature,
                            int seed, int limit, char *out, size_t cap);

int gs_provider_body(const char *model, const char *prompt,
                     double temperature, int seed, int limit,
                     char *out, size_t cap);

/* The same again, with the earlier exchanges of the conversation between
 * the system message and the question, each as a user message followed
 * by the assistant message that answered it. A body that will not fit is
 * refused rather than cut, since a conversation missing its middle reads
 * to a model as a different conversation. */
int gs_provider_body_turns(const char *model, const char *system,
                           const gs_provider_turn_t *history, int count,
                           const char *prompt, double temperature,
                           int seed, int limit, char *out, size_t cap);

#endif
