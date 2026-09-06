/* window_x11_conn.c
 *
 * The socket underneath everything else. An X server listens on
 * /tmp/.X11-unix/X<n>. A client opens that socket, sends a twelve byte
 * greeting, and reads back a description of the screen. Every request after
 * that is a short packet written to the same socket, and everything the
 * server has to say arrives as fixed thirty two byte packets.
 */
#include "window_internal.h"
#include "gravestone.h"
#include "log.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void gs_win_put16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)(v >> 8);
}

void gs_win_put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

uint16_t gs_win_get16(const unsigned char *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t gs_win_get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int gs_win_pad4(int n)
{
    return (4 - (n & 3)) & 3;
}

int gs_win_write_all(struct gs_window *w, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = write(w->fd, p + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            gs_log_error("window: write failed: %s", strerror(errno));
            w->broken = 1;
            return GS_ERR_IO;
        }
        sent += (size_t)n;
    }
    return GS_OK;
}

int gs_win_read_all(struct gs_window *w, void *buf, size_t len)
{
    unsigned char *p = buf;
    size_t got = 0;

    while (got < len) {
        ssize_t n = read(w->fd, p + got, len - got);
        if (n == 0) {
            gs_log_error("window: the server closed the connection");
            w->broken = 1;
            return GS_ERR_IO;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            gs_log_error("window: read failed: %s", strerror(errno));
            w->broken = 1;
            return GS_ERR_IO;
        }
        got += (size_t)n;
    }
    return GS_OK;
}

/* The server numbers requests as it receives them, and errors quote that
 * number, so the counter has to move in step with every write. */
int gs_win_send_request(struct gs_window *w, const void *buf, size_t len)
{
    w->sequence++;
    return gs_win_write_all(w, buf, len);
}

int gs_win_display_number(void)
{
    const char *display = getenv("DISPLAY");
    const char *colon;

    if (display == NULL || display[0] == '\0') {
        gs_log_error("window: DISPLAY is not set, so there is no screen to "
                     "draw on");
        return -1;
    }
    colon = strchr(display, ':');
    if (colon == NULL) {
        gs_log_error("window: DISPLAY is '%s', which has no colon in it",
                     display);
        return -1;
    }
    return atoi(colon + 1);
}

