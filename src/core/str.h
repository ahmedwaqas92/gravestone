#ifndef GS_CORE_STR_H
#define GS_CORE_STR_H

#include <stddef.h>

/* Copies into a fixed field, cutting the text short when it will not fit
 * and always leaving a terminator. The cut is deliberate, since system
 * fields are frequently larger than the places we keep them. */
void gs_str_copy(char *dst, size_t cap, const char *src);

/* Renders a byte count the way a person reads it, such as "7.6 GB". */
void gs_str_bytes(long long bytes, char *out, size_t cap);

/* A moment, written by the clock of the machine this runs on.
 *
 * The number handed in is seconds since the start of 1970, which is what
 * the store keeps against every prompt and every answer. What comes out
 * is the local reading of that moment, so a person in Karachi and a
 * person in London each see their own wall clock rather than a shared
 * one.
 *
 * Today gives the hour and minute alone, as in "14:32". An earlier day
 * carries its date too, as in "9 Sep 14:32", since a bare hour on a
 * conversation from last week says nothing useful.
 *
 * A time of nought or less writes an empty string, since a moment nobody
 * recorded is better shown as nothing than as 1970. */
void gs_str_clock(long long seconds, char *out, size_t cap);

/* Writes text out as the one word Windows takes for a whole command.
 *
 * A command handed to Windows through a shell is read by that shell
 * first, so a dollar sign inside it names a shell variable and vanishes
 * before Windows ever sees it. Windows reads a command as pairs of bytes,
 * and it accepts one written in base64, which no shell touches. Widening
 * the text and writing it that way is what carries it across whole.
 *
 * Returns the number of characters written, not counting the terminator,
 * or a negative value when it will not fit.
 */
int gs_str_windows_command(const char *text, char *out, size_t cap);

/* Writes the name of the WSL distribution this program is running inside,
 * taken from the surroundings the system sets up. Windows reaches the
 * files of a distribution by that name, and the mirrored window carries
 * it in brackets after the title, so both need it.
 *
 * The name is refused unless it holds only letters, digits, dot, dash and
 * underscore, since it reaches a shell and a Windows path. A refusal is
 * total rather than a trim, because a trimmed name would name a different
 * distribution.
 *
 * Returns GS_OK, or an error when there is no name or it is unclean.
 */
int gs_str_wsl_distro(char *out, size_t cap);

/* Writes text in plain ASCII for a screen that can only draw ASCII.
 *
 * Models write UTF-8, where one character beyond plain English takes two
 * to four bytes. The glyph code draws one byte as one character and has
 * nothing for a byte above 126, so the three bytes of a dash vanished and
 * "question\u2014and" reached the screen as "questionand".
 *
 * Dashes, curly quotes, the ellipsis, bullets, arrows and the no-break
 * space become their plain spelling. A character with no plain spelling,
 * an emoji for instance, is left out, and a space is put in its place
 * when leaving it out would join two words together. A malformed byte is
 * skipped rather than trusted.
 *
 * Returns the number of characters written, not counting the terminator.
 */
size_t gs_str_to_ascii(const char *text, char *out, size_t cap);

#endif
