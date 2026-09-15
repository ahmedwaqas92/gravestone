#include "library.h"
#include "gravestone.h"
#include "http.h"
#include "paths.h"
#include "log.h"
#include "proc.h"
#include "str.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#define OLLAMA_SUBDIR "models"
#define OLLAMA_WEIGHTS "application/vnd.ollama.image.model"

#define GS_LIBRARY_SUBDIR "gravestone-models"

static gs_library_entry_t entries[GS_LIBRARY_MAX];
static int entry_count;
static long long total_bytes;

/* Only the characters Ollama itself puts in a model name. The name goes
 * through a shell, and a manifest is a file on disk anyone could have
 * written, so anything stranger than a name is refused outright. */
static int name_is_clean(const char *name)
{
    size_t i;

    if (name[0] == '\0' || name[0] == '-')
        return 0;
    for (i = 0; name[i] != '\0'; i++) {
        char c = name[i];

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' ||
            c == '-' || c == ':')
            continue;
        return 0;
    }
    return 1;
}

int gs_library_remove(int index)
{
    const gs_library_entry_t *entry = gs_library_at(index);
    size_t n;

    if (entry == NULL)
        return GS_ERR_ARG;

    /* A blob path marks a model that belongs to Ollama's store. */
    if (strstr(entry->path, "/blobs/sha256-") != NULL) {
        char command[256];
        char answer[256];

        if (!name_is_clean(entry->name)) {
            gs_log_warn("library: refusing to pass \"%s\" to a shell",
                        entry->name);
            return GS_ERR_ARG;
        }
        if (!gs_proc_exists("ollama")) {
            gs_log_warn("library: ollama is not installed, so its models "
                        "cannot be removed from here");
            return GS_ERR;
        }
        snprintf(command, sizeof command, "ollama rm %s", entry->name);
        /* The reason is kept rather than thrown away, since ollama rm
         * talks to the server rather than to the disk, and a server that
         * is not running is the commonest reason a removal fails. */
        if (gs_proc_reason(command, answer, sizeof answer) != GS_OK) {
            char *end = strchr(answer, '\n');

            if (end != NULL)
                *end = '\0';
            if (answer[0] != '\0')
                gs_log_warn("library: ollama would not remove %s: %s",
                            entry->name, answer);
            else
                gs_log_warn("library: ollama would not remove %s, and "
                            "said nothing about why", entry->name);
            return GS_ERR;
        }
        gs_log_info("library: removed %s through ollama", entry->name);
        return GS_OK;
    }

    /* A file gravestone downloaded sits under gravestone-models with the
     * gguf suffix, and anything else is nothing this function owns. */
    n = strlen(entry->path);
    if (strstr(entry->path, "/" GS_LIBRARY_SUBDIR "/") == NULL ||
        n < 5 || strcmp(entry->path + n - 5, ".gguf") != 0)
        return GS_ERR_ARG;

    if (unlink(entry->path) != 0) {
        gs_log_warn("library: %s would not delete", entry->path);
        return GS_ERR_IO;
    }
    gs_log_info("library: deleted %s", entry->path);
    return GS_OK;
}

/* Progress of the pull in flight. The stream reports every layer with
 * its own total, and the weights layer is the largest, so its numbers
 * are the ones a bar should show. */
static atomic_llong pull_completed;
static atomic_llong pull_total;
static int pull_ok;

void gs_library_pull_progress(long long *completed, long long *total)
{
    if (completed != NULL)
        *completed = atomic_load(&pull_completed);
    if (total != NULL)
        *total = atomic_load(&pull_total);
}

/* Pulls a number out of a line of machine written JSON, the digits after
 * a fixed marker such as "total":. Minus one when the marker is absent. */
static long long field_after(const char *line, const char *marker)
{
    const char *at = strstr(line, marker);

    if (at == NULL)
        return -1;
    return atoll(at + strlen(marker));
}

void gs_library_pull_note_line(const char *line)
{
    long long total, completed;

    if (line == NULL)
        return;
    if (strstr(line, "\"status\":\"success\"") != NULL) {
        pull_ok = 1;
        return;
    }
    total = field_after(line, "\"total\":");
    completed = field_after(line, "\"completed\":");
    if (total <= 0)
        return;
    if (total > atomic_load(&pull_total))
        atomic_store(&pull_total, total);
    /* Only the biggest layer drives the bar, so smaller layers finishing
     * late can never drag a nearly done bar backwards. */
    if (total == atomic_load(&pull_total) && completed >= 0) {
        if (completed > total)
            completed = total;
        atomic_store(&pull_completed, completed);
    }
}

static int pull_stream_line(const char *line, void *context)
{
    (void)context;
    if (strstr(line, "\"error\"") != NULL) {
        gs_log_warn("library: the pull reported: %.200s", line);
        return 1;
    }
    gs_library_pull_note_line(line);
    return 0;
}

