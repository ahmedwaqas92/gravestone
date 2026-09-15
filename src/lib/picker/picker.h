/* picker.h
 *
 * Asks the person running the program to choose a file.
 *
 * Every system puts its own box on the screen for this, so the box itself
 * is never drawn here. Windows has one built in. A Linux desktop has one
 * behind whichever helper is installed. Running under WSL, the Linux
 * program borrows the Windows box and the path comes back translated.
 *
 * The call does not return until the person answers, which may be
 * minutes, so a program with a window of its own runs it away from the
 * loop that keeps that window painted.
 */
#ifndef GS_LIB_PICKER_H
#define GS_LIB_PICKER_H

#include <stddef.h>

/* Chose nothing. Separate from an error, since a person closing the box
 * is an ordinary answer rather than a fault. */
#define GS_PICKER_NONE 1

/* Non zero when this machine can put a file box on the screen at all. */
int gs_picker_available(void);

/* Names what the box will accept, as it appears in the box itself. The
 * list matches what the chat panel takes: spreadsheets, documents,
 * pictures, sound and video. */
const char *gs_picker_kinds(void);

/* Puts the box on the screen and writes the chosen path into out.
 *
 * Returns GS_OK with a path, GS_PICKER_NONE when the person chose
 * nothing, or an error when no box could be opened. The path is in the
 * form this program uses to open files, so under WSL a Windows path comes
 * back as the mounted one.
 */
int gs_picker_open(const char *title, char *out, size_t cap);

#endif
