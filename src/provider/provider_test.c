/* provider_test.c
 *
 * Adversarial. What goes out has to be JSON a server will accept, and
 * what comes back has to be read the way it was written, since a prompt
 * carrying a quote would otherwise end the request early and the rest
 * would be read as part of it.
 *
 * Nothing here needs a server. The one section that does asks only what
 * the server is holding, and reports a skip when nothing answers.
 */
#include "provider.h"
#include "provider_internal.h"
#include "gravestone.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures;
static int checks;
static int skipped;

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

/* A prompt is somebody's words, so every character JSON gives a meaning
 * to has to be written the way JSON expects. */
static void test_escape(void)
{
    char out[256];

    printf("what goes into the request\n");
    check(gs_provider_escape("plain words", out, sizeof out) == GS_OK &&
          strcmp(out, "plain words") == 0, "ordinary words pass through");

    check(gs_provider_escape("say \"hello\"", out, sizeof out) == GS_OK &&
          strcmp(out, "say \\\"hello\\\"") == 0,
          "a quote is written down rather than ending the string");
    check(gs_provider_escape("a\\b", out, sizeof out) == GS_OK &&
          strcmp(out, "a\\\\b") == 0, "and so is a backslash");
    check(gs_provider_escape("one\ntwo", out, sizeof out) == GS_OK &&
          strcmp(out, "one\\ntwo") == 0, "a line break becomes two letters");
    check(gs_provider_escape("a\tb", out, sizeof out) == GS_OK &&
          strcmp(out, "a\\tb") == 0, "and so does a tab");

    /* A control character is refused inside a JSON string, so it goes in
     * as the number JSON writes it with. */
    check(gs_provider_escape("a\x01""b", out, sizeof out) == GS_OK &&
          strcmp(out, "a\\u0001b") == 0,
          "a control character goes in as its number");

    check(gs_provider_escape("", out, sizeof out) == GS_OK && out[0] == '\0',
          "nothing gives nothing");
    check(gs_provider_escape(NULL, out, sizeof out) == GS_ERR_ARG,
          "no text is refused");
    check(gs_provider_escape("x", NULL, 10) == GS_ERR_ARG,
          "nowhere to write is refused");
    check(gs_provider_escape("x", out, 0) == GS_ERR_ARG, "no room is refused");
    {
        char tiny[4];

        check(gs_provider_escape("\"\"\"", tiny, sizeof tiny) == GS_ERR_MEM,
              "text that will not fit once written down is refused");
        check(tiny[0] == '\0', "and leaves nothing half written");
    }
}

static void test_body(void)
{
    char body[1024];

    printf("the request put to the server\n");
    check(gs_provider_body("glm4", "hello", 0.8, 42, 256, body,
                           sizeof body) == GS_OK, "a body is built");
    check(strstr(body, "\"model\":\"glm4\"") != NULL, "it names the model");
    check(strstr(body, "\"content\":\"hello\"") != NULL, "and the prompt");
    check(strstr(body, "\"temperature\":0.800") != NULL,
          "and how much randomness to use");
    check(strstr(body, "\"seed\":42") != NULL,
          "and the number that makes that randomness repeatable");
    check(strstr(body, "\"max_tokens\":256") != NULL,
          "and how much it may write");
    check(strstr(body, "\"stream\":false") != NULL,
          "asking for the whole answer at once rather than in pieces");
    check(strstr(body, "\"role\":\"user\"") != NULL,
          "in the shape the OpenAI format expects");

    /* A prompt carrying a quote would end the string early and the rest
     * would be read as part of the request. */
    check(gs_provider_body("m", "say \"hi\"", 0.0, 1, 8, body,
                           sizeof body) == GS_OK, "a quoted prompt is built");
    check(strstr(body, "\\\"hi\\\"") != NULL,
          "with the quotes written down rather than left to end it");

    check(gs_provider_body(NULL, "x", 0.0, 1, 8, body, sizeof body)
          == GS_ERR_ARG, "no model is refused");
    check(gs_provider_body("m", NULL, 0.0, 1, 8, body, sizeof body)
          == GS_ERR_ARG, "no prompt is refused");
    check(gs_provider_body("m", "x", -1.0, 1, 8, body, sizeof body)
          == GS_ERR_ARG, "randomness below nothing is refused");
    check(gs_provider_body("m", "x", 9.0, 1, 8, body, sizeof body)
          == GS_ERR_ARG, "and far above what any model takes");
    check(gs_provider_body("m", "x", 0.0, 1, 0, body, sizeof body)
          == GS_ERR_ARG, "room for no words at all is refused");
    check(gs_provider_body("m", "x", 0.0, 1, 8, body, 20) == GS_ERR_MEM,
          "a body that will not fit is refused");
}

