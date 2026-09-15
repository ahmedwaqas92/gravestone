/* provider.c
 *
 * The asking itself. One question goes out as one request, and the whole
 * reply is gathered before anything is read out of it, since the body is
 * one piece of JSON rather than a stream of pieces.
 */
#include "provider.h"
#include "provider_internal.h"
#include "gravestone.h"
#include "http.h"
#include "log.h"
#include "paths.h"
#include "proc.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>


/* Puts a request and hands back the whole body. A body of nothing asks
 * rather than sends, which is what the listing wants. */
static int fetch(const char *path, const char *body, char *out, size_t cap)
{
    int rc;

    /* The body is taken whole rather than line by line. A reply from the
     * server is one long line of JSON, and the line form cuts at a fixed
     * width and throws away whatever falls past it, which turned a real
     * answer into silence. */
    if (body == NULL || body[0] == '\0')
        rc = gs_http_get_body(GS_PROVIDER_HOST, GS_PROVIDER_PORT, path,
                              out, cap);
    else
        rc = gs_http_post_body(GS_PROVIDER_HOST, GS_PROVIDER_PORT, path,
                               body, out, cap);
    return rc == GS_OK ? GS_OK : GS_ERR;
}

int gs_provider_available(void)
{
    char *reply;
    int up;

    /* Asking what it holds is the cheapest question the server answers,
     * and an answer at all means it is up. The whole listing is taken,
     * since a reader that fills its buffer stops the stream early and
     * that reads as a failure rather than as an answer. */
    reply = malloc(1 << 16);
    if (reply == NULL)
        return 0;
    up = fetch("/api/tags", "", reply, 1u << 16) == GS_OK &&
         strstr(reply, "models") != NULL;
    free(reply);
    return up;
}

/* Where the server program might live. Each is tried in turn and the
 * first one really there is the one that runs. Nothing is looked up on
 * the search path, since under WSL most of that path sits on the Windows
 * drive where another program can write. */
static const char *const server_paths[] = {
    "/usr/local/bin/ollama",
    "/usr/bin/ollama",
    "/bin/ollama",
    "/opt/homebrew/bin/ollama",
    "/usr/local/opt/ollama/bin/ollama"
};
#define SERVER_COUNT ((int)(sizeof server_paths / sizeof server_paths[0]))

static char server_log[512];

const char *gs_provider_log_path(void)
{
    return server_log;
}

static const char *find_server(void)
{
    struct stat where;
    int i;

    for (i = 0; i < SERVER_COUNT; i++)
        if (stat(server_paths[i], &where) == 0 && S_ISREG(where.st_mode) &&
            (where.st_mode & S_IXUSR) != 0)
            return server_paths[i];
    return NULL;
}

int gs_provider_start(int wait_seconds)
{
    const char *server;
    const char *argv[3];
    char dir[400];
    int waited;

    /* A server already answering is left alone, which is the ordinary
     * case. Asking the port rather than looking for a program of that
     * name is what keeps this from starting a second one, and it is also
     * what makes a server somebody else started count. */
    if (gs_provider_available())
        return GS_OK;

    server = find_server();
    if (server == NULL) {
        gs_log_warn("provider: no inference server is installed, so "
                    "nothing can be asked");
        return GS_ERR;
    }

    if (gs_paths_data_dir(dir, sizeof dir) == GS_OK)
        snprintf(server_log, sizeof server_log, "%s/server.log", dir);
    else
        server_log[0] = '\0';

    argv[0] = server;
    argv[1] = "serve";
    argv[2] = NULL;
    if (gs_proc_spawn(server, argv,
                      server_log[0] != '\0' ? server_log : NULL) != GS_OK) {
        gs_log_warn("provider: %s would not start", server);
        return GS_ERR;
    }
    gs_log_info("provider: started %s, watching %s:%d", server,
                GS_PROVIDER_HOST, GS_PROVIDER_PORT);

    if (wait_seconds <= 0)
        return GS_OK;

    /* Loading the weights takes a few seconds, so the port is asked
     * again on a short clock rather than once. */
    for (waited = 0; waited < wait_seconds * 4; waited++) {
        struct timespec quarter;

        quarter.tv_sec = 0;
        quarter.tv_nsec = 250000000L;
        nanosleep(&quarter, NULL);
        if (gs_provider_available()) {
            gs_log_info("provider: the server answered after about %d "
                        "second%s", (waited + 1) / 4,
                        (waited + 1) / 4 == 1 ? "" : "s");
            return GS_OK;
        }
    }

    gs_log_warn("provider: the server did not answer within %d seconds",
                wait_seconds);
    return GS_ERR;
}

int gs_provider_models(char names[][GS_PROVIDER_MODEL], int max)
{
    char *reply;
    const char *at;
    int found = 0;

    if (names == NULL || max <= 0)
        return 0;

    reply = malloc(1 << 16);
    if (reply == NULL)
        return 0;
    if (fetch("/api/tags", "", reply, 1u << 16) != GS_OK) {
        free(reply);
        return 0;
    }

    /* Every model in the listing carries a name, and they arrive in the
     * order the server keeps them. */
    at = reply;
    while (found < max) {
        const char *key = strstr(at, "\"name\"");

        if (key == NULL)
            break;
        if (gs_provider_field(key, "name", names[found],
                              GS_PROVIDER_MODEL) == GS_OK &&
            names[found][0] != '\0')
            found++;
        at = key + 6;
    }
    free(reply);
    return found;
}

