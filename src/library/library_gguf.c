/* library_gguf.c
 *
 * Reading a model file's own header. A GGUF file opens with a table of
 * contents, a magic word, a version, then a run of named values, and
 * everything wanted sits there before the weights begin.
 *
 * Every value carries a type tag, and skipping the ones not wanted is
 * what makes finding the one that is wanted possible.
 */
#include "library.h"
#include "gravestone.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A GGUF file opens with its own table of contents, a magic word, a
 * version, then a run of named values. Reading it costs one open and a
 * few kilobytes, since everything wanted sits before the weights.
 *
 * Every value carries a type tag, and skipping the ones not wanted is
 * what makes finding the one that is wanted possible. */
#define GGUF_STRING 8
#define GGUF_ARRAY  9

static const int gguf_width[] = {
    1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8
};

static int gguf_read(FILE *f, void *out, size_t bytes)
{
    return fread(out, 1, bytes, f) == bytes;
}

/* Reads a length prefixed name into out, and reports whether the file
 * still made sense. A name longer than the buffer is read past rather
 * than kept, so the walk stays lined up with the file. */
static int gguf_name(FILE *f, char *out, size_t cap)
{
    uint64_t len = 0;
    size_t take;

    if (!gguf_read(f, &len, 8) || len > (1u << 20))
        return 0;
    take = len < cap - 1 ? (size_t)len : cap - 1;
    if (!gguf_read(f, out, take))
        return 0;
    out[take] = '\0';
    if (len > take && fseek(f, (long)(len - take), SEEK_CUR) != 0)
        return 0;
    return 1;
}

static int gguf_skip(FILE *f, uint32_t type)
{
    if (type == GGUF_STRING) {
        uint64_t len = 0;

        if (!gguf_read(f, &len, 8) || len > (1u << 20))
            return 0;
        return fseek(f, (long)len, SEEK_CUR) == 0;
    }
    if (type == GGUF_ARRAY) {
        uint32_t inner = 0;
        uint64_t count = 0;
        uint64_t i;

        if (!gguf_read(f, &inner, 4) || !gguf_read(f, &count, 8))
            return 0;
        if (count > (1u << 24))
            return 0;
        for (i = 0; i < count; i++)
            if (!gguf_skip(f, inner))
                return 0;
        return 1;
    }
    if (type >= sizeof gguf_width / sizeof gguf_width[0] ||
        gguf_width[type] == 0)
        return 0;
    return fseek(f, gguf_width[type], SEEK_CUR) == 0;
}

/* True when the key names the model's own context length rather than a
 * scaling note that carries the same ending. */
static int is_context_key(const char *key, const char *arch)
{
    size_t n = strlen(key);

    if (n < 15 || strcmp(key + n - 15, ".context_length") != 0)
        return 0;
    if (strstr(key, "original") != NULL)
        return 0;
    if (arch[0] != '\0')
        return strncmp(key, arch, strlen(arch)) == 0 &&
               key[strlen(arch)] == '.';
    return 1;
}

long long gs_library_read_context(const char *path)
{
    FILE *f = fopen(path, "rb");
    char magic[4];
    uint32_t version = 0;
    uint64_t tensors = 0, pairs = 0, i;
    char arch[64] = {0};
    long long context = 0;

    if (f == NULL)
        return 0;
    if (!gguf_read(f, magic, 4) || memcmp(magic, "GGUF", 4) != 0) {
        fclose(f);
        return 0;
    }
    if (!gguf_read(f, &version, 4) || !gguf_read(f, &tensors, 8) ||
        !gguf_read(f, &pairs, 8) || pairs > 4096) {
        fclose(f);
        return 0;
    }

    for (i = 0; i < pairs; i++) {
        char key[128];
        uint32_t type = 0;

        if (!gguf_name(f, key, sizeof key) || !gguf_read(f, &type, 4))
            break;

        if (strcmp(key, "general.architecture") == 0 &&
            type == GGUF_STRING) {
            if (!gguf_name(f, arch, sizeof arch))
                break;
            continue;
        }
        if (context == 0 && is_context_key(key, arch)) {
            if (type == 4 || type == 5) {
                uint32_t v = 0;

                if (!gguf_read(f, &v, 4))
                    break;
                context = (long long)v;
                continue;
            }
            if (type == 10 || type == 11) {
                uint64_t v = 0;

                if (!gguf_read(f, &v, 8))
                    break;
                context = (long long)v;
                continue;
            }
        }
        if (!gguf_skip(f, type))
            break;
    }
    fclose(f);
    return context;
}

