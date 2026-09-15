#include "http.h"
#include "gravestone.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows carries the same sockets under different names, and asks to be
 * started before any of them are used. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define GS_SOCK_CLOSE closesocket
#define GS_SOCK_READ(fd, buf, n) recv((fd), (buf), (int)(n), 0)
#define GS_SOCK_WRITE(fd, buf, n) send((fd), (buf), (int)(n), 0)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#define GS_SOCK_CLOSE close
#define GS_SOCK_READ read
/* send rather than write, because a write to a socket the far end has
 * already closed raises a pipe signal, and the default behaviour for that
 * signal ends the program before the error below can be answered. */
#define GS_SOCK_WRITE(fd, buf, n) send((fd), (buf), (n), MSG_NOSIGNAL)
#endif

/* macOS asks for the quiet write once on the socket rather than on every
 * call, and a system with neither way leaves the flag at nothing, which
 * asks for nothing and builds as it always did. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define LINE_MAX_BYTES 2048
#define CHUNK_HEAD_MAX 16

/* One response being taken apart. The server hands over arbitrary
 * fragments, and these fields remember where the parsing stood when the
 * last fragment ran out. */
typedef struct {
    int  in_header;
    int  chunked;
    long chunk_left;       /* body bytes left in the piece being read */
    int  reading_size;     /* between pieces, collecting the hex length */
    char size_text[CHUNK_HEAD_MAX];
    int  size_len;
    char line[LINE_MAX_BYTES];
    int  line_len;
    int  line_overflow;
    char header[1024];
    int  header_len;
    int  done;
    int  stopped;
    int  broken;       /* the framing itself was wrong */
    gs_http_line_fn on_line;
    void *context;
    /* A caller wanting the body whole points these at a buffer, and the
     * line splitting above is then bypassed. A reply carrying one long
     * line of JSON has no lines to speak of, and tearing it at a fixed
     * width would lose whatever fell past the edge. */
    char  *body_out;
    size_t body_cap;
    size_t body_len;
} stream_t;

static void body_byte(stream_t *st, char c)
{
    /* Kept whole for a caller that asked for the body rather than for
     * lines. Filling the buffer stops the stream rather than quietly
     * dropping the rest. */
    if (st->body_out != NULL) {
        if (st->body_len + 1 >= st->body_cap) {
            st->stopped = 1;
            return;
        }
        st->body_out[st->body_len++] = c;
        st->body_out[st->body_len] = '\0';
        return;
    }

    if (c == '\n') {
        if (!st->line_overflow && !st->stopped) {
            st->line[st->line_len] = '\0';
            if (st->on_line(st->line, st->context) != 0)
                st->stopped = 1;
        }
        st->line_len = 0;
        st->line_overflow = 0;
        return;
    }
    if (c == '\r')
        return;
    if (st->line_len >= LINE_MAX_BYTES - 1) {
        /* A line past any sane length is dropped whole rather than fed
         * to the caller in torn pieces. */
        st->line_overflow = 1;
        return;
    }
    st->line[st->line_len++] = c;
}

/* Feeds one received byte through the chunked framing. A chunk arrives
 * as a hex length, a newline, that many body bytes, and a trailing
 * newline, with a length of zero closing the stream. */
static void chunked_byte(stream_t *st, char c)
{
    if (st->chunk_left > 0) {
        body_byte(st, c);
        st->chunk_left--;
        return;
    }
    if (!st->reading_size) {
        /* Between the body and the next size sit \r\n leftovers. */
        if (c == '\r' || c == '\n')
            return;
        st->reading_size = 1;
        st->size_len = 0;
    }
    if (c != '\r' && c != '\n') {
        if (st->size_len < CHUNK_HEAD_MAX - 1)
            st->size_text[st->size_len++] = c;
        return;
    }
    if (c == '\n') {
        long parsed;
        char *end = NULL;

        st->size_text[st->size_len] = '\0';
        st->reading_size = 0;
        parsed = strtol(st->size_text, &end, 16);
        /* A length that is no hex number, is negative, or claims more
         * than a sane chunk carries marks a broken stream. */
        if (end == st->size_text || parsed < 0 || parsed > 16 * 1024 * 1024) {
            st->done = 1;
            st->broken = 1;
            return;
        }
        if (parsed == 0) {
            st->done = 1;
            return;
        }
        st->chunk_left = parsed;
    }
}

static void feed(stream_t *st, const char *data, size_t len)
{
    size_t i;

    for (i = 0; i < len && !st->done; i++) {
        char c = data[i];

        if (st->in_header) {
            if (st->header_len < (int)sizeof st->header - 1)
                st->header[st->header_len++] = c;
            st->header[st->header_len] = '\0';
            if (st->header_len >= 4 &&
                strcmp(st->header + st->header_len - 4, "\r\n\r\n") == 0) {
                st->in_header = 0;
                st->chunked = strstr(st->header,
                                     "Transfer-Encoding: chunked") != NULL;
                st->reading_size = 0;
                st->chunk_left = 0;
            }
            continue;
        }
        if (st->chunked)
            chunked_byte(st, c);
        else
            body_byte(st, c);
    }
}

