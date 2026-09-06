#include "catalogue.h"
#include "gravestone.h"
#include "log.h"
#include "str.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Weights alone do not run a model, since the working space for one reply
 * sits alongside them. A fifth on top is an allowance rather than a
 * measurement, and it has not been checked against a real run. */
#define HEADROOM_NUM 6
#define HEADROOM_DEN 5

static gs_catalogue_entry_t *entries;
static int entry_count;
static char source_path[512];

static const char *SEARCH[] = {
    "data/catalogue.tsv",
    "../data/catalogue.tsv",
    "/usr/share/gravestone/catalogue.tsv"
};

static int parse_line(char *line, gs_catalogue_entry_t *out)
{
    char *field[5];
    int n = 0;
    char *scan = line;

    while (n < 5) {
        field[n++] = scan;
        scan = strchr(scan, '\t');
        if (scan == NULL)
            break;
        *scan++ = '\0';
    }
    if (n != 5)
        return 0;

    field[4][strcspn(field[4], "\r\n")] = '\0';

    gs_str_copy(out->repository, sizeof out->repository, field[0]);
    gs_str_copy(out->file, sizeof out->file, field[1]);
    gs_str_copy(out->quantisation, sizeof out->quantisation, field[2]);
    out->bytes = strtoll(field[3], NULL, 10);
    out->downloads = strtoll(field[4], NULL, 10);

    return out->repository[0] != '\0' && out->file[0] != '\0' &&
           out->bytes > 0;
}

int gs_catalogue_load(void)
{
    FILE *f = NULL;
    char line[1024];
    size_t i;

    const char *override = getenv("GS_CATALOGUE_PATH");

    gs_catalogue_release();

    /* Tests point the loader at a file of their own making, since feeding
     * it something malformed on purpose is the only way to know it
     * survives one. */
    if (override != NULL && override[0] != '\0') {
        f = fopen(override, "r");
        if (f != NULL)
            gs_str_copy(source_path, sizeof source_path, override);
    }
    for (i = 0; f == NULL && i < sizeof SEARCH / sizeof SEARCH[0]; i++) {
        f = fopen(SEARCH[i], "r");
        if (f != NULL) {
            gs_str_copy(source_path, sizeof source_path, SEARCH[i]);
            break;
        }
    }
    if (f == NULL) {
        gs_log_warn("catalogue: no snapshot found on any known path");
        return 0;
    }

    entries = calloc(GS_CATALOGUE_MAX, sizeof *entries);
    if (entries == NULL) {
        fclose(f);
        return 0;
    }

    {
        int discarding = 0;

        while (fgets(line, sizeof line, f) != NULL &&
               entry_count < GS_CATALOGUE_MAX) {
            size_t len = strlen(line);
            int complete = (len > 0 && line[len - 1] == '\n') || feof(f);

            /* A line longer than the buffer arrives torn into pieces,
             * and the later pieces can look like rows of their own, so
             * everything up to the next real line end is thrown away. */
            if (discarding) {
                discarding = !complete;
                continue;
            }
            if (!complete) {
                discarding = 1;
                continue;
            }
            if (line[0] == '#' || line[0] == '\n')
                continue;
            if (parse_line(line, &entries[entry_count]))
                entry_count++;
        }
    }
    fclose(f);

    gs_log_info("catalogue: %d models loaded from %s", entry_count,
                source_path);
    return entry_count;
}

void gs_catalogue_release(void)
{
    free(entries);
    entries = NULL;
    entry_count = 0;
    source_path[0] = '\0';
}

int gs_catalogue_count(void)
{
    return entry_count;
}

const gs_catalogue_entry_t *gs_catalogue_at(int index)
{
    if (index < 0 || index >= entry_count)
        return NULL;
    return &entries[index];
}

const char *gs_catalogue_source(void)
{
    return source_path[0] != '\0' ? source_path : "nothing loaded";
}

