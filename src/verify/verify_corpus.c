/* verify_corpus.c
 *
 * The documents an answer is checked against, held in memory once so a
 * search costs no reading.
 */
#include "verify.h"
#include "verify_internal.h"
#include "gravestone.h"
#include "log.h"
#include "str.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DOC_MAX   64
#define DOC_BYTES (4u << 20)     /* four megabytes of any one document */

typedef struct {
    char  name[128];
    char *text;
    size_t len;
} document_t;

static document_t docs[DOC_MAX];
static int doc_count;

void gs_verify_release(void)
{
    int i;

    for (i = 0; i < doc_count; i++) {
        free(docs[i].text);
        docs[i].text = NULL;
    }
    doc_count = 0;
}

int gs_verify_ready(void)
{
    return doc_count > 0;
}

static int read_one(const char *path, const char *name)
{
    FILE *f;
    long size;
    char *text;

    if (doc_count >= DOC_MAX)
        return GS_ERR;
    f = fopen(path, "rb");
    if (f == NULL)
        return GS_ERR;

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || (unsigned long)size > DOC_BYTES) {
        fclose(f);
        return GS_ERR;
    }

    text = malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(f);
        return GS_ERR_MEM;
    }
    if (fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return GS_ERR;
    }
    fclose(f);
    text[size] = '\0';

    docs[doc_count].text = text;
    docs[doc_count].len = (size_t)size;
    gs_str_copy(docs[doc_count].name, sizeof docs[doc_count].name, name);
    doc_count++;
    return GS_OK;
}

int gs_verify_corpus(const char *directory)
{
    DIR *dir;
    struct dirent *entry;

    gs_verify_release();
    if (directory == NULL || directory[0] == '\0')
        return GS_ERR_ARG;

    dir = opendir(directory);
    if (dir == NULL)
        return GS_ERR;

    while ((entry = readdir(dir)) != NULL) {
        char path[512];
        struct stat what;

        if (entry->d_name[0] == '.')
            continue;
        if ((size_t)snprintf(path, sizeof path, "%s/%s", directory,
                             entry->d_name) >= sizeof path)
            continue;
        if (stat(path, &what) != 0 || !S_ISREG(what.st_mode))
            continue;
        read_one(path, entry->d_name);
    }
    closedir(dir);

    gs_log_info("verify: %d document%s to check against", doc_count,
                doc_count == 1 ? "" : "s");
    return doc_count > 0 ? GS_OK : GS_ERR;
}

int gs_verify_span_found(const char *span)
{
    int i;

    if (span == NULL || span[0] == '\0' || doc_count == 0)
        return 0;
    for (i = 0; i < doc_count; i++)
        if (strstr(docs[i].text, span) != NULL)
            return 1;
    return 0;
}

/* The document a marker names, matched on the opening of the file name so
 * a marker may write the name without its ending. */
static const document_t *document_named(const char *marker)
{
    int i;

    if (marker == NULL || marker[0] == '\0')
        return NULL;
    for (i = 0; i < doc_count; i++)
        if (strncmp(docs[i].name, marker, strlen(marker)) == 0)
            return &docs[i];
    return NULL;
}

/* Words shorter than four letters are skipped, since the small joining
 * words appear everywhere and say nothing about the claim. */
static int worth_counting(const char *word, size_t len)
{
    size_t i;

    if (len < 4)
        return 0;
    for (i = 0; i < len; i++)
        if (!isalpha((unsigned char)word[i]))
            return 0;
    return 1;
}

/* Non zero when the document holds this word, whatever its case, with
 * something other than a letter on each side so a word is not found
 * inside a longer one. */
static int document_holds(const document_t *doc, const char *word, size_t len)
{
    size_t i;

    for (i = 0; i + len <= doc->len; i++) {
        size_t k;
        int same = 1;

        for (k = 0; k < len; k++)
            if (tolower((unsigned char)doc->text[i + k]) !=
                tolower((unsigned char)word[k])) {
                same = 0;
                break;
            }
        if (!same)
            continue;
        if (i > 0 && isalpha((unsigned char)doc->text[i - 1]))
            continue;
        if (i + len < doc->len && isalpha((unsigned char)doc->text[i + len]))
            continue;
        return 1;
    }
    return 0;
}

int gs_verify_overlap(const char *sentence, const char *marker,
                      int *looked_at)
{
    const document_t *doc = document_named(marker);
    size_t at = 0;
    int seen = 0;
    int found = 0;

    if (looked_at != NULL)
        *looked_at = 0;
    if (sentence == NULL || doc == NULL)
        return 0;

    while (sentence[at] != '\0') {
        size_t start;
        size_t len;

        while (sentence[at] != '\0' && !isalpha((unsigned char)sentence[at]))
            at++;
        start = at;
        while (isalpha((unsigned char)sentence[at]))
            at++;
        len = at - start;
        if (len == 0)
            continue;
        if (!worth_counting(sentence + start, len))
            continue;

        seen++;
        if (document_holds(doc, sentence + start, len))
            found++;
    }

    if (looked_at != NULL)
        *looked_at = seen;
    return found;
}