/* What comes back is read the way it was written, or an answer arrives
 * with its quotes and breaks still spelled out. */
static void test_field(void)
{
    char out[256];
    static const char reply[] =
        "{\"choices\":[{\"message\":{\"role\":\"assistant\","
        "\"content\":\"a clock ticks slower\"}}]}";

    printf("reading what came back\n");
    check(gs_provider_field(reply, "content", out, sizeof out) == GS_OK &&
          strcmp(out, "a clock ticks slower") == 0,
          "the answer is found however deep it sits");
    check(gs_provider_field(reply, "role", out, sizeof out) == GS_OK &&
          strcmp(out, "assistant") == 0, "and so is anything else named");
    check(gs_provider_field(reply, "nothing", out, sizeof out) == GS_ERR,
          "a name that is not there is reported as missing");
    check(out[0] == '\0', "and leaves nothing behind");

    check(gs_provider_field("{\"a\":\"say \\\"hi\\\"\"}", "a", out,
                            sizeof out) == GS_OK &&
          strcmp(out, "say \"hi\"") == 0,
          "a quote written down comes back as a quote");
    check(gs_provider_field("{\"a\":\"one\\ntwo\"}", "a", out,
                            sizeof out) == GS_OK &&
          strcmp(out, "one\ntwo") == 0, "and a break as a break");
    check(gs_provider_field("{\"a\":\"a\\\\b\"}", "a", out, sizeof out)
          == GS_OK && strcmp(out, "a\\b") == 0, "and a backslash as one");

    /* A quote written down inside the text must not be taken for the end
     * of the value, or the answer would be cut in half. */
    check(gs_provider_field("{\"a\":\"he said \\\"no\\\" loudly\"}", "a", out,
                            sizeof out) == GS_OK &&
          strcmp(out, "he said \"no\" loudly") == 0,
          "a written down quote never ends the value early");

    /* Ollama answers a refusal with an object rather than a string, so
     * the name has to be reported as missing rather than read wrongly. */
    check(gs_provider_field("{\"error\":{\"message\":\"gone\"}}", "error",
                            out, sizeof out) == GS_ERR,
          "a name whose value is an object is reported as missing");
    check(gs_provider_field("{\"error\":{\"message\":\"gone\"}}", "message",
                            out, sizeof out) == GS_OK &&
          strcmp(out, "gone") == 0, "and the words inside it are read");

    check(gs_provider_field(NULL, "a", out, sizeof out) == GS_ERR_ARG,
          "no body is refused");
    check(gs_provider_field("{}", NULL, out, sizeof out) == GS_ERR_ARG,
          "no name is refused");
    check(gs_provider_field("{}", "a", NULL, 10) == GS_ERR_ARG,
          "nowhere to write is refused");
    check(gs_provider_field("{\"a\":\"long enough to be cut\"}", "a", out, 5)
          == GS_OK && strlen(out) == 4,
          "an answer larger than the room is cut to what fits");
}