int gs_library_pull(const char *name, const char *root)
{
    char command[256];
    char answer[512];
    char dir[512];

    if (name == NULL || !name_is_clean(name)) {
        gs_log_warn("library: refusing to pass \"%s\" to a shell",
                    name != NULL ? name : "(null)");
        return GS_ERR_ARG;
    }

    /* The store directories exist before anything lands in them. EEXIST
     * is the good case on every disk after the first. */
    if (root != NULL && root[0] != '\0' && strlen(root) < 300) {
        snprintf(dir, sizeof dir, "%s/%s",
                 strcmp(root, "/") == 0 ? "" : root, OLLAMA_SUBDIR);
        (void)gs_paths_make_dir(dir);
        if (gs_library_dir(root, dir, sizeof dir) == GS_OK)
            (void)gs_paths_make_dir(dir);
    }

    /* The daemon reports exact byte counts while it pulls, which is
     * what the progress bar draws, so the daemon is asked first and the
     * command line is only the fallback when nothing answers on the
     * daemon's port. */
    atomic_store(&pull_completed, 0);
    atomic_store(&pull_total, 0);
    pull_ok = 0;

    snprintf(command, sizeof command, "{\"model\":\"%s\"}", name);
    if (gs_http_post_stream("127.0.0.1", 11434, "/api/pull", command,
                            pull_stream_line, NULL) == GS_OK) {
        if (pull_ok) {
            gs_log_info("library: pulled %s", name);
            return GS_OK;
        }
        gs_log_warn("library: the daemon would not pull %s", name);
        return GS_ERR;
    }

    if (!gs_proc_exists("ollama")) {
        gs_log_warn("library: no daemon answered and ollama is not "
                    "installed, so nothing can be pulled");
        return GS_ERR;
    }
    snprintf(command, sizeof command, "ollama pull %s", name);
    if (gs_proc_capture(command, answer, sizeof answer) != GS_OK) {
        gs_log_warn("library: ollama would not pull %s", name);
        return GS_ERR;
    }
    gs_log_info("library: pulled %s", name);
    return GS_OK;
}

long long gs_library_partial_bytes(const char *root)
{
    char dir[512];
    DIR *d;
    struct dirent *item;
    long long sum = 0;

    if (root == NULL || strlen(root) > 300)
        return 0;
    snprintf(dir, sizeof dir, "%s/%s/blobs",
             strcmp(root, "/") == 0 ? "" : root, OLLAMA_SUBDIR);

    d = opendir(dir);
    if (d == NULL)
        return 0;
    while ((item = readdir(d)) != NULL) {
        char full[1024];
        struct stat st;

        if (strstr(item->d_name, "-partial") == NULL)
            continue;
        snprintf(full, sizeof full, "%s/%s", dir, item->d_name);
        if (stat(full, &st) == 0 && S_ISREG(st.st_mode))
            sum += (long long)st.st_size;
    }
    closedir(d);
    return sum;
}

