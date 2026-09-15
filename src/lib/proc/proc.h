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

/* Both forms append a redirection to the command handed in, so one
 * command is what they expect. Two joined by a semicolon would have the
 * redirection land on the second alone. */

/* The same, keeping whatever the program said on its error channel as
 * well. A tool that refuses says why there, and a caller about to tell a
 * person that something would not work needs those words.
 *
 * Returns GS_OK when the program exited with zero. On a failure the text
 * is still written, so the reason survives the error. */
int gs_proc_reason(const char *command, char *out, size_t cap);

/* Non zero when a program of that name can be found on the path. */
int gs_proc_exists(const char *name);

/* Runs a program with no shell involved anywhere. The two forms above
 * hand their text to /bin/sh, which reads a semicolon, a backtick and an
 * ampersand as instructions, so a value coming out of a database or out
 * of the surroundings can carry a second command inside it. Here the
 * program and each of its arguments travel as separate strings that
 * nothing ever parses, which is the only safe way to run anything as
 * another user.
 *
 * program must be an absolute path. Nothing is looked up on the search
 * path, because that path is written by whoever controls the
 * surroundings. The child is given a fixed one of its own instead.
 *
 * argv is the argument list the program sees, ending in a NULL. By
 * custom argv[0] repeats the program name.
 *
 * Whatever the program printed on either channel is copied into out, cut
 * short when it will not fit. exit_code receives the number the program
 * exited with, which is where the answer lives, since a program that
 * worked in silence prints nothing at all.
 *
 * seconds bounds the wait. A program still running at the deadline is
 * killed outright and reported as a failure, so a tool waiting on a
 * device that never answers cannot hold the interface still.
 *
 * Returns GS_OK when the program ran to completion, whatever it exited
 * with. GS_ERR covers a timeout or a death by signal, GS_ERR_IO a child
 * that could not be started, and GS_ERR_ARG a malformed request.
 */
int gs_proc_run(const char *program, const char *const *argv,
                char *out, size_t cap, int *exit_code, int seconds);

/* The same, with bytes written to the program's own input first.
 *
 * This exists for sudo, which reads a password on its input when it is
 * asked to. A password must never travel as an argument, because the
 * arguments of every running program are readable by anyone on the
 * machine through /proc, and never in the surroundings for the same
 * reason. Down a pipe it reaches that one program and nothing else.
 *
 * input is copied nowhere. It is written straight to the pipe and this
 * call never keeps a copy, so wiping the caller's own buffer afterwards
 * is enough to have it gone. Nothing about it reaches the log, whatever
 * the logging level.
 *
 * A NULL input means the program reads end of file at once, which is
 * what stops a program that wants typing from sitting there waiting.
 */
int gs_proc_run_input(const char *program, const char *const *argv,
                      const char *input, size_t input_len,
                      char *out, size_t cap, int *exit_code, int seconds);

/* Starts a program and walks away from it.
 *
 * The program is put in a session of its own, so it outlives this one
 * and no longer shares the terminal. A server started this way keeps
 * answering after the thing that started it has closed, which is how a
 * person expects a server to behave.
 *
 * Nothing it prints comes back. log_path receives its output when it is
 * given, and the file is written fresh each time rather than grown for
 * ever. A NULL sends the output to the bin.
 *
 * Unlike the two calls above, the surroundings this program has are
 * handed on unchanged, because a server reads its own settings from
 * them. The place it keeps its files is one of those, so scrubbing them
 * would start a server that cannot find anything.
 *
 * Returns GS_OK once the program has been started, which says nothing
 * about whether it worked. Ask it a question to learn that.
 */
int gs_proc_spawn(const char *program, const char *const *argv,
                  const char *log_path);

#endif
