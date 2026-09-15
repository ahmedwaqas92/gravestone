#include "paths.h"
#include "gravestone.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <sys/types.h>

#define GS_APP_DIR "gravestone"

static char override_dir[512];

void gs_paths_override(const char *dir)
{
    if (dir == NULL) {
        override_dir[0] = '\0';
        return;
    }
    strncpy(override_dir, dir, sizeof override_dir - 1);
    override_dir[sizeof override_dir - 1] = '\0';
}

/* Creates one directory, treating an existing one as success. 0700 keeps
 * other accounts on the machine out, which matters because the database
 * holds task descriptions and generated code in plain text. */
static int ensure_dir(const char *path)
{
    if (gs_paths_make_dir(path) == GS_OK)
        return GS_OK;
    gs_log_error("paths: cannot create %s: %s", path, strerror(errno));
    return GS_ERR_IO;
}

/* Creates every directory along the path, since the parent may be missing
 * on a fresh account. */
static int ensure_tree(const char *path)
{
    char work[512];
    size_t i;

    strncpy(work, path, sizeof work - 1);
    work[sizeof work - 1] = '\0';

    for (i = 1; work[i] != '\0'; i++) {
        if (work[i] != '/')
            continue;
        work[i] = '\0';
        if (ensure_dir(work) != GS_OK)
            return GS_ERR_IO;
        work[i] = '/';
    }
    return ensure_dir(work);
}

int gs_paths_make_dir(const char *path)
{
    int made;

    if (path == NULL || path[0] == '\0')
        return GS_ERR_ARG;

#ifdef _WIN32
    made = _mkdir(path);
#else
    made = mkdir(path, 0700);
#endif
    if (made == 0 || errno == EEXIST)
        return GS_OK;
    return GS_ERR_IO;
}

int gs_paths_data_dir(char *out, size_t cap)
{
    const char *base;

    if (out == NULL || cap < 2)
        return GS_ERR_ARG;

    if (override_dir[0] != '\0') {
        snprintf(out, cap, "%s", override_dir);
        return ensure_tree(out);
    }

#if defined(_WIN32)
    base = getenv("LOCALAPPDATA");
    if (base == NULL || base[0] == '\0') {
        gs_log_error("paths: LOCALAPPDATA is not set");
        return GS_ERR;
    }
    snprintf(out, cap, "%s\\%s", base, GS_APP_DIR);
#elif defined(__APPLE__)
    base = getenv("HOME");
    if (base == NULL || base[0] == '\0') {
        gs_log_error("paths: HOME is not set");
        return GS_ERR;
    }
    snprintf(out, cap, "%s/Library/Application Support/%s", base, GS_APP_DIR);
#else
    base = getenv("XDG_DATA_HOME");
    if (base != NULL && base[0] != '\0') {
        snprintf(out, cap, "%s/%s", base, GS_APP_DIR);
    } else {
        base = getenv("HOME");
        if (base == NULL || base[0] == '\0') {
            gs_log_error("paths: neither XDG_DATA_HOME nor HOME is set");
            return GS_ERR;
        }
        snprintf(out, cap, "%s/.local/share/%s", base, GS_APP_DIR);
    }
#endif

    return ensure_tree(out);
}

int gs_paths_db_file(char *out, size_t cap)
{
    char dir[512];

    if (out == NULL || cap < 2)
        return GS_ERR_ARG;
    if (gs_paths_data_dir(dir, sizeof dir) != GS_OK)
        return GS_ERR;

    snprintf(out, cap, "%s/gravestone.sqlite", dir);
    return GS_OK;
}
