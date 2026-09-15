/* provider_json.c
 *
 * The small amount of JSON this program needs, written here rather than
 * taken from a library, since the shape of what goes out and what comes
 * back is known and fixed.
 */
#include "provider.h"
#include "provider_internal.h"
#include "gravestone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gs_provider_escape(const char *text, char *out, size_t cap)
{
    size_t at = 0;
    size_t i;

    if (text == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';

    for (i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];
        char piece[8];
        size_t len;

        /* JSON gives a meaning to the quote and the backslash, and it
         * refuses a raw control character inside a string. */
        switch (c) {
        case '"':  memcpy(piece, "\\\"", 3); break;
        case '\\': memcpy(piece, "\\\\", 3); break;
        case '\n': memcpy(piece, "\\n", 3); break;
        case '\r': memcpy(piece, "\\r", 3); break;
        case '\t': memcpy(piece, "\\t", 3); break;
        case '\b': memcpy(piece, "\\b", 3); break;
        case '\f': memcpy(piece, "\\f", 3); break;
        default:
            if (c < 0x20)
                snprintf(piece, sizeof piece, "\\u%04x", c);
            else {
                piece[0] = (char)c;
                piece[1] = '\0';
            }
            break;
        }

        len = strlen(piece);
        if (at + len + 1 > cap) {
            out[0] = '\0';
            return GS_ERR_MEM;
        }
        memcpy(out + at, piece, len);
        at += len;
    }
    out[at] = '\0';
    return GS_OK;
}

/* Turns what JSON wrote back into the characters it stood for. */
static int unescape(const char *in, size_t len, char *out, size_t cap)
{
    size_t at = 0;
    size_t i;

    for (i = 0; i < len; i++) {
        char c = in[i];

        if (at + 1 >= cap)
            break;                     /* what fits is kept */
        if (c != '\\') {
            out[at++] = c;
            continue;
        }
        i++;
        if (i >= len)
            break;
        switch (in[i]) {
        case 'n': out[at++] = '\n'; break;
        case 'r': out[at++] = '\r'; break;
        case 't': out[at++] = '\t'; break;
        case 'b': out[at++] = '\b'; break;
        case 'f': out[at++] = '\f'; break;
        case '"': out[at++] = '"'; break;
        case '\\': out[at++] = '\\'; break;
        case '/': out[at++] = '/'; break;
        case 'u': {
            /* Only the characters this program can draw are kept, and
             * anything above them becomes a question mark rather than
             * being written as bytes nothing here can show. */
            unsigned int code = 0;
            int k;

            for (k = 1; k <= 4 && i + (size_t)k < len; k++) {
                char h = in[i + (size_t)k];
                unsigned int v;

                if (h >= '0' && h <= '9') v = (unsigned int)(h - '0');
                else if (h >= 'a' && h <= 'f') v = (unsigned int)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') v = (unsigned int)(h - 'A' + 10);
                else break;
                code = code * 16u + v;
            }
            i += 4;
            out[at++] = code >= 0x20 && code <= 0x7e ? (char)code : '?';
            break;
        }
        default:
            out[at++] = in[i];
            break;
        }
    }
    out[at] = '\0';
    return GS_OK;
}