static void test_bad_asks(void)
{
    gs_provider_ask_t ask;
    char out[64];

    printf("questions that make no sense\n");
    memset(&ask, 0, sizeof ask);
    check(gs_provider_ask(NULL, out, sizeof out) == GS_ERR_ARG,
          "no question is refused");
    ask.model = "m";
    ask.prompt = "x";
    check(gs_provider_ask(&ask, NULL, 64) == GS_ERR_ARG,
          "nowhere to put the answer is refused");
    check(gs_provider_ask(&ask, out, 0) == GS_ERR_ARG, "no room is refused");
    ask.model = NULL;
    check(gs_provider_ask(&ask, out, sizeof out) == GS_ERR_ARG,
          "no model is refused");
    ask.model = "";
    check(gs_provider_ask(&ask, out, sizeof out) == GS_ERR_ARG,
          "a model with no name is refused");
    ask.model = "m";
    ask.prompt = NULL;
    check(gs_provider_ask(&ask, out, sizeof out) == GS_ERR_ARG,
          "and no prompt is refused");
}

static void test_server(void)
{
    char names[GS_PROVIDER_MAX][GS_PROVIDER_MODEL];
    int n;

    printf("the server itself\n");
    if (!gs_provider_available()) {
        printf("  skip  nothing answering on %s:%d\n",
               GS_PROVIDER_HOST, GS_PROVIDER_PORT);
        skipped++;
        check(gs_provider_models(names, GS_PROVIDER_MAX) == 0,
              "and it holds nothing this program can see");
        return;
    }

    n = gs_provider_models(names, GS_PROVIDER_MAX);
    printf("        %d model%s holding\n", n, n == 1 ? "" : "s");
    check(n >= 0, "the listing came back");
    if (n > 0)
        check(names[0][0] != '\0', "and the first one carries a name");
    check(gs_provider_models(NULL, 4) == 0, "nowhere to write is refused");
    check(gs_provider_models(names, 0) == 0, "and no room for any");

    /* A model that does not exist is a refusal rather than an empty
     * answer, since the two mean different things to the harness. */
    {
        gs_provider_ask_t ask;
        char answer[256];

        memset(&ask, 0, sizeof ask);
        ask.model = "zz-no-such-model-xyzzy:1b";
        ask.prompt = "hello";
        ask.temperature = 0.0;
        ask.seed = 1;
        ask.limit = 8;
        check(gs_provider_ask(&ask, answer, sizeof answer) != GS_OK,
              "a model that does not exist is refused, not answered");
        check(answer[0] == '\0', "with nothing left in the answer");
    }
}

/* A model that writes its working out before its answer can spend the
 * whole allowance thinking, handing back nothing while having written a
 * great deal. Telling that apart from a model with nothing to say is what
 * lets the harness ask again with more room. */
static void test_why_it_stopped(void)
{
    gs_provider_note_t note;
    char answer[256];
    gs_provider_ask_t ask;

    printf("why a model stopped writing\n");

    memset(&note, 0, sizeof note);
    memset(&ask, 0, sizeof ask);
    ask.model = "zz-no-such-model-xyzzy:1b";
    ask.prompt = "hello";
    ask.limit = 8;

    /* The note is optional, so the plain form still works. */
    check(gs_provider_ask(&ask, answer, sizeof answer) != GS_OK ||
          answer[0] == '\0', "the plain form still answers");
    check(gs_provider_ask_noted(&ask, answer, sizeof answer, NULL) != GS_OK ||
          answer[0] == '\0', "and so does the noted form with no note");

    check(gs_provider_ask_noted(NULL, answer, sizeof answer, &note)
          != GS_OK, "no question is refused");
    check(gs_provider_ask_noted(&ask, NULL, sizeof answer, &note)
          != GS_OK, "and nowhere to put the answer");

    /* The three reasons are three different values, so a caller can tell
     * a finished answer from one cut short. */
    check(GS_PROVIDER_STOP_UNKNOWN != GS_PROVIDER_STOP_DONE,
          "not knowing why is not the same as finishing");
    check(GS_PROVIDER_STOP_DONE != GS_PROVIDER_STOP_LIMIT,
          "and finishing is not the same as running out");
    check((int)GS_PROVIDER_STOP_UNKNOWN == 0,
          "a zeroed note says the reason is unknown");
}

