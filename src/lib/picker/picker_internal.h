/* picker_internal.h
 *
 * Shared between the picker and its test. Nothing outside
 * src/lib/picker/ may include this.
 */
#ifndef GS_LIB_PICKER_INTERNAL_H
#define GS_LIB_PICKER_INTERNAL_H

#include <stddef.h>

/* Builds the PowerShell that puts a file box on a Windows desktop.
 *
 * Split out so the text can be read and checked without a box appearing
 * on anybody's screen. The title is written into it twice, once as the
 * box's own title and once as the name the lifting looks the box up by,
 * so a title carrying a quote is refused before it reaches either.
 *
 * Returns GS_OK, or an error when the title is unusable or the script
 * will not fit.
 */
int gs_picker_script(const char *title, char *out, size_t cap);

#endif