int gs_library_dir(const char *root, char *out, size_t cap)
{
    if (root == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;

    /* A root of "/" would otherwise give a doubled slash. */
    if (strcmp(root, "/") == 0)
        snprintf(out, cap, "/%s", GS_LIBRARY_SUBDIR);
    else
        snprintf(out, cap, "%s/%s", root, GS_LIBRARY_SUBDIR);
    return GS_OK;
}

static int ends_with_gguf(const char *name)
{
    size_t n = strlen(name);

    return n > 5 && strcmp(name + n - 5, ".gguf") == 0;
}

/* Records one model, refusing nothing except a full table. */
static void remember(const char *name, const char *path, long long bytes)
{
    if (entry_count >= GS_LIBRARY_MAX)
        return;
    gs_str_copy(entries[entry_count].name, sizeof entries[0].name, name);
    gs_str_copy(entries[entry_count].path, sizeof entries[0].path, path);
    entries[entry_count].bytes = bytes;
    entries[entry_count].context = gs_library_read_context(path);
    total_bytes += bytes;
    entry_count++;
}

/* Reads one Ollama manifest, which is a small JSON file naming the pieces
 * a model is made of. The weights piece is marked with its own media type,
 * and its digest names a file in blobs/ that is a plain GGUF file wearing
 * a checksum for a name. No JSON library is used, since the two fields
 * needed sit right after a fixed marker in a file Ollama writes by
 * machine, and a manifest that does not match this shape is skipped. */
static void read_manifest(const char *store, const char *manifest_path,
                          const char *model, const char *tag)
{
    FILE *f = fopen(manifest_path, "rb");
    char text[65536];
    char blob[400];
    char name[160];
    char hex[80];
    size_t got;
    struct stat st;
    const char *at;
    long long bytes = 0;
    int i;

    if (f == NULL)
        return;
    got = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[got] = '\0';

    at = strstr(text, OLLAMA_WEIGHTS);
    if (at == NULL)
        return;
    at = strstr(at, "\"digest\":\"sha256:");
    if (at == NULL)
        return;
    at += strlen("\"digest\":\"sha256:");
    for (i = 0; i < (int)sizeof hex - 1 && at[i] != '\0' && at[i] != '\"';
         i++)
        hex[i] = at[i];
    hex[i] = '\0';

    at = strstr(at, "\"size\":");
    if (at != NULL)
        bytes = atoll(at + strlen("\"size\":"));

    snprintf(blob, sizeof blob, "%s/blobs/sha256-%s", store, hex);
    if (stat(blob, &st) == 0 && S_ISREG(st.st_mode))
        bytes = (long long)st.st_size;
    else {
        gs_log_debug("library: %s names a blob that is not there", model);
        return;
    }

    snprintf(name, sizeof name, "%s:%s", model, tag);
    remember(name, blob, bytes);
}

/* Walks <root>/models the way Ollama lays it out, which is
 * manifests/<registry>/<namespace>/<model>/<tag> with the weights in
 * blobs/. Ollama is the inference server this project talks to anyway,
 * so what it has already pulled counts as models on the disk. */
static void scan_ollama(const char *root)
{
    char store[256];
    char manifests[300];
    char level0[600];
    char level1[900];
    char level2[1200];
    DIR *d0, *d1, *d2, *d3;
    struct dirent *e0, *e1, *e2, *e3;

    if (strlen(root) > 128)
        return;
    if (strcmp(root, "/") == 0)
        snprintf(store, sizeof store, "/%s", OLLAMA_SUBDIR);
    else
        snprintf(store, sizeof store, "%s/%s", root, OLLAMA_SUBDIR);
    snprintf(manifests, sizeof manifests, "%s/manifests", store);

    d0 = opendir(manifests);
    if (d0 == NULL)
        return;

    while ((e0 = readdir(d0)) != NULL) {
        if (e0->d_name[0] == '.')
            continue;
        snprintf(level0, sizeof level0, "%s/%s", manifests,
                 e0->d_name);
        d1 = opendir(level0);
        if (d1 == NULL)
            continue;
        while ((e1 = readdir(d1)) != NULL) {
            if (e1->d_name[0] == '.')
                continue;
            snprintf(level1, sizeof level1, "%s/%s", level0,
                     e1->d_name);
            d2 = opendir(level1);
            if (d2 == NULL)
                continue;
            while ((e2 = readdir(d2)) != NULL) {
                if (e2->d_name[0] == '.')
                    continue;
                snprintf(level2, sizeof level2, "%s/%s", level1,
                         e2->d_name);
                d3 = opendir(level2);
                if (d3 == NULL)
                    continue;
                while ((e3 = readdir(d3)) != NULL) {
                    char manifest[1500];
                    struct stat st;

                    if (e3->d_name[0] == '.')
                        continue;
                    snprintf(manifest, sizeof manifest, "%s/%s", level2,
                             e3->d_name);
                    if (stat(manifest, &st) == 0 && S_ISREG(st.st_mode))
                        read_manifest(store, manifest, e2->d_name,
                                      e3->d_name);
                }
                closedir(d3);
            }
            closedir(d2);
        }
        closedir(d1);
    }
    closedir(d0);
}

int gs_library_scan(const char *root)
{
    char dir[512];
    DIR *d;
    struct dirent *item;

    gs_library_release();

    if (root == NULL)
        return 0;

    scan_ollama(root);

    if (gs_library_dir(root, dir, sizeof dir) != GS_OK)
        return entry_count;

    d = opendir(dir);
    if (d == NULL) {
        gs_log_debug("library: %s holds no downloaded models yet", dir);
        return entry_count;
    }

    while ((item = readdir(d)) != NULL && entry_count < GS_LIBRARY_MAX) {
        char full[1024];
        struct stat st;

        if (!ends_with_gguf(item->d_name))
            continue;
        snprintf(full, sizeof full, "%s/%s", dir, item->d_name);
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        remember(item->d_name, full, (long long)st.st_size);
    }
    closedir(d);

    gs_log_debug("library: %d models in %s", entry_count, dir);
    return entry_count;
}

int gs_library_count(void)
{
    return entry_count;
}

const gs_library_entry_t *gs_library_at(int index)
{
    if (index < 0 || index >= entry_count)
        return NULL;
    return &entries[index];
}

long long gs_library_bytes(void)
{
    return total_bytes;
}

void gs_library_release(void)
{
    memset(entries, 0, sizeof entries);
    entry_count = 0;
    total_bytes = 0;
}

static int library_init(void)
{
    return GS_OK;
}

static int library_run(int argc, char **argv)
{
    const char *root = argc > 1 && argv[1] != NULL ? argv[1] : "/";
    char dir[512];
    int i, n;

    gs_library_dir(root, dir, sizeof dir);
    n = gs_library_scan(root);

    printf("root\t%s\n", root);
    printf("directory\t%s\n", dir);
    printf("models\t%d\n", n);
    printf("bytes\t%lld\n", gs_library_bytes());

    for (i = 0; i < n; i++) {
        const gs_library_entry_t *e = gs_library_at(i);
        printf("model\t%lld\t%lld\t%s\n", e->bytes, e->context, e->name);
    }

    gs_library_release();
    return GS_OK;
}

static void library_shutdown(void)
{
    gs_library_release();
}

const gs_module gs_library_module = {
    "library",
    "list the models already on a disk",
    library_init,
    library_run,
    library_shutdown
};
