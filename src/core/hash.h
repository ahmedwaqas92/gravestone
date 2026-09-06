#ifndef GS_CORE_HASH_H
#define GS_CORE_HASH_H

#include <stddef.h>

/* FNV-1a, sixty four bits. Enough to notice that a machine changed, and
 * no use at all against somebody trying to forge a match. */
unsigned long long gs_hash_bytes(const void *data, size_t len);
unsigned long long gs_hash_text(const char *text);

/* Writes the hash as sixteen hexadecimal characters plus a terminator, so
 * cap has to be at least seventeen. */
int gs_hash_hex(unsigned long long value, char *out, size_t cap);

#endif