int gs_provider_ask_noted(const gs_provider_ask_t *ask, char *out,
                          size_t cap, gs_provider_note_t *note)
{
    return gs_provider_ask_full(ask, out, cap, NULL, 0, note);
}

int gs_provider_ask_full(const gs_provider_ask_t *ask, char *out, size_t cap,
                         char *working, size_t working_cap,
                         gs_provider_note_t *note)
{
    char *body;
    char *reply;
    int rc;

    if (ask == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (ask->model == NULL || ask->model[0] == '\0' || ask->prompt == NULL)
        return GS_ERR_ARG;

    body = malloc(1 << 16);
    reply = malloc(1 << 20);
    if (body == NULL || reply == NULL) {
        free(body);
        free(reply);
        return GS_ERR_MEM;
    }

    rc = gs_provider_body_turns(ask->model, ask->system, ask->history,
                                ask->history_count, ask->prompt,
                                ask->temperature, ask->seed,
                                ask->limit > 0 ? ask->limit : 512,
                                body, 1u << 16);
    if (rc != GS_OK) {
        free(body);
        free(reply);
        return rc;
    }

    rc = fetch("/v1/chat/completions", body, reply, 1u << 20);
    free(body);
    if (rc != GS_OK) {
        gs_log_warn("provider: %s did not answer", ask->model);
        free(reply);
        return GS_ERR;
    }

    /* A server that refused says so, and which refusal it was carries
     * more use than a bare failure. The reason sits inside an object of
     * its own, so the message within it is what gets read. */
    if (strstr(reply, "\"error\"") != NULL) {
        char trouble[256];

        if (gs_provider_field(reply, "message", trouble,
                              sizeof trouble) != GS_OK)
            gs_str_copy(trouble, sizeof trouble, "no reason given");
        gs_log_warn("provider: %s refused: %s", ask->model, trouble);
        free(reply);
        return GS_ERR;
    }

    /* An answer of nothing is a real outcome the harness records, so an
     * absent content field is reported as an empty answer rather than as
     * a failure to reach the server. */
    if (gs_provider_field(reply, "content", out, cap) != GS_OK)
        out[0] = '\0';

    if (working != NULL && working_cap > 0)
        working[0] = '\0';

    if (note != NULL || working != NULL) {
        char *thought = malloc(1u << 16);

        /* The working out is read whole into room of its own, so its
         * length is the real one rather than the size of a small buffer
         * it was cut to fit. A person asking how an answer was arrived at
         * reads it, so it is handed on when there is somewhere to put
         * it. */
        if (thought != NULL) {
            if (gs_provider_field(reply, "reasoning", thought, 1u << 16)
                != GS_OK)
                thought[0] = '\0';
            if (working != NULL && working_cap > 0)
                gs_str_copy(working, working_cap, thought);
        }
        if (note != NULL) {
            note->answer_len = (int)strlen(out);
            note->working_len = thought != NULL ? (int)strlen(thought) : 0;
            if (note->working_len == 0 &&
                strstr(reply, "\"reasoning\"") != NULL)
                note->working_len = 1;
        }
        free(thought);
    }

    if (note != NULL) {
        char why[32];

        note->stop = GS_PROVIDER_STOP_UNKNOWN;
        if (gs_provider_field(reply, "finish_reason", why, sizeof why)
            == GS_OK) {
            if (strcmp(why, "length") == 0)
                note->stop = GS_PROVIDER_STOP_LIMIT;
            else
                note->stop = GS_PROVIDER_STOP_DONE;
        }
    }
    free(reply);
    return GS_OK;
}

int gs_provider_ask(const gs_provider_ask_t *ask, char *out, size_t cap)
{
    return gs_provider_ask_noted(ask, out, cap, NULL);
}

/* ---- what the command line shows ---- */

static int provider_init(void)
{
    return GS_OK;
}

static int provider_run(int argc, char **argv)
{
    char names[GS_PROVIDER_MAX][GS_PROVIDER_MODEL];
    int n;
    int i;

    (void)argc;
    (void)argv;

    if (!gs_provider_available()) {
        printf("no inference server answering on %s:%d, starting one\n",
               GS_PROVIDER_HOST, GS_PROVIDER_PORT);
        if (gs_provider_start(30) != GS_OK) {
            printf("none could be started, so nothing can be asked\n");
            if (gs_provider_log_path()[0] != '\0')
                printf("what it said is in %s\n", gs_provider_log_path());
            return GS_ERR;
        }
        printf("the server is answering now\n");
    }

    n = gs_provider_models(names, GS_PROVIDER_MAX);
    printf("%d model%s ready to answer\n", n, n == 1 ? "" : "s");
    for (i = 0; i < n; i++)
        printf("  %s\n", names[i]);
    return GS_OK;
}

static void provider_shutdown(void)
{
}

const gs_module gs_provider_module = {
    "provider",
    "list the models an inference server is holding",
    provider_init,
    provider_run,
    provider_shutdown
};
