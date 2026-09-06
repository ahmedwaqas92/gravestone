/* http.h
 *
 * Talks HTTP over a plain socket to a server on this machine. Wrapped
 * here because every platform opens sockets differently, and because the
 * one caller today, the pull progress reader, wants lines as they
 * arrive rather than a body after the fact.
 */
#ifndef GS_LIB_HTTP_H
#define GS_LIB_HTTP_H

#include <stddef.h>

/* Called once per line of the response body, without its newline. The
 * stream carries on while the callback returns 0 and stops early when it
 * returns non zero. */
typedef int (*gs_http_line_fn)(const char *line, void *context);

/* Posts body to path and feeds every line of the reply to on_line as it
 * arrives. Understands chunked transfer, which is how a server sends a
 * body whose length it does not know yet, as counted pieces ending with
 * a piece of length zero. Returns GS_OK when the stream ended cleanly. */
int gs_http_post_stream(const char *host, int port, const char *path,
                        const char *body, gs_http_line_fn on_line,
                        void *context);

#endif
