/* library_test.c
 *
 * Walking a disk for model files. The test builds its own directory under
 * the scratch area it is given, so nothing outside it is read or written.
 */
#include "library.h"
#include "gravestone.h"
#include "log.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static void write_file(const char *path, size_t bytes)
{
    FILE *f = fopen(path, "wb");
    size_t i;

    if (f == NULL)
        return;
    for (i = 0; i < bytes; i++)
        fputc('x', f);
    fclose(f);
}

static void test_contract(void)
{
    printf("module contract\n");
    check(gs_library_module.name != NULL, "name is set");
    check(strcmp(gs_library_module.name, "library") == 0,
          "name is \"library\"");
    check(gs_library_module.run != NULL, "run is set");
}

static void test_directory_names(void)
{
    char out[512];

    printf("where models live on a disk\n");

    check(gs_library_dir("/mnt/d", out, sizeof out) == GS_OK,
          "a plain root is accepted");
    check(strcmp(out, "/mnt/d/gravestone-models") == 0,
          "the models directory hangs under the root");

    check(gs_library_dir("/", out, sizeof out) == GS_OK,
          "the filesystem root is accepted");
    check(strcmp(out, "/gravestone-models") == 0,
          "and gives no doubled slash");

    check(gs_library_dir(NULL, out, sizeof out) == GS_ERR_ARG,
          "a null root is refused");
    check(gs_library_dir("/mnt/d", NULL, sizeof out) == GS_ERR_ARG,
          "a null buffer is refused");
    check(gs_library_dir("/mnt/d", out, 0) == GS_ERR_ARG,
          "a buffer with no room is refused");
}

static void test_scanning(const char *root)
{
    char dir[512];
    char path[768];

    printf("walking a disk\n");

    check(gs_library_scan(NULL) == 0, "a null root holds nothing");
    check(gs_library_scan("/no/such/place/at/all") == 0,
          "a root that does not exist holds nothing");
    check(gs_library_count() == 0, "and the list is left empty");
    check(gs_library_at(0) == NULL, "with no row to read");

    if (gs_library_dir(root, dir, sizeof dir) != GS_OK) {
        check(0, "the models directory name was built");
        return;
    }
    if (mkdir(dir, 0700) != 0) {
        printf("  skip  the scratch directory could not be made\n");
        return;
    }

    check(gs_library_scan(root) == 0, "an empty directory holds no models");

    snprintf(path, sizeof path, "%s/alpha.gguf", dir);
    write_file(path, 100);
    snprintf(path, sizeof path, "%s/beta.gguf", dir);
    write_file(path, 250);
    snprintf(path, sizeof path, "%s/notes.txt", dir);
    write_file(path, 999);
    snprintf(path, sizeof path, "%s/.gguf", dir);
    write_file(path, 7);

    check(gs_library_scan(root) == 2, "two model files were found");
    check(gs_library_count() == 2, "and the count agrees");
    check(gs_library_bytes() == 350, "their sizes are added up");
    check(gs_library_at(0) != NULL, "the first row is readable");
    check(gs_library_at(2) == NULL, "one past the end gives nothing");
    check(gs_library_at(-1) == NULL, "a negative index gives nothing");

    if (gs_library_at(0) != NULL) {
        const gs_library_entry_t *e = gs_library_at(0);

        check(e->name[0] != '\0', "a row carries a name");
        check(strstr(e->path, "gravestone-models") != NULL,
              "and the path it was found at");
        check(e->bytes > 0, "and its size");
    }

    /* Scanning again must replace the list rather than add to it. */
    check(gs_library_scan(root) == 2, "a second walk reports the same two");
    check(gs_library_bytes() == 350, "and the same total");

    gs_library_release();
    check(gs_library_count() == 0, "releasing empties the list");

    snprintf(path, sizeof path, "%s/alpha.gguf", dir);
    unlink(path);
    snprintf(path, sizeof path, "%s/beta.gguf", dir);
    unlink(path);
    snprintf(path, sizeof path, "%s/notes.txt", dir);
    unlink(path);
    snprintf(path, sizeof path, "%s/.gguf", dir);
    unlink(path);
    rmdir(dir);
}

/* Builds a small Ollama store by hand and checks the walk reads it the
 * way Ollama writes it: names from the manifest path, bytes from the
 * blob file, and a manifest naming a missing blob left out. */
