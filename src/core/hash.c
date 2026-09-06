#include "hash.h"
#include "gravestone.h"

#include <stdio.h>
#include <string.h>

#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

unsigned long long gs_hash_bytes(const void *data, size_t len)
{
    const unsigned char *p = data;
    unsigned long long h = FNV_OFFSET;
    size_t i;

    if (data == NULL)
        return h;
    for (i = 0; i < len; i++) {
        h ^= (unsigned long long)p[i];
        h *= FNV_PRIME;
    }
    return h;
}

unsigned long long gs_hash_text(const char *text)
{
    return text != NULL ? gs_hash_bytes(text, strlen(text)) : FNV_OFFSET;
}

int gs_hash_hex(unsigned long long value, char *out, size_t cap)
{
    if (out == NULL || cap < 17)
        return GS_ERR_ARG;
    snprintf(out, cap, "%016llx", value);
    return GS_OK;
}
