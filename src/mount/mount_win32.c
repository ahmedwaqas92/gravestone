/* mount_win32.c
 *
 * Windows reaches its own drives by letter without anything being
 * mounted, so there is nothing here to put back. Every call answers the
 * way an empty machine would, which is what lets the rest of the module
 * be written once.
 */
#include "mount.h"
#include "mount_internal.h"

#include "gravestone.h"

#include <stddef.h>
#include <string.h>

const char *gs_mount_first_real(const char *const *paths, int count)
{
    (void)paths;
    (void)count;
    return NULL;
}

int gs_mount_kernel_is_windows(void)
{
    return 0;
}

int gs_mount_under_wsl(void)
{
    return 0;
}

int gs_mount_table_read(gs_mount_drive_t *out, int max)
{
    if (out == NULL || max <= 0)
        return GS_ERR_ARG;
    memset(out, 0, sizeof *out * (size_t)max);
    return 0;
}

int gs_mount_is_mounted(const char *point)
{
    (void)point;
    return 0;
}

int gs_mount_read_drive_list(const char *text, char *out, size_t cap)
{
    (void)text;
    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    return GS_ERR;
}

int gs_mount_windows_letters(char *out, size_t cap)
{
    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    return GS_ERR;
}

int gs_mount_answers(const char *point)
{
    (void)point;
    return 0;
}

int gs_mount_dir_at(const char *point, int *exists_out)
{
    (void)point;
    if (exists_out != NULL)
        *exists_out = 0;
    return 0;
}

int gs_mount_dir_has_content(const char *point)
{
    (void)point;
    return 0;
}

int gs_mount_holders(const char *point, char *first, size_t cap)
{
    (void)point;
    if (first != NULL && cap > 0)
        first[0] = '\0';
    return 0;
}

gs_mount_outcome_t gs_mount_repair(char letter, const char *point,
                                   gs_mount_state_t state,
                                   char *why, size_t cap)
{
    (void)letter;
    (void)point;
    (void)state;
    if (why != NULL && cap > 0)
        why[0] = '\0';
    return GS_MOUNT_SKIPPED;
}

char gs_mount_wants_setup(void)
{
    return '\0';
}

gs_mount_road_t gs_mount_root_road(const char **helper_out)
{
    if (helper_out != NULL)
        *helper_out = NULL;
    return GS_MOUNT_ROAD_NONE;
}

int gs_mount_root_free(void)
{
    return 0;
}

int gs_mount_root_settled(void)
{
    return 1;
}

int gs_mount_line_is_nought(const char *text, size_t cap)
{
    (void)text;
    (void)cap;
    return 0;
}

int gs_mount_as_root(const char *program, const char *const *tail,
                     int tail_len, char *out, size_t cap, int *code)
{
    (void)program;
    (void)tail;
    (void)tail_len;
    if (out != NULL && cap > 0)
        out[0] = '\0';
    if (code != NULL)
        *code = -1;
    return GS_ERR;
}

int gs_mount_root_argv(gs_mount_road_t road, const char *helper,
                       const char *distro, const char *program,
                       const char *const *tail, int tail_len,
                       const char **argv, int cap)
{
    (void)road;
    (void)helper;
    (void)distro;
    (void)program;
    (void)tail;
    (void)tail_len;
    (void)argv;
    (void)cap;
    return GS_ERR;
}

int gs_mount_fstab_grants(char letter)
{
    (void)letter;
    return 0;
}

int gs_mount_fstab_grant(char letter, const char *password,
                         size_t password_len, char *why, size_t cap)
{
    (void)letter;
    (void)password;
    (void)password_len;
    if (why != NULL && cap > 0)
        why[0] = '\0';
    return GS_ERR;
}

int gs_mount_lock(void)
{
    return GS_OK;
}

void gs_mount_unlock(void)
{
}
