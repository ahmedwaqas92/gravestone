/* ghost_internal.h
 *
 * Shared between the sweep and the encoder that carries its script across
 * to Windows. Nothing outside src/lib/ghost/ may include this.
 */
#ifndef GS_LIB_GHOST_INTERNAL_H
#define GS_LIB_GHOST_INTERNAL_H

#include <stddef.h>

/* Takes off the note the system puts on the end of a program's path once
 * that program has been replaced on disk. A rebuild leaves every process
 * still running the old binary reading back with that note, and the same
 * program rebuilt is still the same program for the purpose of deciding
 * whether another copy is open. */
void gs_ghost_drop_deleted_note(char *path);

/* Cuts a forked child loose from the program that made it. The parent
 * caught the stop signals and a fork keeps that arrangement, so a signal
 * aimed at the group would be swallowed by the child and would cut short
 * whatever it is waiting for. This puts the ordinary behaviour back and
 * moves the child into a group of its own. Only meaningful in a child. */
void gs_ghost_detach(void);

/* Builds the PowerShell that walks the desktop and hides what matches. */
int gs_ghost_script(const char *pattern, char *out, size_t cap);

#endif