static long long now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Bringing the inference server up. Nothing here starts one, because
 * this test runs under `make check` after every edit, and a test that
 * started a server would leave one running on the machine of whoever
 * typed it. Only the paths that refuse or that find one already up are
 * worked. */
static void test_starting_the_server(void)
{
    printf("bringing the server up\n");

    /* Before anything has been started there is no log to read, and the
     * answer is an empty string rather than a null pointer, so a caller
     * printing it cannot crash. */
    check(gs_provider_log_path() != NULL,
          "the log path is always a real string");

    if (gs_provider_available()) {
        long long before, after;

        /* A server already answering is left alone. Asking the port
         * rather than looking for a program of that name is what keeps a
         * second one from being started, and it is what makes a server
         * somebody else started count as theirs. */
        before = now_ms();
        check(gs_provider_start(0) == GS_OK,
              "a server already answering is reported as ready");
        after = now_ms();
        check(after - before < 1000,
              "and that answer costs no waiting at all");

        check(gs_provider_start(30) == GS_OK,
              "asking again changes nothing");
        check(now_ms() - after < 1000,
              "and still returns at once rather than waiting the thirty");
        printf("        a server is up on this machine, so the leaving "
               "alone was checked\n");
    } else {
        printf("  skip  the server paths (none is running, and this test "
               "will not start one)\n");
        skipped += 2;
    }
}

/* The standing instructions ride in front of the question as a message
 * of their own, so the model reads them as who it is rather than as
 * something the person typed. */
static void test_system_message(void)
{
    char body[4096];
    const char *sys_at;
    const char *user_at;

    printf("the standing instructions\n");

    check(gs_provider_body_system("qwen3:4b", "You are the Gravestone agent.",
                                  "Who are you?", 0.0, 1, 2048, body,
                                  sizeof body) == GS_OK,
          "a request with a system message is built");
    sys_at = strstr(body, "{\"role\":\"system\",\"content\":\"You are the "
                          "Gravestone agent.\"}");
    user_at = strstr(body, "{\"role\":\"user\",\"content\":\"Who are you?\"}");
    check(sys_at != NULL, "the system message is there, whole");
    check(user_at != NULL, "and so is the question");
    check(sys_at != NULL && user_at != NULL && sys_at < user_at,
          "and the instructions come first");
    check(strstr(body, "\"max_tokens\":2048") != NULL,
          "with the allowance asked for");

    /* A quote inside the instructions cannot end the string early. */
    check(gs_provider_body_system("m", "say \"hi\"", "x", 0.0, 1, 8, body,
                                  sizeof body) == GS_OK,
          "instructions carrying quotes are built");
    check(strstr(body, "say \\\"hi\\\"") != NULL,
          "with the quotes escaped");

    /* No instructions sends exactly what the plain form sends. */
    {
        char plain[4096];

        gs_provider_body("m", "x", 0.0, 1, 8, plain, sizeof plain);
        gs_provider_body_system("m", NULL, "x", 0.0, 1, 8, body, sizeof body);
        check(strcmp(plain, body) == 0,
              "no system message gives the plain request byte for byte");
        gs_provider_body_system("m", "", "x", 0.0, 1, 8, body, sizeof body);
        check(strcmp(plain, body) == 0, "and so does an empty one");
        check(strstr(body, "\"system\"") == NULL,
              "with no system role in it at all");
    }

    check(gs_provider_body_system("m", "a long instruction", "x", 0.0, 1, 8,
                                  body, 40) == GS_ERR_MEM,
          "a buffer too small refuses rather than sending half");
}

/* The full form hands back the working out. With no server it has to
 * leave every buffer empty rather than untouched. */
