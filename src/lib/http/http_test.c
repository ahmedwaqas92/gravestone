/* http_test.c
 *
 * The line streamer against a server of this test's own making, which
 * answers each connection with whatever framing the case calls for,
 * including framing built to hurt.
 */
#include "http.h"
#include "gravestone.h"
#include "log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int failures;
static int checks;

static void check(int condition, const char *what)
{
    checks++;
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

/* One throwaway server. It listens on a port the system picks, serves a
 * single connection with a canned reply, and is gone. */
static const char *canned;
static int serve_port;

static void *serve_once(void *arg)
{
    int fd = (int)(long)arg;
    int client = accept(fd, NULL, NULL);
    char sink[2048];

    if (client >= 0) {
        ssize_t got = read(client, sink, sizeof sink);
        size_t i;

        (void)got;
        /* Sent one byte at a time, so every fragment boundary the parser
         * could mishandle is exercised on every run. */
        for (i = 0; i < strlen(canned); i++)
            if (write(client, canned + i, 1) != 1)
                break;
        close(client);
    }
    close(fd);
    return NULL;
}

static pthread_t start_server(const char *reply)
{
    struct sockaddr_in address;
    socklen_t len = sizeof address;
    pthread_t thread;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    canned = reply;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (struct sockaddr *)&address, sizeof address);
    getsockname(fd, (struct sockaddr *)&address, &len);
    serve_port = (int)ntohs(address.sin_port);
    listen(fd, 1);
    pthread_create(&thread, NULL, serve_once, (void *)(long)fd);
    return thread;
}

static char seen[16][256];
static int seen_count;

static int collect(const char *line, void *context)
{
    (void)context;
    if (seen_count < 16) {
        strncpy(seen[seen_count], line, sizeof seen[0] - 1);
        seen[seen_count][sizeof seen[0] - 1] = '\0';
        seen_count++;
    }
    return 0;
}

static int stop_at_two(const char *line, void *context)
{
    (void)line;
    (void)context;
    return ++seen_count >= 2;
}

static void test_chunked(void)
{
    pthread_t t = start_server(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/x-ndjson\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "10\r\n{\"first\":1}\n{\"se\r\n"
        "0a\r\ncond\":22}\n\r\n"
        "0\r\n\r\n");
    int rc;

    printf("a chunked stream cut at awkward places\n");
    seen_count = 0;
    rc = gs_http_post_stream("127.0.0.1", serve_port, "/x", "{}",
                             collect, NULL);
    pthread_join(t, NULL);
    check(rc == GS_OK, "the stream ended cleanly");
    check(seen_count == 2, "two lines arrived");
    check(seen_count == 2 && strcmp(seen[0], "{\"first\":1}") == 0,
          "the first line survived its chunk boundary");
    check(seen_count == 2 && strcmp(seen[1], "{\"second\":22}") == 0,
          "the second line was stitched across two chunks");
}

static void test_unchunked(void)
{
    pthread_t t = start_server(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n\r\n"
        "alpha\nbeta");
    int rc;

    printf("a plain body ending with the connection\n");
    seen_count = 0;
    rc = gs_http_post_stream("127.0.0.1", serve_port, "/x", "{}",
                             collect, NULL);
    pthread_join(t, NULL);
    check(rc == GS_OK, "the stream ended cleanly");
    check(seen_count == 2 && strcmp(seen[1], "beta") == 0,
          "the last line without a newline still arrived");
}

/* The decoder fed framing built to hurt. Garbage where the hex length
 * belongs, a length claiming more than any chunk carries, a stream cut
 * mid chunk, and a line longer than any sane report. None may hang, none
 * may hand the caller a torn line, and every case must return. */
static void test_hostile_framing(void)
{
    pthread_t t;
    int rc, i;
    static char big[9000];

    printf("framing built to hurt\n");

    t = start_server(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "zzzz\r\nrubbish");
    seen_count = 0;
    rc = gs_http_post_stream("127.0.0.1", serve_port, "/x", "{}",
                             collect, NULL);
    pthread_join(t, NULL);
    check(rc != GS_OK, "a length that is no hex number fails cleanly");
    check(seen_count == 0, "and no torn line reached the caller");

    t = start_server(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "7fffffff\r\nnope");
    seen_count = 0;
    rc = gs_http_post_stream("127.0.0.1", serve_port, "/x", "{}",
                             collect, NULL);
    pthread_join(t, NULL);
    check(rc != GS_OK, "a length past any sane chunk fails cleanly");

    t = start_server(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "40\r\ncut off long before sixty four bytes");
    seen_count = 0;
    rc = gs_http_post_stream("127.0.0.1", serve_port, "/x", "{}",
                             collect, NULL);
    pthread_join(t, NULL);
    check(rc == GS_OK || rc == GS_ERR,
          "a stream dying mid chunk still returns");
    check(seen_count == 0, "and its torn line was not delivered");

    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    {
        static char reply[10000];

        snprintf(reply, sizeof reply,
                 "HTTP/1.1 200 OK\r\n\r\n%s\nshort\n", big);
        t = start_server(reply);
    }
    seen_count = 0;
    rc = gs_http_post_stream("127.0.0.1", serve_port, "/x", "{}",
                             collect, NULL);
    pthread_join(t, NULL);
    check(rc == GS_OK, "a nine thousand byte line does not kill the stream");
    check(seen_count == 1 && strcmp(seen[0], "short") == 0,
          "the giant line was dropped whole and the next arrived");

    t = start_server(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "06\r\none\ntw\r\n06\r\no\nthr\r\n0\r\n\r\n");
    seen_count = 0;
    rc = gs_http_post_stream("127.0.0.1", serve_port, "/x", "{}",
                             stop_at_two, NULL);
    pthread_join(t, NULL);
    check(seen_count == 2, "a callback asking to stop stops the stream");

    for (i = 0; i < 1; i++) {
        rc = gs_http_post_stream("127.0.0.1", 1, "/x", "{}", collect, NULL);
        check(rc != GS_OK, "a port with nobody behind it fails cleanly");
    }
    check(gs_http_post_stream(NULL, 80, "/", "{}", collect, NULL)
          == GS_ERR_ARG, "a null host is refused");
    check(gs_http_post_stream("not-an-address", 80, "/", "{}", collect,
                              NULL) == GS_ERR_ARG,
          "a host that is no address is refused");
}

int main(void)
{
    gs_log_set_level(GS_LOG_ERROR);

    test_chunked();
    test_unchunked();
    test_hostile_framing();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
