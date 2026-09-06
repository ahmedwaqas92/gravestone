/* catalogue_test.c
 *
 * The snapshot, the fit rule and the kind classifier. Everything here runs
 * against the shipped file and a machine description made up on the spot,
 * so no hardware is read and no network is touched.
 */
#include "catalogue.h"
#include "gravestone.h"
#include "detect.h"
#include "log.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* A machine with the memory sizes handed in and nothing else filled. */
static gs_detect_report_t machine_of(long long ram, long long vram)
{
    gs_detect_report_t m;

    memset(&m, 0, sizeof m);
    m.ram_total_bytes = ram;
    m.gpu_memory_bytes = vram;
    return m;
}

static void test_contract(void)
{
    printf("module contract\n");
    check(gs_catalogue_module.name != NULL, "name is set");
    check(strcmp(gs_catalogue_module.name, "catalogue") == 0,
          "name is \"catalogue\"");
    check(gs_catalogue_module.summary != NULL, "summary is set");
    check(gs_catalogue_module.run != NULL, "run is set");
}

static void test_loading(void)
{
    int n;

    printf("reading the snapshot\n");

    n = gs_catalogue_load();
    printf("        %d rows from %s\n", n, gs_catalogue_source());
    check(n > 0, "the snapshot loaded");
    check(n == gs_catalogue_count(), "the count agrees with what load said");
    check(gs_catalogue_at(-1) == NULL, "a negative index gives nothing");
    check(gs_catalogue_at(n) == NULL, "one past the end gives nothing");
    check(gs_catalogue_at(0) != NULL, "the first row is readable");

    if (n > 0) {
        const gs_catalogue_entry_t *e = gs_catalogue_at(0);

        check(e->repository[0] != '\0', "the first row names a repository");
        check(e->file[0] != '\0', "and a file");
        check(e->bytes > 0, "and carries a size");
    }

    /* Loading twice must not double the list. */
    check(gs_catalogue_load() == n, "a second load reports the same count");
}

static void test_fit_arithmetic(void)
{
    gs_detect_report_t none = machine_of(0, 0);
    gs_detect_report_t cpu_only = machine_of(8000000000LL, 0);
    gs_detect_report_t card = machine_of(4000000000LL, 6000000000LL);

    printf("the fit rule\n");

    /* needed = bytes / 5 * 6, with the division truncating first. */
    check(gs_catalogue_fit(5, &cpu_only) == GS_FIT_PROCESSOR,
          "5 bytes needs 6 and fits in system memory");
    check(gs_catalogue_fit(9, &none) == GS_FIT_NONE,
          "nothing fits on a machine with no memory at all");
    check(gs_catalogue_fit(0, &cpu_only) == GS_FIT_NONE,
          "a zero sized file never fits");
    check(gs_catalogue_fit(-1, &cpu_only) == GS_FIT_NONE,
          "a negative size never fits");
    check(gs_catalogue_fit(1000, NULL) == GS_FIT_NONE,
          "a null machine never fits");

    /* 6,666,666,666 / 5 * 6 = 7,999,999,998, one under the limit. */
    check(gs_catalogue_fit(6666666666LL, &cpu_only) == GS_FIT_PROCESSOR,
          "the largest model that fits in 8 GB of system memory");
    check(gs_catalogue_fit(6666666670LL, &cpu_only) == GS_FIT_NONE,
          "four bytes more does not fit");

    /* The card is asked first, so anything inside it answers GRAPHICS. */
    check(gs_catalogue_fit(1000000000LL, &card) == GS_FIT_GRAPHICS,
          "a small model lands on the card");
    /* 7,000,000,000 / 5 * 6 = 8,400,000,000, over the card and over
     * system memory, under the two added together. */
    check(gs_catalogue_fit(7000000000LL, &card) == GS_FIT_PARTIAL,
          "a model larger than either store alone splits across both");
    check(gs_catalogue_fit(4500000000LL, &card) == GS_FIT_GRAPHICS,
          "5,400,000,000 still fits inside a 6 GB card");
    check(gs_catalogue_fit(9000000000LL, &card) == GS_FIT_NONE,
          "a model larger than both together does not fit");

    check(gs_catalogue_fit_name(GS_FIT_NONE) != NULL, "every fit has a name");
    check(gs_catalogue_fit_name(GS_FIT_GRAPHICS) != NULL, "graphics has one");
}

