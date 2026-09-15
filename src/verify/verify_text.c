/* verify_text.c
 *
 * Taking an answer apart. Sentences, the quotations inside them, and the
 * markers naming where each one came from.
 */
#include "verify.h"
#include "verify_internal.h"
#include "gravestone.h"

#include <ctype.h>
#include <string.h>

/* A sentence ends at a full stop, a question mark or an exclamation mark
 * that is followed by a space or by nothing, which is what a person
 * reading would call the end of one. A stop inside a number or inside a
 * name is followed by a letter, so it ends nothing. */
static int ends_sentence(const char *text, size_t at)
{
    char c = text[at];
    char next;

    if (c != '.' && c != '?' && c != '!')
        return 0;
    next = text[at + 1];
    return next == '\0' || next == ' ' || next == '\n' || next == '\t';
}

int gs_verify_sentence(const char *text, int index, char *out, size_t cap)
{
    size_t at = 0;
    int found = 0;

    if (text == NULL || out == NULL || cap == 0 || index < 0)
        return 0;
    out[0] = '\0';

    while (text[at] != '\0') {
        size_t start;
        size_t end;

        while (text[at] == ' ' || text[at] == '\n' || text[at] == '\t')
            at++;
        if (text[at] == '\0')
            return 0;

        start = at;
        while (text[at] != '\0' && !ends_sentence(text, at))
            at++;
        end = text[at] != '\0' ? at + 1 : at;   /* the stop belongs to it */

        if (found == index) {
            size_t len = end - start;

            if (len > cap - 1)
                len = cap - 1;
            memcpy(out, text + start, len);
            out[len] = '\0';
            return 1;
        }
        found++;
        at = end;
    }
    return 0;
}

int gs_verify_quotation(const char *sentence, char *out, size_t cap)
{
    const char *open;
    const char *close;
    size_t len;

    if (sentence == NULL || out == NULL || cap == 0)
        return 0;
    out[0] = '\0';

    open = strchr(sentence, '"');
    if (open == NULL)
        return 0;
    close = strchr(open + 1, '"');
    if (close == NULL)
        return 0;

    len = (size_t)(close - open - 1);
    if (len == 0)
        return 0;                      /* an empty quotation quotes nothing */
    if (len > cap - 1)
        len = cap - 1;
    memcpy(out, open + 1, len);
    out[len] = '\0';
    return 1;
}

int gs_verify_marker(const char *sentence, char *out, size_t cap)
{
    const char *open = NULL;
    const char *close = NULL;
    const char *at;
    size_t len;

    if (sentence == NULL || out == NULL || cap == 0)
        return 0;
    out[0] = '\0';

    /* The last pair, since a marker follows the words it belongs to. */
    for (at = sentence; *at != '\0'; at++)
        if (*at == '[') {
            const char *shut = strchr(at + 1, ']');

            if (shut != NULL) {
                open = at;
                close = shut;
            }
        }
    if (open == NULL || close == NULL)
        return 0;

    len = (size_t)(close - open - 1);
    if (len == 0)
        return 0;                      /* an empty marker names nothing */
    if (len > cap - 1)
        len = cap - 1;
    memcpy(out, open + 1, len);
    out[len] = '\0';
    return 1;
}
