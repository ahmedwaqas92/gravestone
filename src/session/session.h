/* session.h
 *
 * Where a conversation lives between one run of the program and the next.
 *
 * A session is one conversation. A turn is one prompt inside it, in the
 * order it was asked. An answer is what one model gave back for one turn,
 * and a turn keeps every answer it collected rather than only the one
 * shown, so a choice made by the checking can be looked at afterwards.
 *
 * Nothing here talks to a model. It records what was asked, what came
 * back, and which answer won.
 */
#ifndef GS_SESSION_H
#define GS_SESSION_H

#include "gravestone.h"

#include <stddef.h>

#define GS_SESSION_TITLE   120
#define GS_SESSION_MODEL    96
#define GS_SESSION_NOTE   8192   /* the record kept of one whole run */
#define GS_SESSION_MAX    1024   /* sessions one listing carries */
#define GS_SESSION_TURNS  4096   /* turns one conversation carries */

/* What happened to one answer once the tools had looked at it. Nothing
 * here asks a model whether an answer is correct. */
typedef enum {
    GS_SESSION_PENDING = 0,  /* collected, nothing has checked it yet */
    GS_SESSION_PASSED,       /* survived every check that was run */
    GS_SESSION_FAILED_CHECK, /* the compiler or the static check refused it */
    GS_SESSION_FAILED_TESTS, /* it built, and the tests said no */
    GS_SESSION_EMPTY         /* the model gave back nothing at all */
} gs_session_verdict_t;

typedef struct {
    long long id;
    long long started_at;    /* seconds since the epoch */
    long long last_at;       /* when the newest turn was added */
    int       turns;
    char      title[GS_SESSION_TITLE];
} gs_session_t;

typedef struct {
    long long id;
    long long session_id;
    int       ordinal;       /* first turn is 1 */
    long long asked_at;
    long long shown_answer;  /* the answer that won, 0 while none has */
    char      prompt[2048];
    char      note[GS_SESSION_NOTE];  /* how the answer was arrived at */
} gs_session_turn_t;

typedef struct {
    long long id;
    long long turn_id;
    long long sampled_at;
    int       verdict;       /* one of gs_session_verdict_t */
    int       score;         /* what the ranking counted, higher wins */
    char      model[GS_SESSION_MODEL];
    char      text[8192];
} gs_session_answer_t;

/* One prompt as the history panel reads it, without the record of the
 * run behind it, which can be many times the size and is read on its own
 * when somebody opens it. */
typedef struct {
    long long id;
    long long session_id;
    long long asked_at;
    long long shown_answer;  /* the answer that won, 0 while none has */
    char      prompt[2048];
} gs_session_line_t;

/* Opens the file the conversations live in and makes its tables when they
 * are missing. Safe to call twice. */
int  gs_session_open(void);
void gs_session_close(void);

/* Starts a conversation and hands back its number. The title is what a
 * person sees in a list of conversations, and an empty one is filled in
 * from the first prompt. */
int gs_session_start(const char *title, long long *out_id);

/* Adds one prompt to a conversation, at the end. Hands back the number of
 * the turn it made, which every answer is recorded against. */
int gs_session_add_turn(long long session_id, const char *prompt,
                        long long *out_turn);

/* Records what one model gave back for one turn. Called once per model,
 * so a turn asked of four models carries four answers. */
int gs_session_add_answer(long long turn_id, const char *model,
                          const char *text, long long *out_answer);

/* Writes down what the checking decided about one answer. The score is
 * what the ranking counted, and the answer with the highest score is the
 * one a turn shows. */
int gs_session_judge(long long answer_id, gs_session_verdict_t verdict,
                     int score);

/* Marks which answer a turn shows. Refuses an answer belonging to another
 * turn, since a turn shows one of its own or none. */
int gs_session_show(long long turn_id, long long answer_id);

/* Picks the answer a turn should show, out of the answers it already
 * holds, by the order written down in the README. Highest score first,
 * then the shortest, then the earliest, so the same answers always give
 * the same winner. Only answers that passed are considered.
 *
 * Returns GS_OK and marks the turn, or an error when nothing passed. */
int gs_session_pick(long long turn_id, long long *out_answer);

/* Reads conversations back, newest first. Returns how many were written,
 * which may be fewer than asked for. */
int gs_session_list(gs_session_t *out, int max);

/* Writes down how a turn was answered, which is the whole run, model by
 * model, so the note on a line still opens after the program is shut and
 * started again. Passing nothing clears it. */
int gs_session_note(long long turn_id, const char *note);

/* The working out a model wrote before its answer, kept against that
 * answer. It is read on its own rather than carried in the answer
 * struct, since it can be as long as the answer again and the struct is
 * held in arrays. */
/* Every prompt ever asked, from every conversation, in the order they
 * were asked. A person reads one history, and a question asked from the
 * command line or after the window started a fresh conversation belongs
 * in it as much as any other.
 *
 * skip leaves out that many of the newest, and max bounds how many are
 * read after that, so the history can be read one page at a time from
 * the newest backwards. The page comes back oldest first. Returns how
 * many were written. */
int gs_session_timeline(gs_session_line_t *out, int skip, int max);

/* How many prompts the file holds across every conversation. */
int gs_session_prompt_count(void);

/* Non zero when a conversation of that number is in the file. */
int gs_session_exists(long long session_id);

/* The record of the run behind one prompt. */
int gs_session_turn_note(long long turn_id, char *out, size_t cap);

/* How many answers a prompt collected. A model that gave nothing back
 * still leaves a row, with no words in it, so with_words set counts only
 * the rows that hold an actual answer. */
int gs_session_answer_count(long long turn_id, int with_words);

int gs_session_set_working(long long answer_id, const char *working);
int gs_session_working(long long answer_id, char *out, size_t cap);

/* Reads one conversation back in the order it was asked, so the panel can
 * show it again exactly as it was. */
int gs_session_turns(long long session_id, gs_session_turn_t *out, int max);

/* Reads every answer collected for one turn, in the order they arrived. */
int gs_session_answers(long long turn_id, gs_session_answer_t *out, int max);

/* Reads the one answer a turn shows. Returns an error while the turn has
 * not chosen one. */
int gs_session_shown(long long turn_id, gs_session_answer_t *out);

/* One earlier exchange of a conversation, as a model is shown it again
 * ahead of a new question: what was asked, and the answer shown for it. */
typedef struct {
    char prompt[4096];
    char answer[8192];
} gs_session_exchange_t;

/* Reads the exchanges of one conversation asked before a turn, newest
 * first, so a caller short of room keeps the most recent ones. A turn
 * that never showed an answer with words in it is left out, since a
 * question with no reply after it tells a model nothing about what was
 * said. Returns how many were written. */
int gs_session_exchanges(long long session_id, long long before_turn,
                         gs_session_exchange_t *out, int max);

/* Forgets a whole conversation, along with its turns and their answers. */
int gs_session_forget(long long session_id);


/* Prints the conversations on record, newest first. */
extern const gs_module gs_session_module;

#endif