/* One request, whichever word it opens with. A body of nothing means the
 * request carries none, which is what a plain fetch looks like. */
static int send_request(const char *method, const char *host, int port,
                        const char *path, const char *body,
                        gs_http_line_fn on_line, void *context,
                        char *body_out, size_t body_cap)
{
    struct sockaddr_in address;
    char request[1024];
    stream_t st;
    int written;
#ifdef _WIN32
    SOCKET fd;
    DWORD patience = 600000;
    WSADATA started;

    if (WSAStartup(MAKEWORD(2, 2), &started) != 0)
        return GS_ERR_IO;
#else
    int fd;
    struct timeval patience = {600, 0};
#endif

    /* One of the two ways of taking the reply has to be given. A caller
     * wanting lines hands in a callback, and one wanting the body whole
     * hands in a buffer. */
    if (host == NULL || path == NULL || port <= 0 || port > 65535)
        return GS_ERR_ARG;
    if (on_line == NULL && body_out == NULL)
        return GS_ERR_ARG;
    if (body == NULL)
        body = "";

    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1)
        return GS_ERR_ARG;

    fd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
    if (fd == INVALID_SOCKET)
        return GS_ERR_IO;
#else
    if (fd < 0)
        return GS_ERR_IO;
#endif
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&patience,
               sizeof patience);
#ifdef SO_NOSIGPIPE
    {
        int quiet = 1;

        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&quiet,
                   sizeof quiet);
    }
#endif
    if (connect(fd, (struct sockaddr *)&address, sizeof address) != 0) {
        gs_log_debug("http: nothing answering at %s:%d", host, port);
        GS_SOCK_CLOSE(fd);
        return GS_ERR_IO;
    }

    /* Head and body go together whenever they fit, since a server reading
     * once expects one piece and a second write can arrive after it has
     * already answered. A body too large to sit beside the head follows
     * on its own. */
    written = snprintf(request, sizeof request,
                       "%s %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n\r\n%s",
                       method, path, host, strlen(body), body);
    if (written > 0 && written < (int)sizeof request)
        body = "";                 /* it went with the head */
    else
        written = snprintf(request, sizeof request,
                           "%s %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n\r\n",
                           method, path, host, strlen(body));
    if (written <= 0 || written >= (int)sizeof request) {
        GS_SOCK_CLOSE(fd);
        return GS_ERR_ARG;
    }
    if (GS_SOCK_WRITE(fd, request, (size_t)written) != written) {
        GS_SOCK_CLOSE(fd);
        return GS_ERR_IO;
    }
    if (body[0] != '\0') {
        size_t len = strlen(body);
        size_t sent = 0;

        while (sent < len) {
            ssize_t n = GS_SOCK_WRITE(fd, body + sent, len - sent);

            if (n <= 0) {
                GS_SOCK_CLOSE(fd);
                return GS_ERR_IO;
            }
            sent += (size_t)n;
        }
    }

    memset(&st, 0, sizeof st);
    st.in_header = 1;
    st.on_line = on_line;
    st.context = context;
    st.body_out = body_out;
    st.body_cap = body_cap;
    if (body_out != NULL && body_cap > 0)
        body_out[0] = '\0';

    for (;;) {
        char buffer[4096];
        int got = (int)GS_SOCK_READ(fd, buffer, sizeof buffer);

        if (got <= 0)
            break;
        feed(&st, buffer, (size_t)got);
        if (st.done || st.stopped)
            break;
    }

    /* A body with no framing ends when the connection does, so whatever
     * sits in the line buffer at that point is the last line. A caller
     * taking the body whole has nothing waiting, since every byte went
     * straight into the buffer. */
    if (st.body_out == NULL && !st.chunked && st.line_len > 0 && !st.stopped)
        body_byte(&st, '\n');

    GS_SOCK_CLOSE(fd);

    if (st.broken)
        return GS_ERR;
    if (st.stopped && !st.done)
        return GS_ERR;
    if (st.in_header)
        return GS_ERR_IO;
    return GS_OK;
}

int gs_http_post_stream(const char *host, int port, const char *path,
                        const char *body, gs_http_line_fn on_line,
                        void *context)
{
    if (body == NULL)
        return GS_ERR_ARG;
    return send_request("POST", host, port, path, body, on_line, context,
                        NULL, 0);
}

int gs_http_get_stream(const char *host, int port, const char *path,
                       gs_http_line_fn on_line, void *context)
{
    return send_request("GET", host, port, path, "", on_line, context,
                        NULL, 0);
}

int gs_http_post_body(const char *host, int port, const char *path,
                      const char *body, char *out, size_t cap)
{
    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    return send_request("POST", host, port, path, body, NULL, NULL,
                        out, cap);
}

int gs_http_get_body(const char *host, int port, const char *path,
                     char *out, size_t cap)
{
    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    return send_request("GET", host, port, path, "", NULL, NULL, out, cap);
}
