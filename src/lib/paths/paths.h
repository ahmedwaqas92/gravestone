/* paths.h
 *
 * Where this program keeps its files. Every platform answers differently,
 * so the answer is wrapped here rather than spread through the code.
 */
#ifndef GS_LIB_PATHS_H
#define GS_LIB_PATHS_H

#include <stddef.h>

/* Fills out with the per user data directory, creating it when absent
 * with permissions that keep other accounts out.
 *
 *   Linux    $XDG_DATA_HOME/gravestone or ~/.local/share/gravestone
 *   macOS    ~/Library/Application Support/gravestone
 *   Windows  %LOCALAPPDATA%\gravestone
 */
int gs_paths_data_dir(char *out, size_t cap);

/* The database file inside that directory. */
int gs_paths_db_file(char *out, size_t cap);

/* Overrides the directory for the duration of a test run. Passing NULL
 * puts the real one back. */
void gs_paths_override(const char *dir);

/* Makes one directory, with the permissions that matter on the systems
 * that carry them. An existing directory counts as success. Wrapped
 * here because Windows takes no permission argument at all. */
int gs_paths_make_dir(const char *path);

#endif
