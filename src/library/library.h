/* library.h
 *
 * The models already sitting on a disk. A model file carries the GGUF
 * extension, which is the single file format llama.cpp reads, so finding
 * them means walking a directory for that suffix.
 */
#ifndef GS_LIBRARY_H
#define GS_LIBRARY_H

#include <stddef.h>
#include "gravestone.h"

#define GS_LIBRARY_MAX 256

typedef struct {
    char      name[160];     /* the file, without its directory */
    char      path[512];     /* where it sits */
    long long bytes;
    /* How many tokens the model can hold at once, read from the file's
     * own header. A token is a chunk of text, roughly three or four
     * characters of English. Zero when the header would not say. */
    long long context;
} gs_library_entry_t;

/* Walks the models directory under root and records what it finds.
 * Returns how many models were seen. A missing directory reads as none,
 * since a disk nobody has used yet holds nothing. */
int gs_library_scan(const char *root);

int gs_library_count(void);
const gs_library_entry_t *gs_library_at(int index);
long long gs_library_bytes(void);

/* The directory models live in, under the disk chosen for them. */
int gs_library_dir(const char *root, char *out, size_t cap);

/* Removes one model from the disk. A model from the Ollama store is
 * removed by asking ollama itself, since layers are shared between tags
 * and only ollama knows which blobs nothing references any more. A file
 * gravestone downloaded is simply deleted. The list is not rescanned
 * here, so the caller walks the disk again afterwards.
 *
 * The space given back can be less than the model's size, since two tags
 * pointing at the same layer keep that layer alive. */
int gs_library_remove(int index);

/* Pulls a model onto the disk through ollama, which downloads it into
 * its own store. The model directories under root are made first when
 * they are missing, so a fresh disk needs no hand preparation. Blocks
 * until the download finishes, so callers run it off the main thread. */
int gs_library_pull(const char *name, const char *root);

/* The byte count of the pull now running, for a progress bar. Completed
 * and total come straight from ollama's own reports, and both read zero
 * outside a pull. */
void gs_library_pull_progress(long long *completed, long long *total);

/* Reads one progress line of ollama's pull stream. Public so a test can
 * feed it poison, and called for every line a real pull sends. */
void gs_library_pull_note_line(const char *line);

/* How many bytes of half finished download sit in the store on this
 * disk. Ollama grows partial files in blobs/ while it pulls, so this
 * number over the expected size is a measured download percentage. */
long long gs_library_partial_bytes(const char *root);

/* Reads the context window out of one GGUF file, the single file format
 * llama.cpp reads. Zero when the file carries no such header. */
long long gs_library_read_context(const char *path);

void gs_library_release(void);

extern const gs_module gs_library_module;

#endif