gs_catalogue_fit_t gs_catalogue_fit(long long bytes,
                                    const gs_detect_report_t *machine)
{
    long long needed;

    if (machine == NULL || bytes <= 0)
        return GS_FIT_NONE;

    needed = bytes / HEADROOM_DEN * HEADROOM_NUM;

    if (machine->gpu_memory_bytes > 0 && needed <= machine->gpu_memory_bytes)
        return GS_FIT_GRAPHICS;
    if (needed <= machine->ram_total_bytes)
        return GS_FIT_PROCESSOR;
    if (machine->gpu_memory_bytes > 0 &&
        needed <= machine->gpu_memory_bytes + machine->ram_total_bytes)
        return GS_FIT_PARTIAL;
    return GS_FIT_NONE;
}

const char *gs_catalogue_fit_name(gs_catalogue_fit_t fit)
{
    switch (fit) {
    case GS_FIT_GRAPHICS:  return "card";
    case GS_FIT_PARTIAL:   return "split";
    case GS_FIT_PROCESSOR: return "processor";
    default:               return "too large";
    }
}

static int by_size_desc(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    long long da = entries[ia].bytes;
    long long db = entries[ib].bytes;

    if (da < db) return 1;
    if (da > db) return -1;
    return 0;
}

/* Case blind substring search. The snapshot carries names written by
 * hundreds of different people, so capitalisation proves nothing. */
static int contains_ci(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    size_t i;

    if (n == 0)
        return 1;
    for (i = 0; haystack[i] != '\0'; i++) {
        size_t k = 0;

        while (k < n && haystack[i + k] != '\0' &&
               tolower((unsigned char)haystack[i + k]) ==
               tolower((unsigned char)needle[k]))
            k++;
        if (k == n)
            return 1;
    }
    return 0;
}

static int any_of(const char *text, const char *const *words, int count)
{
    int i;

    for (i = 0; i < count; i++)
        if (contains_ci(text, words[i]))
            return 1;
    return 0;
}

/* Words drawn from the snapshot itself rather than invented. Every one of
 * them appears in at least one repository or file name in the 729 rows. */
static const char *const VISION_FILE[] = {
    "mmproj", "vision", "llava", "clip"
};
static const char *const VISION_REPO[] = {
    "-vl", "vision", "llava", "surya", "-ocr"
};
static const char *const EMBEDDING[] = {
    "embed", "rerank", "bge-", "gte-", "e5-"
};
static const char *const SPEECH[] = {
    "audio", "speech", "voice", "tts", "asr", "whisper", "transcribe",
    "parakeet", "codec", "music", "demucs", "roformer", "diar", "vocoder",
    "sortformer", "citrinet", "kroko", "supertonic", "cosyvoice",
    "ace-step", "fish-", "midashenglm", "vibevoice", "miocodec", "inflect",
    "mel-band", "s2-pro", "audiosr", "stable-audio", "moss",
    /* The slash pins ced to the start of a name, since advanced- and
     * uncensored- carry the same three letters in passing. */
    "/ced-"
};
static const char *const CODE[] = {
    "coder", "-code", "codellama", "starcoder"
};

#define COUNT_OF(a) ((int)(sizeof (a) / sizeof (a)[0]))

gs_catalogue_kind_t gs_catalogue_kind(const gs_catalogue_entry_t *entry)
{
    char both[264];

    if (entry == NULL)
        return GS_KIND_TEXT;

    snprintf(both, sizeof both, "%s/%s", entry->repository, entry->file);

    if (any_of(entry->file, VISION_FILE, COUNT_OF(VISION_FILE)) ||
        any_of(entry->repository, VISION_REPO, COUNT_OF(VISION_REPO)))
        return GS_KIND_VISION;
    if (any_of(both, EMBEDDING, COUNT_OF(EMBEDDING)))
        return GS_KIND_EMBEDDING;
    if (any_of(both, SPEECH, COUNT_OF(SPEECH)))
        return GS_KIND_SPEECH;
    if (any_of(both, CODE, COUNT_OF(CODE)))
        return GS_KIND_CODE;
    return GS_KIND_TEXT;
}

