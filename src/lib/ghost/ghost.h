/* ghost.h
 *
 * Clears the Windows side leftovers of a Linux window.
 *
 * Running a Linux program under WSL puts its window on the Windows
 * desktop by mirroring it. The mirror is an ordinary Windows window owned
 * by msrdc.exe, the client that carries the picture across, and that
 * process sits outside this distribution. A mirror whose Linux window
 * went away without the compositor being told stays on the desktop and in
 * the task switcher for the rest of the session, and nothing on the Linux
 * side can destroy it. Hiding it is the disposal available, and a hidden
 * mirror is gone from the screen, from the task switcher and from the
 * taskbar.
 *
 * Everything here is a quiet no operation away from WSL.
 */
#ifndef GS_LIB_GHOST_H
#define GS_LIB_GHOST_H

#include <stddef.h>

/* Non zero when this machine mirrors windows onto a Windows desktop. */
int gs_ghost_available(void);

/* Writes the title pattern that matches a mirror of one of our windows.
 * The mirror carries the distribution name in brackets after the title,
 * so "Gravestone" becomes "Gravestone*(Debian)*" on Debian. An unknown
 * distribution drops the bracket part rather than guessing.
 *
 * Returns GS_OK, or GS_ERR_ARG when there is nowhere to write or no room.
 */
int gs_ghost_pattern(const char *title, char *out, size_t cap);

/* Hides every mirror matching the pattern. Counts what it hid into hidden
 * and what it found already hidden into already, either of which may be
 * NULL. Returns GS_OK when the sweep ran.
 *
 * Takes about half a second, since it starts a Windows program. Refuses
 * to run while another copy of this program is alive, because two copies
 * share one title and the sweep cannot tell a corpse from a live window.
 */
int gs_ghost_sweep(const char *title, int *hidden, int *already);

/* The same sweep with nothing waiting for it. The work is handed to a
 * child that outlives this process, so a program shutting down pays
 * nothing for it. Returns GS_OK once the child is on its way.
 *
 * The child waits a moment first, which lets the compositor take the
 * mirror down by itself and lets this program finish going. It then
 * refuses in the same way, so a copy still running keeps its window. */
int gs_ghost_sweep_detached(const char *title);

/* Non zero when another copy of this program is running. Used to hold the
 * sweep back, and useful on its own. */
int gs_ghost_others_running(void);

#endif
