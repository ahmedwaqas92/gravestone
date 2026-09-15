/* store_settings.c
 *
 * What the interface remembers between sessions. Settings are text
 * against a key, and the model inventory is what the last walk of a disk
 * found there.
 *
 * Kept apart from store.c so neither file grows past what fits in a head.
 */
#include "store.h"
#include "store_internal.h"
#include "gravestone.h"
#include "db.h"
#include "log.h"
#include "str.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GS_STORE_MODEL_MAX 512

static gs_store_model_t models[GS_STORE_MODEL_MAX];
static int model_count;

int gs_store_setting_put(const char *key, const char *value)
{
    gs_db_t *handle = gs_store_handle();
    gs_db_stmt_t *stmt;
    int rc;

    if (handle == NULL || key == NULL || value == NULL || key[0] == '\0')
        return GS_ERR_ARG;

    stmt = gs_db_prepare(handle,
        "INSERT INTO setting (key, value, updated_at) VALUES (?, ?, ?)"
        " ON CONFLICT(key) DO UPDATE SET value = excluded.value,"
        " updated_at = excluded.updated_at;");
    if (stmt == NULL)
        return GS_ERR;

    gs_db_bind_text(stmt, 1, key);
    gs_db_bind_text(stmt, 2, value);
    gs_db_bind_int(stmt, 3, (long long)time(NULL));
    rc = gs_db_step(stmt) < 0 ? GS_ERR : GS_OK;
    gs_db_finalise(stmt);
    return rc;
}

int gs_store_setting_get(const char *key, char *out, size_t cap)
{
    gs_db_t *handle = gs_store_handle();
    gs_db_stmt_t *stmt;
    int rc = GS_ERR;

    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (handle == NULL || key == NULL)
        return GS_ERR_ARG;

    stmt = gs_db_prepare(handle, "SELECT value FROM setting WHERE key = ?;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_text(stmt, 1, key);
    if (gs_db_step(stmt) == 1) {
        gs_str_copy(out, cap, gs_db_text(stmt, 0));
        rc = GS_OK;
    }
    gs_db_finalise(stmt);
    return rc;
}

int gs_store_setting_count(void)
{
    gs_db_t *handle = gs_store_handle();
    gs_db_stmt_t *stmt;
    int n = 0;

    if (handle == NULL)
        return 0;
    stmt = gs_db_prepare(handle, "SELECT count(*) FROM setting;");
    if (stmt == NULL)
        return 0;
    if (gs_db_step(stmt) == 1)
        n = (int)gs_db_int(stmt, 0);
    gs_db_finalise(stmt);
    return n;
}

int gs_store_model_remember(const char *name, const char *path,
                            const char *root, long long bytes)
{
    gs_db_t *handle = gs_store_handle();
    gs_db_stmt_t *stmt;
    int rc;

    if (handle == NULL || name == NULL || path == NULL || root == NULL)
        return GS_ERR_ARG;
    if (name[0] == '\0' || bytes < 0)
        return GS_ERR_ARG;

    stmt = gs_db_prepare(handle,
        "INSERT INTO known_model (name, path, root, bytes, seen_at)"
        " VALUES (?, ?, ?, ?, ?)"
        " ON CONFLICT(name) DO UPDATE SET path = excluded.path,"
        " root = excluded.root, bytes = excluded.bytes,"
        " seen_at = excluded.seen_at;");
    if (stmt == NULL)
        return GS_ERR;

    gs_db_bind_text(stmt, 1, name);
    gs_db_bind_text(stmt, 2, path);
    gs_db_bind_text(stmt, 3, root);
    gs_db_bind_int(stmt, 4, bytes);
    gs_db_bind_int(stmt, 5, (long long)time(NULL));
    rc = gs_db_step(stmt) < 0 ? GS_ERR : GS_OK;
    gs_db_finalise(stmt);
    return rc;
}

int gs_store_model_forget_all(void)
{
    gs_db_t *handle = gs_store_handle();

    if (handle == NULL)
        return GS_ERR;
    model_count = 0;
    return gs_db_exec(handle, "DELETE FROM known_model;");
}

int gs_store_model_load(void)
{
    gs_db_t *handle = gs_store_handle();
    gs_db_stmt_t *stmt;

    model_count = 0;
    if (handle == NULL)
        return 0;

    stmt = gs_db_prepare(handle,
        "SELECT name, path, root, bytes FROM known_model"
        " ORDER BY bytes DESC;");
    if (stmt == NULL)
        return 0;

    while (gs_db_step(stmt) == 1 && model_count < GS_STORE_MODEL_MAX) {
        gs_store_model_t *m = &models[model_count];

        gs_str_copy(m->name, sizeof m->name, gs_db_text(stmt, 0));
        gs_str_copy(m->path, sizeof m->path, gs_db_text(stmt, 1));
        gs_str_copy(m->root, sizeof m->root, gs_db_text(stmt, 2));
        m->bytes = gs_db_int(stmt, 3);
        model_count++;
    }
    gs_db_finalise(stmt);
    return model_count;
}

int gs_store_model_count(void)
{
    return model_count;
}

const gs_store_model_t *gs_store_model_at(int index)
{
    if (index < 0 || index >= model_count)
        return NULL;
    return &models[index];
}
