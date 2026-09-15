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

/* Where a model could run, naming every place rather than the best one.
 * A machine with a card and room in memory can put a small model in
 * either, and a person choosing wants to know that both are open. */
typedef enum {
    GS_FIT_NONE = 0,    /* too large for the memory this machine has */
    GS_FIT_PARTIAL,     /* only across the card and memory together,
                         * however slowly that runs, which is a separate
                         * question answered by gs_catalogue_listed */
    GS_FIT_PROCESSOR,   /* memory alone, the card being absent or too small */
    GS_FIT_GRAPHICS,    /* the card alone, memory being too small */
    GS_FIT_EITHER       /* the card alone, and memory alone */
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

/* How much of the machine one model may draw on.
 *
 * Memory is what this session was given, less what the machine already
 * holds. Graphics memory adds up across every card that can be seen,
 * since a model too large for one may still be split across two. Storage
 * counts every disk in reach, because a model has to be kept somewhere
 * before it can be run. Cores count whatever the machine has.
 *
 * The room a model may occupy is memory plus graphics memory, and the
 * room it needs to be kept in is the largest single disk, since a file
 * cannot be spread across two. */
typedef struct {
    long long memory_bytes;    /* what is left after the reserve */
    long long memory_total;    /* what this session was given */
    long long reserved;        /* the kernel, the desktop, the open programs */
    long long graphics_bytes;  /* what a model may actually be given */
    long long graphics_free;   /* what the card reports free */
    long long graphics_total;  /* every card added together */
    long long graphics_held;   /* kept back by the inference server */
    long long largest_disk;    /* the biggest single disk with room */
    long long storage_bytes;   /* every disk in reach added together */
    int       cores;
} gs_catalogue_room_t;

/* Reads the room out of a machine report. */
gs_catalogue_room_t gs_catalogue_room(const gs_detect_report_t *machine);

/* What the machine is already holding before a model is loaded, covering
 * the kernel, the desktop and every open program. The larger of what the
 * machine reports free and what a machine in ordinary use carries, so an
 * idle moment does not promise room that vanishes when the person opens
 * their mail. */
long long gs_catalogue_reserve(const gs_detect_report_t *machine);

/* How much of a set of models would be held at once. Each one is counted
 * with the same headroom a single model gets, since they run together and
 * each needs its own working space.
 *
 * Returns how many of the first n fit, counting from the start, so a
 * caller ticking models learns where the line falls. */
int gs_catalogue_fit_many(const long long *sizes, int n,
                          const gs_detect_report_t *machine);

/* What a set of models would take in total, headroom counted in. */
long long gs_catalogue_needed(const long long *sizes, int n);

/* Where a model of this size would run on the machine described. The
 * headroom allows for the working space a model needs beyond its own
 * weights, and one and a fifth is an assumption rather than a
 * measurement. */
gs_catalogue_fit_t gs_catalogue_fit(long long bytes,
                                    const gs_detect_report_t *machine);

/* The answer written out, as in "card or processor". Used where there is
 * room to read a phrase. */
const char *gs_catalogue_fit_name(gs_catalogue_fit_t fit);

/* The same answer in a few letters, for a column in a list of a thousand
 * rows where a phrase would push the model name off the edge. Seven
 * characters at most. */
const char *gs_catalogue_fit_tag(gs_catalogue_fit_t fit);

/* Whether a model of this size could be kept on this machine. One file
 * sits on one disk, so the largest single disk decides. Asked when a
 * model would have to be fetched, and left unasked for one already on the
 * disk, which needs no free space to run. */
int gs_catalogue_storable(long long bytes,
                          const gs_detect_report_t *machine);

/* Whether a row of this size belongs in the list a person picks a model
 * to fetch from. It has to run somewhere, it has to fit on a disk, and a
 * divided one has to put seven tenths of itself on the card, since a
 * model with more than that outside would load and then answer at a word
 * a minute, which is a download wasted.
 *
 * A model already on the disk is never asked this. It runs however slowly
 * it runs, and gs_catalogue_fit alone says where. */
int gs_catalogue_listed(long long bytes, const gs_detect_report_t *machine);

/* That seven tenths written as a percentage, so a panel naming the rule
 * to a person reads it from the same place the rule is applied. */
#define GS_CATALOGUE_SPLIT_FLOOR 70

/* Whether a model of this size answers at conversational speed. Only a
 * model held entirely in graphics memory does, because anything left in
 * system memory crosses the bus once per word, and every number in the
 * file is read once per word. */
int gs_catalogue_conversational(gs_catalogue_fit_t fit);

/* How a model too large for either store on its own would be divided.
 * The card is filled first, since every byte left in system memory
 * crosses the bus once for every word produced, so the share on the card
 * is what decides the speed.
 *
 * The figures are bytes of the model plus its working space. A real run
 * moves whole layers rather than bytes, so the division lands on the
 * layer boundary nearest these numbers. The layer count is not in the
 * snapshot, so how near is unmeasured.
 *
 * Returns GS_OK only for a model that splits, leaving both figures at
 * nought otherwise. */
int gs_catalogue_share(long long bytes, const gs_detect_report_t *machine,
                       long long *on_card, long long *in_memory);

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