/* Every row the classifier is asked about, and the kind it has to answer.
 * All of these appear in data/catalogue.tsv. */
static void test_kinds(void)
{
    static const struct {
        const char *repository;
        const char *file;
        gs_catalogue_kind_t want;
    } cases[] = {
        {"unsloth/Qwen3.5-9B-GGUF", "Qwen3.5-9B-Q4_K_M", GS_KIND_TEXT},
        {"unsloth/Qwen3.5-9B-GGUF", "mmproj-F16", GS_KIND_VISION},
        {"unsloth/Qwen3-Coder-30B-A3B-Instruct-GGUF",
         "Qwen3-Coder-30B-A3B-Instruct-Q4_K_M", GS_KIND_CODE},
        {"mudler/KAT-Coder-V2.5-Dev-APEX-GGUF", "KAT-Coder-V2.5-Q4_K_M",
         GS_KIND_CODE},
        {"audio-cpp/audio.cpp-gguf", "pocket-tts-english-q8_0",
         GS_KIND_SPEECH},
        {"audio-cpp/audio.cpp-gguf", "citrinet-asr-q8_0", GS_KIND_SPEECH},
        {"handy-computer/parakeet-unified-en-0.6b-gguf", "parakeet-f16",
         GS_KIND_SPEECH},
        {"mixedbread-ai/mxbai-embed-large-v1", "mxbai-embed-large-v1-f16",
         GS_KIND_EMBEDDING},
        /* CED is an audio tagging family, and surya reads documents with
         * its eyes, so neither is a language model even though nothing in
         * the obvious word lists says so. */
        {"mudler/ced-gguf", "ced-tiny-q8_0", GS_KIND_SPEECH},
        {"mudler/ced-gguf", "ced-base-f32", GS_KIND_SPEECH},
        {"datalab-to/surya-ocr-2-gguf", "surya-2", GS_KIND_VISION},
        /* The three letters ced sit inside advanced and uncensored, and a
         * name carrying them in passing has to stay where it was. */
        {"someone/advanced-gguf", "advanced-model-Q4_K_M", GS_KIND_TEXT},
        {"someone/Uncensored-GGUF", "Uncensored-Q4_K_M", GS_KIND_TEXT}
    };
    size_t i;
    int wrong = 0;

    printf("what kind each row is\n");

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        gs_catalogue_entry_t e;
        gs_catalogue_kind_t got;

        memset(&e, 0, sizeof e);
        gs_str_copy(e.repository, sizeof e.repository, cases[i].repository);
        gs_str_copy(e.file, sizeof e.file, cases[i].file);
        got = gs_catalogue_kind(&e);
        if (got != cases[i].want) {
            printf("        %s/%s answered %s, wanted %s\n",
                   cases[i].repository, cases[i].file,
                   gs_catalogue_kind_name(got),
                   gs_catalogue_kind_name(cases[i].want));
            wrong++;
        }
    }
    check(wrong == 0, "every known row is put in the right category");

    /* Capitalisation is written by hundreds of different authors. */
    {
        gs_catalogue_entry_t e;

        memset(&e, 0, sizeof e);
        gs_str_copy(e.repository, sizeof e.repository, "SOMEONE/MODEL-GGUF");
        gs_str_copy(e.file, sizeof e.file, "MMPROJ-F16");
        check(gs_catalogue_kind(&e) == GS_KIND_VISION,
              "an upper case name is read the same way");
    }

    check(gs_catalogue_kind(NULL) == GS_KIND_TEXT,
          "a null row falls back to a language model");
    check(gs_catalogue_kind_name(GS_KIND_COUNT) != NULL,
          "a kind out of range still has a name");
}

