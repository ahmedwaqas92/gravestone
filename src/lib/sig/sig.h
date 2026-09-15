/* sig.h
 *
 * Catches the signals that ask a program to stop, so the window is taken
 * off the screen properly instead of the process vanishing under it.
 *
 * A handler runs between two ordinary instructions and may only touch a
 * short list of functions, so this one writes a single byte down a pipe
 * and returns. The waiting loop watches the other end of that pipe, sees
 * the byte, and does the real shutdown work with the process back in a
 * normal state. Setting a flag alone does not work here, because a wait
 * that is interrupted starts again from the beginning and never gives the
 * loop a chance to read the flag.
 */
#ifndef GS_LIB_SIG_H
#define GS_LIB_SIG_H

/* Starts catching interrupt, terminate and hangup. Safe to call twice.
 * Returns GS_OK, or an error when the pipe could not be made, in which
 * case the signals keep their old behaviour and the program still runs. */
int gs_sig_install(void);

/* The descriptor a waiting loop should watch alongside its own. Returns
 * -1 before install has run, or after release, and a loop given -1 simply
 * has nothing extra to watch. */
int gs_sig_wake_fd(void);

/* Non zero once a stop signal has arrived. Stays set until release.
 *
 * Only the first stop signal is caught. That signal goes back to its
 * ordinary behaviour straight away, so a second press of the same key
 * ends the program on the spot rather than waiting behind whatever the
 * tidying is doing. */
int gs_sig_quit_requested(void);

/* Which signal arrived first, or 0 when none has. */
int gs_sig_quit_number(void);

/* Empties the pipe so the descriptor stops reporting itself ready. The
 * quit flag is untouched, since the loop still has to act on it. */
void gs_sig_drain(void);

/* Puts the old behaviour back, closes the pipe, and forgets any signal
 * that arrived, so a later install starts clean. */
void gs_sig_release(void);

/* Ends the program the way the signal would have ended it, by restoring
 * the default behaviour and raising the same signal again. A shell then
 * sees a program killed by an interrupt rather than one that exited by
 * itself. Does nothing when no signal arrived. */
void gs_sig_reraise(void);

#endif
