#include "catalogue.h"
#include "catalogue_internal.h"
#include "gravestone.h"
#include "log.h"
#include "str.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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


long long gs_catalogue_needed(const long long *sizes, int n)
{
    long long total = 0;
    int i;

    if (sizes == NULL || n <= 0)
        return 0;
    for (i = 0; i < n; i++) {
        if (sizes[i] <= 0)
            continue;
        total += sizes[i] / HEADROOM_DEN * HEADROOM_NUM;
    }
    return total;
}

int gs_catalogue_fit_many(const long long *sizes, int n,
                          const gs_detect_report_t *machine)
{
    gs_catalogue_room_t room = gs_catalogue_room(machine);
    long long have;
    long long used = 0;
    int i;

    if (sizes == NULL || n <= 0 || machine == NULL)
        return 0;

    /* Models run at the same time, so their needs add up against the one
     * pool of memory and graphics memory together. */
    have = room.memory_bytes + room.graphics_bytes;
    for (i = 0; i < n; i++) {
        long long want;

        if (sizes[i] <= 0)
            return i;
        want = sizes[i] / HEADROOM_DEN * HEADROOM_NUM;
        if (used + want > have)
            return i;
        used += want;
    }
    return n;
}

gs_catalogue_fit_t gs_catalogue_fit(long long bytes,
                                    const gs_detect_report_t *machine)
{
    gs_catalogue_room_t room;
    long long needed;

    if (machine == NULL || bytes <= 0)
        return GS_FIT_NONE;

    room = gs_catalogue_room(machine);
    needed = bytes / HEADROOM_DEN * HEADROOM_NUM;

    /* Both stores are asked, rather than the first one that answers,
     * since a small model on a machine with a card and room to spare can
     * run in either and a person choosing wants to see both. */
    {
        int on_card = room.graphics_bytes > 0 &&
                      needed <= room.graphics_bytes;
        int in_memory = needed <= room.memory_bytes;

        if (on_card && in_memory)
            return GS_FIT_EITHER;
        if (on_card)
            return GS_FIT_GRAPHICS;
        if (in_memory)
            return GS_FIT_PROCESSOR;
    }

    /* Neither store holds the whole model, so the layers are shared out
     * between them, and every layer left in memory crosses the bus once
     * for every word produced. This answers where a model would run, and
     * says nothing about whether it is worth fetching, which
     * gs_catalogue_listed decides. A model already on the disk runs
     * however slowly it runs. */
    if (needed <= room.memory_bytes + room.graphics_bytes)
        return GS_FIT_PARTIAL;
    return GS_FIT_NONE;
}

int gs_catalogue_listed(long long bytes, const gs_detect_report_t *machine)
{
    gs_catalogue_fit_t fit;

    if (machine == NULL || bytes <= 0)
        return 0;

    fit = gs_catalogue_fit(bytes, machine);
    if (fit == GS_FIT_NONE)
        return 0;
    if (!gs_catalogue_storable(bytes, machine))
        return 0;
    if (fit != GS_FIT_PARTIAL)
        return 1;

    /* A divided model with too little of itself on the card is left out,
     * since it would load and then answer at a word a minute, which is a
     * download wasted. Both sides are multiplied rather than divided,
     * since dividing the left first throws away up to nine bytes, which
     * is enough to admit a model a hair under the line. */
    {
        gs_catalogue_room_t room = gs_catalogue_room(machine);
        long long needed = bytes / HEADROOM_DEN * HEADROOM_NUM;

        return needed * SPLIT_FLOOR_NUM
               <= room.graphics_bytes * SPLIT_FLOOR_DEN;
    }
}

int gs_catalogue_storable(long long bytes,
                          const gs_detect_report_t *machine)
{
    gs_catalogue_room_t room;

    if (machine == NULL || bytes <= 0)
        return 0;
    room = gs_catalogue_room(machine);
    /* One file sits on one disk, so the largest single disk decides
     * rather than every disk added together. A machine reporting no disks
     * at all is not held to this, since it has said nothing either way. */
    if (room.largest_disk <= 0)
        return 1;
    return bytes <= room.largest_disk;
}

int gs_catalogue_conversational(gs_catalogue_fit_t fit)
{
    /* Every number in the file is read once for every word produced, so
     * anything left in system memory crosses the bus once per word. A
     * model that fits the card alone avoids that, whether or not it would
     * also fit in memory. */
    return fit == GS_FIT_GRAPHICS || fit == GS_FIT_EITHER;
}

const char *gs_catalogue_fit_tag(gs_catalogue_fit_t fit)
{
    switch (fit) {
    case GS_FIT_EITHER:    return "GPU+CPU";
    case GS_FIT_GRAPHICS:  return "GPU";
    case GS_FIT_PROCESSOR: return "CPU";
    case GS_FIT_PARTIAL:   return "SPLIT";
    default:               return "NONE";
    }
}

const char *gs_catalogue_fit_name(gs_catalogue_fit_t fit)
{
    switch (fit) {
    case GS_FIT_EITHER:    return "card or processor";
    case GS_FIT_GRAPHICS:  return "card";
    case GS_FIT_PROCESSOR: return "processor";
    case GS_FIT_PARTIAL:   return "split";
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

    /* A row is listed when it could be kept and could be run, since the
     * list is what a person picks a model to fetch from. Whether a model
     * already on the disk can run is asked with gs_catalogue_fit alone,
     * which has no opinion on free space. */
    for (i = 0; i < entry_count && n < cap; i++)
        if (gs_catalogue_listed(entries[i].bytes, machine))
            indices[n++] = i;

    /* A caller with too small a buffer gets a short answer, and silence
     * would read as a complete one. */
    if (n == cap && i < entry_count)
        gs_log_warn("catalogue: the buffer held %d, so %d rows were not "
                    "looked at", cap, entry_count - i);

    qsort(indices, (size_t)n, sizeof *indices, by_size_desc);
    return n;
}
