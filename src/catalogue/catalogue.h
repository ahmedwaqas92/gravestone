/* catalogue.h
 *
 * The list of models that exist, and whether each one could run on this
 * machine. The list is a snapshot of the Ollama library taken when a
 * release is built, so reading it needs no network. Ollama is the
 * inference server this project talks to, so every row is a model that
 * runtime has already packaged and can pull by the name in the file
 * column.
 */
#ifndef GS_CATALOGUE_H
#define GS_CATALOGUE_H

#include <stddef.h>
#include "gravestone.h"
#include "detect.h"

#define GS_CATALOGUE_MAX 2048

typedef enum {
    GS_FIT_NONE = 0,    /* too large for the memory this machine has */
    GS_FIT_PROCESSOR,   /* fits in memory, so the processor does the work */
    GS_FIT_PARTIAL,     /* part on the card, the rest falling back */
    GS_FIT_GRAPHICS     /* fits entirely in graphics memory */
} gs_catalogue_fit_t;

typedef struct {
    char      repository[128];
    char      file[128];
    char      quantisation[16];
    long long bytes;
    long long downloads;
} gs_catalogue_entry_t;

/* Reads the snapshot. Returns how many entries were loaded. */
int gs_catalogue_load(void);
void gs_catalogue_release(void);

int  gs_catalogue_count(void);
const gs_catalogue_entry_t *gs_catalogue_at(int index);

/* Where the snapshot was found, for reporting. */
const char *gs_catalogue_source(void);

/* Whether a model of this size could run on the machine described. The
 * headroom allows for the working space a model needs beyond its own
 * weights, and one and a fifth is an assumption rather than a measurement. */
gs_catalogue_fit_t gs_catalogue_fit(long long bytes,
                                    const gs_detect_report_t *machine);
const char *gs_catalogue_fit_name(gs_catalogue_fit_t fit);

/* What a row actually is. The snapshot mixes language models with speech
 * models, vision projector files and one embedding model, and a projector
 * file does nothing on its own, so the kind has to be visible. */
typedef enum {
    GS_KIND_TEXT = 0,     /* an ordinary language model */
    GS_KIND_CODE,         /* a language model trained for programming */
    GS_KIND_VISION,       /* the image half of a model that reads pictures */
    GS_KIND_SPEECH,       /* speech, music and other audio */
    GS_KIND_EMBEDDING,    /* turns text into a vector for searching */
    GS_KIND_COUNT
} gs_catalogue_kind_t;

/* Reads the repository and file names and says which kind the row is.
 * The names are the only evidence the snapshot carries, so a row named
 * carelessly by its author lands in GS_KIND_TEXT. */
gs_catalogue_kind_t gs_catalogue_kind(const gs_catalogue_entry_t *entry);
const char *gs_catalogue_kind_name(gs_catalogue_kind_t kind);

/* Fills indices with the entries that could run, largest first, and
 * returns how many were written. */
int gs_catalogue_runnable(const gs_detect_report_t *machine, int *indices,
                          int cap);

extern const gs_module gs_catalogue_module;

#endif