static void test_ollama_store(const char *root)
{
    char base[600];
    char path[1400];
    static const char manifest[] =
        "{\"schemaVersion\":2,\"layers\":["
        "{\"mediaType\":\"application/vnd.ollama.image.model\","
        "\"digest\":\"sha256:aabb\",\"size\":999},"
        "{\"mediaType\":\"application/vnd.ollama.image.license\","
        "\"digest\":\"sha256:ccdd\",\"size\":5}]}";
    static const char broken[] =
        "{\"layers\":[{\"mediaType\":"
        "\"application/vnd.ollama.image.model\","
        "\"digest\":\"sha256:eeff\",\"size\":7}]}";
    FILE *f;

    printf("a store the way Ollama lays it out\n");

    snprintf(base, sizeof base, "%s/models", root);
    snprintf(path, sizeof path,
             "%s/manifests/registry.ollama.ai/library/fake-model", base);
    if (system(NULL) == 0) {
        printf("  skip  no shell to build the store with\n");
        return;
    }
    snprintf(path, sizeof path,
             "mkdir -p %s/manifests/registry.ollama.ai/library/fake-model "
             "%s/blobs", base, base);
    if (system(path) != 0) {
        check(0, "the store directories were made");
        return;
    }

    snprintf(path, sizeof path,
             "%s/manifests/registry.ollama.ai/library/fake-model/3b", base);
    f = fopen(path, "w");
    if (f != NULL) {
        fputs(manifest, f);
        fclose(f);
    }
    snprintf(path, sizeof path,
             "%s/manifests/registry.ollama.ai/library/fake-model/broken",
             base);
    f = fopen(path, "w");
    if (f != NULL) {
        fputs(broken, f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/blobs/sha256-aabb", base);
    write_file(path, 1234);

    check(gs_library_scan(root) == 1, "one model was read from the store");
    if (gs_library_at(0) != NULL) {
        const gs_library_entry_t *e = gs_library_at(0);

        check(strcmp(e->name, "fake-model:3b") == 0,
              "its name joins the model and the tag");
        check(e->bytes == 1234,
              "its size comes off the blob file rather than the manifest");
        check(strstr(e->path, "blobs/sha256-aabb") != NULL,
              "its path points into blobs/");
    }
    check(gs_library_count() == 1,
          "the manifest naming a missing blob was left out");

    /* A model downloaded by gravestone itself sits beside the store. */
    snprintf(path, sizeof path, "%s/gravestone-models", root);
    if (mkdir(path, 0700) == 0) {
        snprintf(path, sizeof path, "%s/gravestone-models/own.gguf", root);
        write_file(path, 55);
        check(gs_library_scan(root) == 2,
              "both sources are read in one walk");
        check(gs_library_bytes() == 1234 + 55,
              "the total spans both sources");
        snprintf(path, sizeof path, "rm -rf %s/gravestone-models", root);
        (void)system(path);
    }

    snprintf(path, sizeof path, "rm -rf %s/models", root);
    (void)system(path);
    gs_library_release();
}

/* Removal attacked with hostile names, dead indexes and double frees of
 * the same file. A manifest is a file on disk anyone could have written,
 * so a tag carrying shell syntax has to die at the gate rather than
 * reach the shell. */
static void test_remove_under_attack(const char *root)
{
    char base[600];
    char path[1500];
    char marker[700];
    static const char manifest[] =
        "{\"layers\":[{\"mediaType\":"
        "\"application/vnd.ollama.image.model\","
        "\"digest\":\"sha256:feed\",\"size\":50}]}";
    FILE *f;
    int n, i, hostile = -1, clean = -1, gguf = -1;

    printf("removal under attack\n");

    snprintf(base, sizeof base, "%s/models", root);
    snprintf(marker, sizeof marker, "%s/pwned", root);
    remove(marker);

    /* One clean tag, and one whose name would write a file if it ever
     * reached the shell. */
    snprintf(path, sizeof path,
             "mkdir -p '%s/manifests/registry.ollama.ai/library/fake-model' "
             "'%s/blobs'", base, base);
    if (system(path) != 0) {
        check(0, "the store directories were made");
        return;
    }
    for (i = 0; i < 2; i++) {
        snprintf(path, sizeof path,
                 "%s/manifests/registry.ollama.ai/library/fake-model/%s",
                 base, i == 0 ? "3b" : "3b;touch pwned");
        f = fopen(path, "w");
        if (f != NULL) {
            fputs(manifest, f);
            fclose(f);
        }
    }
    snprintf(path, sizeof path, "%s/blobs/sha256-feed", base);
    write_file(path, 50);

    snprintf(path, sizeof path, "%s/gravestone-models", root);
    (void)mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/gravestone-models/mine.gguf", root);
    write_file(path, 70);

    n = gs_library_scan(root);
    check(n == 3, "both tags and the plain file were all seen");
    for (i = 0; i < n; i++) {
        const gs_library_entry_t *e = gs_library_at(i);

        if (strchr(e->name, ';') != NULL)
            hostile = i;
        else if (strcmp(e->name, "fake-model:3b") == 0)
            clean = i;
        else if (strcmp(e->name, "mine.gguf") == 0)
            gguf = i;
    }
    check(hostile >= 0 && clean >= 0 && gguf >= 0,
          "and each of the three was recognised");

    /* The hostile tag dies at the gate. The marker file would exist if
     * the name had reached a shell. */
    check(gs_library_remove(hostile) == GS_ERR_ARG,
          "a name carrying shell syntax is refused");
    check(access(marker, F_OK) != 0, "and nothing it named was run");

    /* A clean name goes to ollama, which does not know the made up
     * model, so the answer is an error and the store is untouched. */
    check(gs_library_remove(clean) != GS_OK,
          "a made up model is refused by ollama or by its absence");
    snprintf(path, sizeof path, "%s/blobs/sha256-feed", base);
    check(access(path, F_OK) == 0, "and its blob is still there");

    check(gs_library_remove(-1) == GS_ERR_ARG, "a negative index is refused");
    check(gs_library_remove(n) == GS_ERR_ARG, "one past the end is refused");

    /* The plain file really goes, and a second pull on the same trigger
     * fails without damage. */
    check(gs_library_remove(gguf) == GS_OK, "the plain file was deleted");
    snprintf(path, sizeof path, "%s/gravestone-models/mine.gguf", root);
    check(access(path, F_OK) != 0, "and it is gone from the disk");
    check(gs_library_remove(gguf) == GS_ERR_IO,
          "removing it twice fails cleanly");
    check(gs_library_scan(root) == 2, "a rescan shows the two that remain");

    snprintf(path, sizeof path, "rm -rf '%s/models' '%s/gravestone-models'",
             root, root);
    (void)system(path);
    gs_library_release();
}

int main(int argc, char **argv)
{
    /* The build directory is inside the project and already disposable,
     * so the walk happens there rather than anywhere the user keeps
     * anything. A different root can be passed as the first argument. */
    const char *root = argc > 1 ? argv[1] : "build";

    gs_log_set_level(GS_LOG_ERROR);

    test_contract();
    test_directory_names();
    test_scanning(root);
    test_ollama_store(root);
    test_remove_under_attack(root);

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
