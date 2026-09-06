#ifndef GS_CORE_STR_H
#define GS_CORE_STR_H

#include <stddef.h>

/* Copies into a fixed field, cutting the text short when it will not fit
 * and always leaving a terminator. The cut is deliberate, since system
 * fields are frequently larger than the places we keep them. */
void gs_str_copy(char *dst, size_t cap, const char *src);

/* Renders a byte count the way a person reads it, such as "7.6 GB". */
void gs_str_bytes(long long bytes, char *out, size_t cap);

#endif