const char *gs_catalogue_kind_name(gs_catalogue_kind_t kind)
{
    switch (kind) {
    case GS_KIND_TEXT:      return "Language";
    case GS_KIND_CODE:      return "Code";
    case GS_KIND_VISION:    return "Vision";
    case GS_KIND_SPEECH:    return "Speech and audio";
    case GS_KIND_EMBEDDING: return "Embedding";
    default:                return "unknown";
    }
}

int gs_catalogue_runnable(const gs_detect_report_t *machine, int *indices,
                          int cap)
{
    int i, n = 0;

    if (machine == NULL || indices == NULL || cap <= 0)
        return 0;

    for (i = 0; i < entry_count && n < cap; i++)
        if (gs_catalogue_fit(entries[i].bytes, machine) != GS_FIT_NONE)
            indices[n++] = i;

    /* A caller with too small a buffer gets a short answer, and silence
     * would read as a complete one. */
    if (n == cap && i < entry_count)
        gs_log_warn("catalogue: the buffer held %d, so %d rows were not "
                    "looked at", cap, entry_count - i);

    qsort(indices, (size_t)n, sizeof *indices, by_size_desc);
    return n;
}

static int catalogue_init(void)
{
    return GS_OK;
}

static int catalogue_run(int argc, char **argv)
{
    gs_detect_report_t machine;
    int *fits;
    int n, i, shown = 0;

    if (gs_catalogue_load() == 0) {
        gs_log_error("catalogue: nothing to show");
        return GS_ERR;
    }
    if (gs_detect_read(&machine) != GS_OK) {
        gs_catalogue_release();
        return GS_ERR;
    }

    fits = calloc(GS_CATALOGUE_MAX, sizeof *fits);
    if (fits == NULL) {
        gs_catalogue_release();
        return GS_ERR_MEM;
    }

    /* "catalogue audit" prints every row with its category, runnable or
     * not, so the classification can be read end to end. */
    if (argc > 1 && strcmp(argv[1], "audit") == 0) {
        for (i = 0; i < gs_catalogue_count(); i++) {
            const gs_catalogue_entry_t *e = gs_catalogue_at(i);

            printf("%s\t%s\t%s\n",
                   gs_catalogue_kind_name(gs_catalogue_kind(e)),
                   e->repository, e->file);
        }
        free(fits);
        gs_catalogue_release();
        return GS_OK;
    }

    n = gs_catalogue_runnable(&machine, fits, GS_CATALOGUE_MAX);
    printf("source\t%s\n", gs_catalogue_source());
    printf("known\t%d\n", gs_catalogue_count());
    printf("runnable\t%d\n", n);

    {
        int per_kind[GS_KIND_COUNT] = {0};
        int k;

        for (i = 0; i < n; i++)
            per_kind[gs_catalogue_kind(gs_catalogue_at(fits[i]))]++;
        for (k = 0; k < (int)GS_KIND_COUNT; k++)
            printf("kind\t%s\t%d\n",
                   gs_catalogue_kind_name((gs_catalogue_kind_t)k),
                   per_kind[k]);
    }

    for (i = 0; i < n && shown < 20; i++, shown++) {
        const gs_catalogue_entry_t *e = gs_catalogue_at(fits[i]);

        printf("%s\t%s\t%lld\t%s\t%s\n",
               gs_catalogue_fit_name(gs_catalogue_fit(e->bytes, &machine)),
               gs_catalogue_kind_name(gs_catalogue_kind(e)),
               e->bytes, e->quantisation, e->file);
    }

    free(fits);
    gs_catalogue_release();
    return GS_OK;
}

static void catalogue_shutdown(void)
{
    gs_catalogue_release();
}

const gs_module gs_catalogue_module = {
    "catalogue",
    "list the models that could run on this machine",
    catalogue_init,
    catalogue_run,
    catalogue_shutdown
};
