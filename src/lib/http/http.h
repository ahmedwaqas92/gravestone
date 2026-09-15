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

/* The same, asking rather than sending, for the places a server answers
 * only to a plain fetch. */
int gs_http_get_stream(const char *host, int port, const char *path,
                       gs_http_line_fn on_line, void *context);

/* The same two, handing back the whole body rather than feeding it line
 * by line.
 *
 * A reply carrying one long line of JSON has no lines to speak of, and
 * the line form cuts at a fixed width and loses whatever falls past the
 * edge. A caller wanting an answer rather than a running commentary asks
 * for the body.
 *
 * Returns GS_OK when the stream ended cleanly. A body larger than the
 * buffer stops the stream and reports the failure rather than handing
 * back a piece as though it were the whole. */
int gs_http_post_body(const char *host, int port, const char *path,
                      const char *body, char *out, size_t cap);

int gs_http_get_body(const char *host, int port, const char *path,
                     char *out, size_t cap);

#endif
