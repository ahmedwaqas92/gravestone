/* verify_internal.h
 *
 * Shared between the checking and the searching. Nothing outside
 * src/verify/ may include this.
 */
#ifndef GS_VERIFY_INTERNAL_H
#define GS_VERIFY_INTERNAL_H

#include <stddef.h>

/* Writes one sentence of text into out. A sentence ends at a full stop, a
 * question mark or an exclamation mark followed by a space or the end,
 * which is what a person reading would call a sentence.
 *
 * Returns 1 when a sentence was written, 0 once there are no more. */
int gs_verify_sentence(const char *text, int index, char *out, size_t cap);

/* Writes the quotation one sentence carries into out, taken as the words
 * between the first pair of double quotes.
 *
 * Returns 1 when a quotation was found, 0 when the sentence carries
 * none. */
int gs_verify_quotation(const char *sentence, char *out, size_t cap);

/* Writes the source marker one sentence carries, taken as the text
 * between the last pair of square brackets, which is where a marker sits
 * when it follows the words it belongs to.
 *
 * Returns 1 when a marker was found, 0 when the sentence carries none. */
int gs_verify_marker(const char *sentence, char *out, size_t cap);

/* Non zero when the whole of span appears somewhere in the documents,
 * character for character. */
int gs_verify_span_found(const char *span);

/* How many content words of the sentence appear in the document the
 * marker names. A word of three letters or fewer is skipped, since the
 * small joining words appear everywhere and say nothing.
 *
 * Writes how many words were looked at into looked_at, which may be
 * NULL. Returns how many were found. */
int gs_verify_overlap(const char *sentence, const char *marker,
                      int *looked_at);

#endif