int gs_provider_field(const char *body, const char *key, char *out,
                      size_t cap)
{
    char pattern[64];
    const char *at;

    if (body == NULL || key == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (snprintf(pattern, sizeof pattern, "\"%s\"", key) >= (int)sizeof pattern)
        return GS_ERR_ARG;

    at = strstr(body, pattern);
    if (at == NULL)
        return GS_ERR;
    at += strlen(pattern);

    while (*at == ' ' || *at == '\t')
        at++;
    if (*at != ':')
        return GS_ERR;
    at++;
    while (*at == ' ' || *at == '\t')
        at++;
    if (*at != '"')
        return GS_ERR;                 /* the value is not a string */
    at++;

    /* The end of the value is the first quote that is not itself
     * written down as part of the text. */
    {
        const char *end = at;

        while (*end != '\0') {
            if (*end == '\\' && end[1] != '\0') {
                end += 2;
                continue;
            }
            if (*end == '"')
                break;
            end++;
        }
        if (*end != '"')
            return GS_ERR;
        return unescape(at, (size_t)(end - at), out, cap);
    }
}

int gs_provider_body(const char *model, const char *prompt,
                     double temperature, int seed, int limit,
                     char *out, size_t cap)
{
    return gs_provider_body_system(model, NULL, prompt, temperature, seed,
                                   limit, out, cap);
}

int gs_provider_body_system(const char *model, const char *system,
                            const char *prompt, double temperature,
                            int seed, int limit, char *out, size_t cap)
{
    return gs_provider_body_turns(model, system, NULL, 0, prompt,
                                  temperature, seed, limit, out, cap);
}

/* Adds text to the end of the body, escaped when it is something a
 * person wrote, and refuses rather than cuts when it will not fit. */
static int put(char *out, size_t cap, size_t *at, const char *text,
               int escaped)
{
    size_t len;

    if (*at >= cap)
        return GS_ERR_MEM;
    if (escaped) {
        if (gs_provider_escape(text, out + *at, cap - *at) != GS_OK)
            return GS_ERR_MEM;
        len = strlen(out + *at);
    } else {
        len = strlen(text);
        if (*at + len + 1 > cap)
            return GS_ERR_MEM;
        memcpy(out + *at, text, len + 1);
    }
    *at += len;
    return GS_OK;
}

/* One message of the conversation, with the comma that parts it from the
 * one before when there is one before. */
static int put_message(char *out, size_t cap, size_t *at, const char *role,
                       const char *content, int first)
{
    char head[48];

    snprintf(head, sizeof head, "%s{\"role\":\"%s\",\"content\":\"",
             first ? "" : ",", role);
    if (put(out, cap, at, head, 0) != GS_OK ||
        put(out, cap, at, content, 1) != GS_OK ||
        put(out, cap, at, "\"}", 0) != GS_OK)
        return GS_ERR_MEM;
    return GS_OK;
}

int gs_provider_body_turns(const char *model, const char *system,
                           const gs_provider_turn_t *history, int count,
                           const char *prompt, double temperature,
                           int seed, int limit, char *out, size_t cap)
{
    char safe_model[GS_PROVIDER_MODEL * 2];
    char tail[128];
    size_t at = 0;
    int first = 1;
    int rc;
    int i;

    if (model == NULL || prompt == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (temperature < 0.0 || temperature > 2.0)
        return GS_ERR_ARG;
    if (limit <= 0 || limit > 32768)
        return GS_ERR_ARG;
    if (count < 0 || (count > 0 && history == NULL))
        return GS_ERR_ARG;
    if (gs_provider_escape(model, safe_model, sizeof safe_model) != GS_OK)
        return GS_ERR_ARG;

    rc = put(out, cap, &at, "{\"model\":\"", 0);
    if (rc == GS_OK)
        rc = put(out, cap, &at, safe_model, 0);
    if (rc == GS_OK)
        rc = put(out, cap, &at, "\",\"messages\":[", 0);

    /* The standing instructions go first, then what was said before in
     * the order it was said, then the question. Every piece is escaped
     * the same way, since all of it is text a person can edit. */
    if (rc == GS_OK && system != NULL && system[0] != '\0') {
        rc = put_message(out, cap, &at, "system", system, first);
        first = 0;
    }
    for (i = 0; rc == GS_OK && i < count; i++) {
        if (history[i].prompt == NULL || history[i].answer == NULL)
            continue;
        rc = put_message(out, cap, &at, "user", history[i].prompt, first);
        first = 0;
        if (rc == GS_OK)
            rc = put_message(out, cap, &at, "assistant", history[i].answer,
                             0);
    }
    if (rc == GS_OK)
        rc = put_message(out, cap, &at, "user", prompt, first);

    snprintf(tail, sizeof tail,
             "],\"temperature\":%.3f,\"seed\":%d,\"max_tokens\":%d,"
             "\"stream\":false}", temperature, seed, limit);
    if (rc == GS_OK)
        rc = put(out, cap, &at, tail, 0);

    if (rc != GS_OK) {
        out[0] = '\0';
        return GS_ERR_MEM;
    }
    return GS_OK;
}
