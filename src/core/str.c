#include "str.h"
#include "gravestone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int gs_str_windows_command(const char *text, char *out, size_t cap)
{
    size_t len;
    size_t wide;
    size_t need;
    size_t i;
    size_t at = 0;

    if (text == NULL || out == NULL || cap == 0)
        return -1;

    len = strlen(text);
    wide = len * 2;                    /* one byte becomes two */
    need = (wide + 2) / 3 * 4;         /* three bytes become four */
    if (need + 1 > cap)
        return -1;

    for (i = 0; i < wide; i += 3) {
        unsigned int block = 0;
        size_t have = wide - i < 3 ? wide - i : 3;
        size_t j;

        for (j = 0; j < 3; j++) {
            /* Every second byte of a widened plain character is zero. */
            unsigned char b = 0;

            if (j < have)
                b = (i + j) % 2 == 0 ? (unsigned char)text[(i + j) / 2] : 0u;
            block = (block << 8) | b;
        }

        out[at++] = alphabet[(block >> 18) & 0x3f];
        out[at++] = alphabet[(block >> 12) & 0x3f];
        out[at++] = have > 1 ? alphabet[(block >> 6) & 0x3f] : '=';
        out[at++] = have > 2 ? alphabet[block & 0x3f] : '=';
    }
    out[at] = '\0';
    return (int)at;
}

/* Letters, digits, dot, dash and underscore are every character a real
 * distribution name holds. */
static int name_is_plain(const char *name, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

int gs_str_wsl_distro(char *out, size_t cap)
{
    const char *name = getenv("WSL_DISTRO_NAME");
    size_t len;

    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (name == NULL)
        return GS_ERR;

    len = strlen(name);
    if (len == 0 || len > 64 || len + 1 > cap)
        return GS_ERR_ARG;
    if (!name_is_plain(name, len))
        return GS_ERR_ARG;

    memcpy(out, name, len + 1);
    return GS_OK;
}

void gs_str_clock(long long seconds, char *out, size_t cap)
{
    time_t when;
    struct tm parts;
    time_t now;
    struct tm today;

    if (out == NULL || cap == 0)
        return;
    out[0] = '\0';
    if (seconds <= 0 || cap < 6)
        return;

    when = (time_t)seconds;
    now = time(NULL);
#ifdef _WIN32
    if (localtime_s(&parts, &when) != 0)
        return;
    if (localtime_s(&today, &now) != 0)
        return;
#else
    if (localtime_r(&when, &parts) == NULL)
        return;
    if (localtime_r(&now, &today) == NULL)
        return;
#endif

    /* Something from today needs no date, since the day is the one the
     * reader is living through. */
    if (parts.tm_year == today.tm_year && parts.tm_yday == today.tm_yday) {
        snprintf(out, cap, "%02d:%02d", parts.tm_hour, parts.tm_min);
        return;
    }

    {
        static const char *const month[12] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        int m = parts.tm_mon;

        if (m < 0 || m > 11)
            m = 0;
        snprintf(out, cap, "%d %s %02d:%02d", parts.tm_mday, month[m],
                 parts.tm_hour, parts.tm_min);
    }
}

/* Reads one UTF-8 character starting at text. Returns how many bytes it
 * took, or 0 for a byte that does not start a well formed character. */
static size_t utf8_one(const unsigned char *text, unsigned long *code)
{
    unsigned long c;
    size_t need;
    size_t i;

    if (text[0] < 0x80) {
        *code = text[0];
        return 1;
    }
    if ((text[0] & 0xe0u) == 0xc0u) {
        c = text[0] & 0x1fu;
        need = 2;
    } else if ((text[0] & 0xf0u) == 0xe0u) {
        c = text[0] & 0x0fu;
        need = 3;
    } else if ((text[0] & 0xf8u) == 0xf0u) {
        c = text[0] & 0x07u;
        need = 4;
    } else {
        return 0;
    }
    for (i = 1; i < need; i++) {
        if ((text[i] & 0xc0u) != 0x80u)
            return 0;
        c = (c << 6) | (text[i] & 0x3fu);
    }
    *code = c;
    return need;
}

/* The plain spelling of a character, or NULL when it has none. */
static const char *plain_spelling(unsigned long code)
{
    switch (code) {
    case 0x00a0: return " ";          /* no-break space */
    case 0x00d7: return "x";          /* multiplication sign */
    case 0x2010: case 0x2011: case 0x2012:
        return "-";                   /* hyphens */
    case 0x2013: case 0x2014: case 0x2015: case 0x2212:
        return "-";                   /* en dash, em dash, bar, minus */
    case 0x2018: case 0x2019: case 0x201a: case 0x2032:
        return "'";
    case 0x201c: case 0x201d: case 0x201e: case 0x2033:
        return "\"";
    case 0x2026: return "...";
    case 0x2022: case 0x25cf: case 0x25e6: case 0x2043:
        return "-";                   /* bullets */
    case 0x2190: return "<-";
    case 0x2192: return "->";
    case 0x21d2: return "=>";
    case 0x2248: return "~";
    case 0x2260: return "!=";
    case 0x2264: return "<=";
    case 0x2265: return ">=";
    default:     return NULL;
    }
}

size_t gs_str_to_ascii(const char *text, char *out, size_t cap)
{
    const unsigned char *at = (const unsigned char *)text;
    size_t filled = 0;
    int dropped = 0;

    if (out == NULL || cap == 0)
        return 0;
    out[0] = '\0';
    if (text == NULL)
        return 0;

    while (*at != '\0' && filled + 1 < cap) {
        unsigned long code = 0;
        size_t took = utf8_one(at, &code);
        const char *plain;

        if (took == 0) {
            at++;
            continue;
        }

        if (took == 1) {
            char c = (char)code;

            /* A space beside something left out would otherwise stand
             * doubled, since the gap the emoji sat in is still there. */
            if (c == ' ' && dropped && filled > 0 && out[filled - 1] == ' ') {
                at++;
                continue;
            }
            out[filled++] = c;
            dropped = 0;
            at++;
            continue;
        }

        plain = plain_spelling(code);
        if (plain != NULL) {
            /* A dash written tight against both words reads as a hyphen
             * once it is plain, so it is given room either side. */
            int spaced = code == 0x2013 || code == 0x2014 || code == 0x2015;
            size_t len = strlen(plain);

            if (spaced && filled > 0 && out[filled - 1] != ' ' &&
                filled + 1 < cap)
                out[filled++] = ' ';
            if (filled + len >= cap)
                break;
            memcpy(out + filled, plain, len);
            filled += len;
            at += took;
            if (spaced && *at != '\0' && *at != ' ' && filled + 1 < cap)
                out[filled++] = ' ';
            dropped = 0;
            continue;
        }

        /* Nothing plain stands for it. It is left out, and a space goes
         * in only where leaving it out would join two words. */
        at += took;
        if (filled > 0 && out[filled - 1] != ' ' && out[filled - 1] != '\n' &&
            *at != '\0' && *at != ' ' && *at != '\n' && *at < 0x80 &&
            filled + 1 < cap)
            out[filled++] = ' ';
        dropped = 1;
    }
    out[filled] = '\0';
    return filled;
}