static void test_ask_full_arguments(void)
{
    gs_provider_ask_t ask;
    gs_provider_note_t note;
    char out[64];
    char working[64];

    printf("asking for the working out\n");
    memset(&ask, 0, sizeof ask);
    ask.model = "m";
    ask.prompt = "x";
    ask.limit = 8;
    out[0] = 'x';
    working[0] = 'x';
    memset(&note, 0x55, sizeof note);
    check(gs_provider_ask_full(NULL, out, sizeof out, working, sizeof working,
                               &note) == GS_ERR_ARG, "no question is refused");
    check(gs_provider_ask_full(&ask, NULL, 8, working, sizeof working,
                               &note) == GS_ERR_ARG, "and nowhere for the answer");
    check(gs_provider_ask_full(&ask, out, sizeof out, NULL, 0, NULL)
          != GS_ERR_ARG, "no room for the working out is allowed");
    check(working[0] == 'x' || working[0] == '\0',
          "and a buffer never handed over is never written");
}

/* The conversation goes to the model in the order it was said, since a
 * model keeps nothing between one request and the next. */
static void test_history_body(void)
{
    static char body[8192];
    gs_provider_turn_t turns[2];
    const char *sys_at;
    const char *first_at;
    const char *reply_at;
    const char *second_at;
    const char *now_at;

    printf("what was said before\n");

    turns[0].prompt = "My name is Waqas.";
    turns[0].answer = "Hello Waqas.";
    turns[1].prompt = "I write C.";
    turns[1].answer = "Noted, \"C\".";

    check(gs_provider_body_turns("qwen3:4b", "You are the agent.", turns, 2,
                                 "What is my name?", 0.0, 1, 2048, body,
                                 sizeof body) == GS_OK,
          "a request carrying two earlier exchanges is built");
    sys_at = strstr(body, "{\"role\":\"system\",\"content\":\"You are the "
                          "agent.\"}");
    first_at = strstr(body, ",{\"role\":\"user\",\"content\":\"My name is "
                            "Waqas.\"}");
    reply_at = strstr(body, ",{\"role\":\"assistant\",\"content\":\"Hello "
                            "Waqas.\"}");
    second_at = strstr(body, "{\"role\":\"assistant\",\"content\":\"Noted, "
                             "\\\"C\\\".\"}");
    now_at = strstr(body, ",{\"role\":\"user\",\"content\":\"What is my "
                          "name?\"}]");
    check(sys_at != NULL && first_at != NULL && reply_at != NULL &&
          second_at != NULL && now_at != NULL,
          "with every message present and escaped");
    check(sys_at < first_at && first_at < reply_at && reply_at < second_at &&
          second_at < now_at,
          "in the order it was said, the question last");
    printf("        %s\n", body);

    check(gs_provider_body_turns("m", NULL, turns, 1, "x", 0.0, 1, 8, body,
                                 sizeof body) == GS_OK &&
          strncmp(body, "{\"model\":\"m\",\"messages\":[{\"role\":\"user\"",
                  39) == 0,
          "with no system message the first exchange opens the list");

    {
        static char plain[8192];

        (void)gs_provider_body_system("m", "s", "x", 0.5, 3, 64, plain,
                                      sizeof plain);
        (void)gs_provider_body_turns("m", "s", NULL, 0, "x", 0.5, 3, 64,
                                     body, sizeof body);
        check(strcmp(plain, body) == 0,
              "no earlier exchanges sends exactly what was sent before");
    }

    check(gs_provider_body_turns("m", NULL, NULL, 2, "x", 0.0, 1, 8, body,
                                 sizeof body) == GS_ERR_ARG,
          "exchanges promised and not given are refused");
    check(gs_provider_body_turns("m", NULL, turns, 2, "x", 0.0, 1, 8, body,
                                 60) == GS_ERR_MEM && body[0] == '\0',
          "a conversation that will not fit is refused rather than cut");
}

int main(void)
{
    printf("provider\n\n");

    test_escape();
    test_body();
    test_system_message();
    test_history_body();
    test_field();
    test_bad_asks();
    test_ask_full_arguments();
    test_server();
    test_starting_the_server();

    test_why_it_stopped();
    printf("\n%d checks, %d failures, %d skipped\n", checks, failures, skipped);
    return failures == 0 ? 0 : 1;
}