int gs_win_connect(int display_number)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        gs_log_error("window: socket failed: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "/tmp/.X11-unix/X%d",
             display_number);

    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        /* A compositor under load refuses connections for a moment, so
         * this is reported at debug level and judged by the caller. */
        gs_log_debug("window: cannot reach the display at %s: %s",
                     addr.sun_path, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* Reads the block the server sends after a successful greeting and picks
 * out the few fields this module needs. */
static int parse_setup(struct gs_window *w, const unsigned char *data,
                       size_t len)
{
    size_t offset;
    uint16_t vendor_len;
    uint8_t  screen_count, format_count;

    if (len < 32)
        return GS_ERR;

    w->id_base = gs_win_get32(data + 4);
    w->id_mask = gs_win_get32(data + 8);
    vendor_len = gs_win_get16(data + 16);
    screen_count = data[20];
    format_count = data[21];
    w->image_byte_order = data[22];
    w->min_keycode = data[26];
    w->max_keycode = data[27];

    /* The limit arrives in four byte units. Anything larger has to be split
     * across several requests. */
    w->max_request_bytes = (uint32_t)gs_win_get16(data + 18) * 4u;
    if (w->max_request_bytes < 4096u)
        w->max_request_bytes = 4096u;

    if (screen_count == 0) {
        gs_log_error("window: the server reports no screens");
        return GS_ERR;
    }

    /* The fixed part runs to 32 bytes, then the vendor string padded out to
     * a multiple of four, then eight bytes for every pixel format. The
     * screen records follow that. */
    offset = 32u + vendor_len + (size_t)gs_win_pad4((int)vendor_len) +
             8u * format_count;
    if (offset + 40 > len) {
        gs_log_error("window: the setup block is shorter than it claims");
        return GS_ERR;
    }

    w->root        = gs_win_get32(data + offset + 0);
    w->root_visual = gs_win_get32(data + offset + 32);
    w->root_depth  = data[offset + 38];

    if (w->root_depth == 0) {
        gs_log_error("window: the screen reports a depth of zero");
        return GS_ERR;
    }

    gs_log_debug("window: root 0x%x visual 0x%x depth %u screens %u",
                 w->root, w->root_visual, w->root_depth, screen_count);
    gs_log_debug("window: max request %u bytes, byte order %s",
                 w->max_request_bytes,
                 w->image_byte_order ? "most significant first"
                                     : "least significant first");
    return GS_OK;
}

int gs_win_handshake(struct gs_window *w)
{
    unsigned char request[12];
    unsigned char header[8];
    unsigned char *body;
    size_t body_len;
    int rc;

    memset(request, 0, sizeof request);
    request[0] = 0x6c;          /* 'l', meaning the client is little endian */
    gs_win_put16(request + 2, 11); /* protocol major */
    gs_win_put16(request + 4, 0);  /* protocol minor */
    gs_win_put16(request + 6, 0);  /* no authorisation protocol name */
    gs_win_put16(request + 8, 0);  /* no authorisation data */

    if (gs_win_write_all(w, request, sizeof request) != GS_OK)
        return GS_ERR_IO;
    if (gs_win_read_all(w, header, sizeof header) != GS_OK)
        return GS_ERR_IO;

    body_len = (size_t)gs_win_get16(header + 6) * 4u;

    if (header[0] != 1) {
        char reason[256];
        size_t n = header[1] < sizeof reason - 1 ? header[1]
                                                 : sizeof reason - 1;
        memset(reason, 0, sizeof reason);
        if (body_len > 0 && gs_win_read_all(w, reason, n) == GS_OK)
            gs_log_error("window: the server refused the connection: %s",
                         reason);
        else
            gs_log_error("window: the server refused the connection");
        return GS_ERR;
    }

    if (body_len == 0) {
        gs_log_error("window: the server sent an empty setup block");
        return GS_ERR;
    }

    body = malloc(body_len);
    if (body == NULL)
        return GS_ERR_MEM;

    if (gs_win_read_all(w, body, body_len) != GS_OK) {
        free(body);
        return GS_ERR_IO;
    }

    rc = parse_setup(w, body, body_len);
    free(body);
    return rc;
}

/* ---- packets coming back ---- */

static void queue_push(struct gs_window *w, const unsigned char *packet)
{
    int slot;

    if (w->queue_len == WIN_EVENT_QUEUE_LEN) {
        gs_log_warn("window: event queue full, dropping the oldest");
        w->queue_head = (w->queue_head + 1) % WIN_EVENT_QUEUE_LEN;
        w->queue_len--;
    }
    slot = (w->queue_head + w->queue_len) % WIN_EVENT_QUEUE_LEN;
    memcpy(w->queue[slot], packet, 32);
    w->queue_len++;
}

int gs_win_queue_pop(struct gs_window *w, unsigned char *out)
{
    if (w->queue_len == 0)
        return 0;
    memcpy(out, w->queue[w->queue_head], 32);
    w->queue_head = (w->queue_head + 1) % WIN_EVENT_QUEUE_LEN;
    w->queue_len--;
    return 1;
}

static void report_error(struct gs_window *w, const unsigned char *packet)
{
    w->errors++;
    gs_log_error("window: protocol error %u on request %u (major opcode %u, "
                 "minor %u, bad value 0x%x)",
                 packet[1], gs_win_get16(packet + 2), packet[10],
                 gs_win_get16(packet + 8), gs_win_get32(packet + 4));
}

/* Reads the bytes trailing a reply, copying what fits into data_out and
 * discarding the rest so the socket stays in step with the protocol. */
static int drain_extra(struct gs_window *w, uint32_t extra,
                       unsigned char *data_out, size_t data_cap,
                       size_t *data_len)
{
    unsigned char scratch[512];
    size_t kept = 0;
    uint32_t left = extra;

    if (data_out != NULL && data_cap > 0) {
        kept = data_cap < extra ? data_cap : extra;
        if (gs_win_read_all(w, data_out, kept) != GS_OK)
            return GS_ERR_IO;
        left -= (uint32_t)kept;
    }
    if (data_len != NULL)
        *data_len = kept;

    while (left > 0) {
        size_t chunk = left < sizeof scratch ? left : sizeof scratch;
        if (gs_win_read_all(w, scratch, chunk) != GS_OK)
            return GS_ERR_IO;
        left -= (uint32_t)chunk;
    }
    return GS_OK;
}

/* Pulls one thirty two byte packet off the socket. Errors are counted,
 * events are queued, and a reply is handed back through reply_out along
 * with any trailing bytes it carries. */
static int read_packet_full(struct gs_window *w, unsigned char *reply_out,
                            unsigned char *data_out, size_t data_cap,
                            size_t *data_len)
{
    unsigned char packet[32];

    if (gs_win_read_all(w, packet, 32) != GS_OK)
        return GS_ERR_IO;

    if (packet[0] == X_REPLY_ERROR) {
        report_error(w, packet);
        return X_REPLY_ERROR;
    }

    if (packet[0] == X_REPLY_OK) {
        uint32_t extra = gs_win_get32(packet + 4) * 4u;
        if (extra > 0 && drain_extra(w, extra, data_out, data_cap,
                                     data_len) != GS_OK)
            return GS_ERR_IO;
        if (reply_out != NULL)
            memcpy(reply_out, packet, 32);
        return X_REPLY_OK;
    }

    queue_push(w, packet);
    return X_REPLY_EVENT;
}

/* Blocks until a reply or an error turns up, keeping any events that arrive
 * in the meantime. */
int gs_win_read_packet(struct gs_window *w, unsigned char *reply_out)
{
    return read_packet_full(w, reply_out, NULL, 0, NULL);
}

int gs_win_await_reply_data(struct gs_window *w, unsigned char *reply_out,
                         unsigned char *data_out, size_t data_cap,
                         size_t *data_len)
{
    for (;;) {
        int rc = read_packet_full(w, reply_out, data_out, data_cap, data_len);
        if (rc == X_REPLY_OK)
            return GS_OK;
        if (rc == X_REPLY_ERROR)
            return GS_ERR;
        if (rc < 0)
            return rc;
    }
}

int gs_win_await_reply(struct gs_window *w, unsigned char *reply_out)
{
    return gs_win_await_reply_data(w, reply_out, NULL, 0, NULL);
}
