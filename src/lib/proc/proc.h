/* proc.h
 *
 * Runs another program and keeps what it printed. Wrapped here because
 * every platform starts a process differently, and because a hung tool
 * must never hold the interface still.
 */
#ifndef GS_LIB_PROC_H
#define GS_LIB_PROC_H

#include <stddef.h>

/* Runs command through the shell and copies its standard output into out,
 * cutting it short when it will not fit. Standard error is discarded.
 *
 * Returns GS_OK when the program ran and exited with zero. A missing tool,
 * a failure, or no output at all is reported as an error, so a caller can
 * fall back without inspecting the text.
 */
int gs_proc_capture(const char *command, char *out, size_t cap);

/* Non zero when a program of that name can be found on the path. */
int gs_proc_exists(const char *name);

#endif
