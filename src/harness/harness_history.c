/* harness_history.c
 *
 * What was said before, sent again with every question.
 *
 * A model keeps nothing between one request and the next. The server
 * forgets the conversation the moment an answer is written, so a model
 * asked "what did I just say" with only the new question in front of it
 * has nothing to go on and says so. A conversation it can follow is one
 * sent again every time, the earlier questions and the answers shown for
 * them, in the order they were said.
 *
 * The room is finite, so the newest exchanges go first and the oldest
 * are left out once the room is full.
 */
#include "harness.h"
#include "harness_internal.h"
#include "gravestone.h"
#include "session.h"

#include <string.h>

/* Read once a run on the worker thread and pointed at by the turns handed
 * to the provider, so it stays put until the next run loads again. */
static gs_session_exchange_t kept[GS_HARNESS_HISTORY_TURNS];

/* Put where the answer is cut, so the model reads the gap as a gap. */
#define CUT_MARK "\n(the rest of this answer is left out)"

int gs_harness_history_load(long long session_id, long long turn_id,
                            const char *system, const char *prompt,
                            gs_harness_history_t *out)
{
    size_t fixed = 0;
    size_t room;
    size_t used = 0;
    int read;
    int count = 0;
    int i;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);

    if (system != NULL)
        fixed += strlen(system);
    if (prompt != NULL)
        fixed += strlen(prompt);
    room = fixed < GS_HARNESS_ROOM_BYTES ? GS_HARNESS_ROOM_BYTES - fixed : 0;

    read = gs_session_exchanges(session_id, turn_id, kept,
                                GS_HARNESS_HISTORY_TURNS);

    /* Newest first, and the walk stops at the first exchange that does
     * not fit, so what is sent has no hole in its middle. */
    for (i = 0; i < read; i++) {
        size_t asked = strlen(kept[i].prompt);
        size_t size = asked + strlen(kept[i].answer);

        if (used + size <= room) {
            used += size;
            count++;
            continue;
        }
        if (i == 0 && asked + sizeof CUT_MARK < room) {
            size_t end = room - asked - sizeof CUT_MARK;

            /* A letter written in more than one byte is not split, since
             * half of one is not text. */
            while (end > 0 &&
                   ((unsigned char)kept[0].answer[end] & 0xC0) == 0x80)
                end--;
            memcpy(kept[0].answer + end, CUT_MARK, sizeof CUT_MARK);
            out->cut = 1;
            count = 1;
        }
        break;
    }

    /* The model reads the conversation in the order it happened. */
    for (i = 0; i < count; i++) {
        out->turns[i].prompt = kept[count - 1 - i].prompt;
        out->turns[i].answer = kept[count - 1 - i].answer;
    }
    out->count = count;
    out->left_out = read - count;
    return count;
}