static void test_every_row_classifies(void)
{
    int n = gs_catalogue_count();
    int per_kind[GS_KIND_COUNT];
    int i, k, total = 0;
    int out_of_range = 0;

    printf("the whole snapshot, row by row\n");

    for (k = 0; k < (int)GS_KIND_COUNT; k++)
        per_kind[k] = 0;

    for (i = 0; i < n; i++) {
        gs_catalogue_kind_t got = gs_catalogue_kind(gs_catalogue_at(i));

        if ((int)got < 0 || (int)got >= (int)GS_KIND_COUNT) {
            out_of_range++;
            continue;
        }
        per_kind[got]++;
    }

    for (k = 0; k < (int)GS_KIND_COUNT; k++) {
        printf("        %-18s %d\n",
               gs_catalogue_kind_name((gs_catalogue_kind_t)k), per_kind[k]);
        total += per_kind[k];
    }

    check(out_of_range == 0, "no row answers with a kind that does not exist");
    check(total == n, "every row lands in exactly one category");
    check(per_kind[GS_KIND_TEXT] > 0, "some rows are language models");
    check(per_kind[GS_KIND_CODE] > 0, "some rows are coding models");
    check(per_kind[GS_KIND_VISION] > 0, "some rows are vision models");
    check(per_kind[GS_KIND_EMBEDDING] > 0, "some rows are embedding models");
}

static void test_runnable(void)
{
    gs_detect_report_t small = machine_of(1000000000LL, 0);
    /* Ten terabytes, since the library carries llama3.1:405b at F16 and
     * 811,722,959,584 x 6 / 5 = 974,067,551,500 wants most of one. */
    gs_detect_report_t large = machine_of(10000000000000LL, 0);
    int *fits = calloc(GS_CATALOGUE_MAX, sizeof *fits);
    int few, many, i;
    int ordered = 1;

    printf("which rows a machine could run\n");

    if (fits == NULL) {
        check(0, "buffer allocated");
        return;
    }

    few = gs_catalogue_runnable(&small, fits, GS_CATALOGUE_MAX);
    many = gs_catalogue_runnable(&large, fits, GS_CATALOGUE_MAX);
    printf("        1 GB machine %d rows, 10 TB machine %d rows\n", few,
           many);
    check(many >= few, "more memory never runs fewer models");
    check(many == gs_catalogue_count(),
          "a machine with a terabyte runs every row");

    for (i = 1; i < many; i++)
        if (gs_catalogue_at(fits[i - 1])->bytes <
            gs_catalogue_at(fits[i])->bytes)
            ordered = 0;
    check(ordered, "the answer comes back largest first");

    check(gs_catalogue_runnable(NULL, fits, GS_CATALOGUE_MAX) == 0,
          "a null machine runs nothing");
    check(gs_catalogue_runnable(&large, NULL, 10) == 0,
          "a null buffer is refused");
    check(gs_catalogue_runnable(&large, fits, 0) == 0,
          "a buffer with no room is refused");
    check(gs_catalogue_runnable(&large, fits, 3) == 3,
          "a small buffer stops at the room it has");

    free(fits);
}

/* Feeds the loader a snapshot written to hurt it. Torn lines, missing
 * columns, sizes that overflow, and a line far longer than the read
 * buffer. Survival means no crash, nothing kept that fails the shape
 * rules, and an exact count of the rows that deserved to live. */
