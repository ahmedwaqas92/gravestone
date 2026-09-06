#include "http.h"
#include "gravestone.h"
#include "log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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
} stream_t;

static void body_byte(stream_t *st, char c)
{
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

int gs_http_post_stream(const char *host, int port, const char *path,
                        const char *body, gs_http_line_fn on_line,
                        void *context)
{
    struct sockaddr_in address;
    struct timeval patience = {600, 0};
    char request[1024];
    stream_t st;
    int fd, written;

    if (host == NULL || path == NULL || body == NULL || on_line == NULL ||
        port <= 0 || port > 65535)
        return GS_ERR_ARG;
    if (strlen(body) > 512)
        return GS_ERR_ARG;

    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1)
        return GS_ERR_ARG;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return GS_ERR_IO;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof patience);
    if (connect(fd, (struct sockaddr *)&address, sizeof address) != 0) {
        gs_log_debug("http: nothing answering at %s:%d", host, port);
        close(fd);
        return GS_ERR_IO;
    }

    written = snprintf(request, sizeof request,
                       "POST %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n\r\n%s",
                       path, host, strlen(body), body);
    if (written <= 0 || written >= (int)sizeof request) {
        close(fd);
        return GS_ERR_ARG;
    }
    if (write(fd, request, (size_t)written) != (ssize_t)written) {
        close(fd);
        return GS_ERR_IO;
    }

    memset(&st, 0, sizeof st);
    st.in_header = 1;
    st.on_line = on_line;
    st.context = context;

    for (;;) {
        char buffer[4096];
        ssize_t got = read(fd, buffer, sizeof buffer);

        if (got <= 0)
            break;
        feed(&st, buffer, (size_t)got);
        if (st.done || st.stopped)
            break;
    }

    /* A body with no framing ends when the connection does, so whatever
     * sits in the line buffer at that point is the last line. */
    if (!st.chunked && st.line_len > 0 && !st.stopped)
        body_byte(&st, '\n');

    close(fd);

    if (st.broken)
        return GS_ERR;
    if (st.stopped && !st.done)
        return GS_ERR;
    if (st.in_header)
        return GS_ERR_IO;
    return GS_OK;
}
