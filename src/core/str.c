#include "str.h"
#include "gravestone.h"

#include <stdio.h>
#include <string.h>

void gs_str_copy(char *dst, size_t cap, const char *src)
{
    size_t n;

    if (dst == NULL || cap == 0)
        return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n > cap - 1)
        n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void gs_str_bytes(long long bytes, char *out, size_t cap)
{
    static const char *unit[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    double value = (double)bytes;
    int step = 0;

    if (out == NULL || cap == 0)
        return;
    if (bytes < 0) {
        snprintf(out, cap, "unknown");
        return;
    }
    while (value >= 1024.0 && step < 5) {
        value /= 1024.0;
        step++;
    }
    if (step == 0)
        snprintf(out, cap, "%lld B", bytes);
    else
        snprintf(out, cap, "%.1f %s", value, unit[step]);
}