static void test_hostile_snapshot(void)
{
    const char *path = "build/hostile-catalogue.tsv";
    FILE *f = fopen(path, "w");
    int n, i;

    printf("a snapshot written to hurt the loader\n");

    if (f == NULL) {
        check(0, "the hostile file could be written");
        return;
    }
    fputs("# comment line\n\n", f);
    fputs("library/good\tgood:1b\tQ4_K_M\t1000\t5\n", f);
    fputs("only\tfour\tcolumns\t9\n", f);
    fputs("library/six\tsix:1b\tQ4\t1000\t5\textra\n", f);
    fputs("library/zero\tzero:1b\tQ4\t0\t5\n", f);
    fputs("library/minus\tminus:1b\tQ4\t-50\t5\n", f);
    fputs("library/text\ttext:1b\tQ4\tnotanumber\t5\n", f);
    fputs("library/huge\thuge:1b\tQ4\t99999999999999999999999\t5\n", f);
    fputs("\tnoname:1b\tQ4\t1000\t5\n", f);
    fputs("library/nofile\t\tQ4\t1000\t5\n", f);
    for (i = 0; i < 2000; i++)
        fputc('a', f);
    fputs("\tlong:1b\tQ4\t1000\t5\n", f);
    fputs("library/last\tlast:1b\tQ4\t1000\t5\n", f);
    fclose(f);

    setenv("GS_CATALOGUE_PATH", path, 1);
    n = gs_catalogue_load();
    printf("        %d rows survived out of 12 lines\n", n);

    /* good, six, huge and last. The torn line splits at the buffer edge
     * and its tail piece carries five columns, so the loader has to
     * recognise the tear and throw both pieces away. */
    check(n == 4, "exactly the four sane rows survived");
    for (i = 0; i < n; i++) {
        const gs_catalogue_entry_t *e = gs_catalogue_at(i);

        check(e->bytes > 0, "a kept row carries a positive size");
        check(e->repository[0] != '\0' && e->file[0] != '\0',
              "a kept row carries both names");
    }

    /* A snapshot bigger than the table stops at the table. */
    f = fopen(path, "w");
    if (f != NULL) {
        for (i = 0; i < GS_CATALOGUE_MAX + 500; i++)
            fprintf(f, "library/m%d\tm%d:1b\tQ4\t1000\t5\n", i, i);
        fclose(f);
        n = gs_catalogue_load();
        check(n == GS_CATALOGUE_MAX,
              "an oversized snapshot fills the table and stops");
    }

    f = fopen(path, "w");
    if (f != NULL) {
        fclose(f);
        check(gs_catalogue_load() == 0, "an empty snapshot loads nothing");
    }

    unsetenv("GS_CATALOGUE_PATH");
    remove(path);
    check(gs_catalogue_load() > 0,
          "the real snapshot loads again once the override is gone");
}

/* Every row of the snapshot this build ships. The shape rules double as
 * a gate on the fetch script, since a scrape that goes wrong lands here
 * before it lands in front of a user. */
static void test_shipped_snapshot_shape(void)
{
    int n = gs_catalogue_count();
    int i, j;
    int bad_bytes = 0, bad_names = 0, bad_quant = 0, bad_pull = 0;
    int duplicates = 0;

    printf("the snapshot this build ships\n");

    check(n > 100, "the snapshot is not nearly empty");

    for (i = 0; i < n; i++) {
        const gs_catalogue_entry_t *e = gs_catalogue_at(i);

        if (e->bytes <= 0 || e->bytes > 2000000000000LL)
            bad_bytes++;
        if (strncmp(e->repository, "library/", 8) != 0 ||
            strchr(e->file, ':') == NULL)
            bad_names++;
        if (e->quantisation[0] == '\0')
            bad_quant++;
        if (e->downloads < 0)
            bad_pull++;
    }
    check(bad_bytes == 0,
          "every size is positive and under two terabytes");
    check(bad_names == 0,
          "every row is library/<model> with a <model>:<tag> pull name");
    check(bad_quant == 0, "every row names its quantisation");
    check(bad_pull == 0, "no pull count is negative");

    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            if (strcmp(gs_catalogue_at(i)->file,
                       gs_catalogue_at(j)->file) == 0)
                duplicates++;
    check(duplicates == 0, "no pull name appears twice");
}

int main(void)
{
    gs_log_set_level(GS_LOG_ERROR);

    test_contract();
    test_loading();
    test_fit_arithmetic();
    test_kinds();
    test_every_row_classifies();
    test_runnable();
    test_hostile_snapshot();
    test_shipped_snapshot_shape();

    gs_catalogue_release();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
