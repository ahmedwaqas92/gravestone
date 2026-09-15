/* session_read.c
 *
 * Reading side. Everything here answers what a conversation held, so the
 * panel can put it back on screen exactly as it was left.
 */
#include "session.h"
#include "session_internal.h"
#include "gravestone.h"
#include "str.h"

#include <string.h>

int gs_session_list(gs_session_t *out, int max)
{
    gs_db_stmt_t *stmt;
    int n = 0;

    if (gs_session_db == NULL || out == NULL || max <= 0)
        return 0;

    /* Newest first, since the conversation a person wants back is nearly
     * always the one they were last in. */
    stmt = gs_db_prepare(gs_session_db,
        "SELECT s.id, s.started_at, s.title,"
        "       COUNT(t.id),"
        "       COALESCE(MAX(t.asked_at), s.started_at)"
        "  FROM session s LEFT JOIN turn t ON t.session_id = s.id"
        " GROUP BY s.id"
        " ORDER BY COALESCE(MAX(t.asked_at), s.started_at) DESC, s.id DESC"
        " LIMIT ?;");
    if (stmt == NULL)
        return 0;
    gs_db_bind_int(stmt, 1, max);

    while (n < max && gs_db_step(stmt) == 1) {
        memset(&out[n], 0, sizeof out[n]);
        out[n].id = gs_db_int(stmt, 0);
        out[n].started_at = gs_db_int(stmt, 1);
        gs_str_copy(out[n].title, sizeof out[n].title, gs_db_text(stmt, 2));
        out[n].turns = (int)gs_db_int(stmt, 3);
        out[n].last_at = gs_db_int(stmt, 4);
        n++;
    }
    gs_db_finalise(stmt);
    return n;
}

int gs_session_turns(long long session_id, gs_session_turn_t *out, int max)
{
    gs_db_stmt_t *stmt;
    int n = 0;

    if (gs_session_db == NULL || out == NULL || max <= 0 || session_id <= 0)
        return 0;

    stmt = gs_db_prepare(gs_session_db,
        "SELECT id, session_id, ordinal, asked_at, shown_answer, prompt,"
        "       note"
        "  FROM turn WHERE session_id = ? ORDER BY ordinal ASC LIMIT ?;");
    if (stmt == NULL)
        return 0;
    gs_db_bind_int(stmt, 1, session_id);
    gs_db_bind_int(stmt, 2, max);

    while (n < max && gs_db_step(stmt) == 1) {
        memset(&out[n], 0, sizeof out[n]);
        out[n].id = gs_db_int(stmt, 0);
        out[n].session_id = gs_db_int(stmt, 1);
        out[n].ordinal = (int)gs_db_int(stmt, 2);
        out[n].asked_at = gs_db_int(stmt, 3);
        out[n].shown_answer = gs_db_int(stmt, 4);
        gs_str_copy(out[n].prompt, sizeof out[n].prompt, gs_db_text(stmt, 5));
        gs_str_copy(out[n].note, sizeof out[n].note, gs_db_text(stmt, 6));
        n++;
    }
    gs_db_finalise(stmt);
    return n;
}

/* Fills one answer from a row of the statement, which both readers below
 * select in the same order. */
static void read_answer(gs_db_stmt_t *stmt, gs_session_answer_t *out)
{
    memset(out, 0, sizeof *out);
    out->id = gs_db_int(stmt, 0);
    out->turn_id = gs_db_int(stmt, 1);
    out->sampled_at = gs_db_int(stmt, 2);
    out->verdict = (int)gs_db_int(stmt, 3);
    out->score = (int)gs_db_int(stmt, 4);
    gs_str_copy(out->model, sizeof out->model, gs_db_text(stmt, 5));
    gs_str_copy(out->text, sizeof out->text, gs_db_text(stmt, 6));
}

#define ANSWER_COLUMNS \
    "id, turn_id, sampled_at, verdict, score, model, text"

int gs_session_answers(long long turn_id, gs_session_answer_t *out, int max)
{
    gs_db_stmt_t *stmt;
    int n = 0;

    if (gs_session_db == NULL || out == NULL || max <= 0 || turn_id <= 0)
        return 0;

    stmt = gs_db_prepare(gs_session_db,
        "SELECT " ANSWER_COLUMNS
        "  FROM answer WHERE turn_id = ? ORDER BY id ASC LIMIT ?;");
    if (stmt == NULL)
        return 0;
    gs_db_bind_int(stmt, 1, turn_id);
    gs_db_bind_int(stmt, 2, max);

    while (n < max && gs_db_step(stmt) == 1)
        read_answer(stmt, &out[n++]);
    gs_db_finalise(stmt);
    return n;
}

int gs_session_shown(long long turn_id, gs_session_answer_t *out)
{
    gs_db_stmt_t *stmt;
    int found = 0;

    if (gs_session_db == NULL || out == NULL || turn_id <= 0)
        return GS_ERR_ARG;
    memset(out, 0, sizeof *out);

    stmt = gs_db_prepare(gs_session_db,
        "SELECT " ANSWER_COLUMNS
        "  FROM answer WHERE id = (SELECT shown_answer FROM turn"
        "                          WHERE id = ?);");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, turn_id);
    if (gs_db_step(stmt) == 1) {
        read_answer(stmt, out);
        found = 1;
    }
    gs_db_finalise(stmt);
    return found ? GS_OK : GS_ERR;
}

int gs_session_exchanges(long long session_id, long long before_turn,
                         gs_session_exchange_t *out, int max)
{
    gs_db_stmt_t *stmt;
    int n = 0;

    if (gs_session_db == NULL || out == NULL || max <= 0 ||
        session_id <= 0 || before_turn <= 0)
        return 0;

    stmt = gs_db_prepare(gs_session_db,
        "SELECT t.prompt, a.text"
        "  FROM turn t JOIN answer a ON a.id = t.shown_answer"
        " WHERE t.session_id = ? AND t.id < ? AND a.text <> ''"
        " ORDER BY t.id DESC LIMIT ?;");
    if (stmt == NULL)
        return 0;
    gs_db_bind_int(stmt, 1, session_id);
    gs_db_bind_int(stmt, 2, before_turn);
    gs_db_bind_int(stmt, 3, max);

    while (n < max && gs_db_step(stmt) == 1) {
        gs_str_copy(out[n].prompt, sizeof out[n].prompt, gs_db_text(stmt, 0));
        gs_str_copy(out[n].answer, sizeof out[n].answer, gs_db_text(stmt, 1));
        n++;
    }
    gs_db_finalise(stmt);
    return n;
}

int gs_session_pick(long long turn_id, long long *out_answer)
{
    gs_db_stmt_t *stmt;
    long long winner = 0;

    if (gs_session_db == NULL || turn_id <= 0)
        return GS_ERR_ARG;

    /* The order written down in the README. Highest score first, then the
     * shortest answer, then the earliest sample, so the same answers give
     * the same winner on every run. Only what passed is considered, since
     * nothing that failed a compiler or a test may be shown. */
    stmt = gs_db_prepare(gs_session_db,
        "SELECT id FROM answer"
        " WHERE turn_id = ? AND verdict = ?"
        " ORDER BY score DESC, LENGTH(text) ASC, sampled_at ASC, id ASC"
        " LIMIT 1;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, turn_id);
    gs_db_bind_int(stmt, 2, GS_SESSION_PASSED);
    if (gs_db_step(stmt) == 1)
        winner = gs_db_int(stmt, 0);
    gs_db_finalise(stmt);

    if (winner == 0)
        return GS_ERR;          /* nothing survived, so nothing is shown */
    if (gs_session_show(turn_id, winner) != GS_OK)
        return GS_ERR;
    if (out_answer != NULL)
        *out_answer = winner;
    return GS_OK;
}

int gs_session_working(long long answer_id, char *out, size_t cap)
{
    gs_db_stmt_t *stmt;
    int found = 0;

    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (gs_session_db == NULL || answer_id <= 0)
        return GS_ERR_ARG;

    stmt = gs_db_prepare(gs_session_db,
                         "SELECT working FROM answer WHERE id = ?;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, answer_id);
    if (gs_db_step(stmt) == 1) {
        gs_str_copy(out, cap, gs_db_text(stmt, 0));
        found = 1;
    }
    gs_db_finalise(stmt);
    return found ? GS_OK : GS_ERR;
}

int gs_session_timeline(gs_session_line_t *out, int skip, int max)
{
    gs_db_stmt_t *stmt;
    int n = 0;

    if (gs_session_db == NULL || out == NULL || max <= 0 || skip < 0)
        return 0;

    /* The newest are counted back from, so a page is a window onto the
     * end of the history, and the inner order is turned round so the page
     * reads top to bottom the way it was asked. */
    stmt = gs_db_prepare(gs_session_db,
        "SELECT id, session_id, asked_at, shown_answer, prompt FROM"
        " (SELECT id, session_id, asked_at, shown_answer, prompt FROM turn"
        "   ORDER BY asked_at DESC, id DESC LIMIT ? OFFSET ?)"
        " ORDER BY asked_at ASC, id ASC;");
    if (stmt == NULL)
        return 0;
    gs_db_bind_int(stmt, 1, max);
    gs_db_bind_int(stmt, 2, skip);

    while (n < max && gs_db_step(stmt) == 1) {
        gs_session_line_t *line = &out[n++];

        memset(line, 0, sizeof *line);
        line->id = gs_db_int(stmt, 0);
        line->session_id = gs_db_int(stmt, 1);
        line->asked_at = gs_db_int(stmt, 2);
        line->shown_answer = gs_db_int(stmt, 3);
        gs_str_copy(line->prompt, sizeof line->prompt, gs_db_text(stmt, 4));
    }
    gs_db_finalise(stmt);
    return n;
}

int gs_session_prompt_count(void)
{
    gs_db_stmt_t *stmt;
    int n = 0;

    if (gs_session_db == NULL)
        return 0;
    stmt = gs_db_prepare(gs_session_db, "SELECT count(*) FROM turn;");
    if (stmt == NULL)
        return 0;
    if (gs_db_step(stmt) == 1)
        n = (int)gs_db_int(stmt, 0);
    gs_db_finalise(stmt);
    return n;
}

int gs_session_turn_note(long long turn_id, char *out, size_t cap)
{
    gs_db_stmt_t *stmt;
    int found = 0;

    if (out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';
    if (gs_session_db == NULL || turn_id <= 0)
        return GS_ERR_ARG;

    stmt = gs_db_prepare(gs_session_db, "SELECT note FROM turn WHERE id = ?;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, turn_id);
    if (gs_db_step(stmt) == 1) {
        gs_str_copy(out, cap, gs_db_text(stmt, 0));
        found = 1;
    }
    gs_db_finalise(stmt);
    return found ? GS_OK : GS_ERR;
}

int gs_session_answer_count(long long turn_id, int with_words)
{
    gs_db_stmt_t *stmt;
    int n = 0;

    if (gs_session_db == NULL || turn_id <= 0)
        return 0;
    stmt = gs_db_prepare(gs_session_db,
                         with_words
                             ? "SELECT count(*) FROM answer"
                               " WHERE turn_id = ? AND text <> '';"
                             : "SELECT count(*) FROM answer WHERE turn_id = ?;");
    if (stmt == NULL)
        return 0;
    gs_db_bind_int(stmt, 1, turn_id);
    if (gs_db_step(stmt) == 1)
        n = (int)gs_db_int(stmt, 0);
    gs_db_finalise(stmt);
    return n;
}

int gs_session_exists(long long session_id)
{
    gs_db_stmt_t *stmt;
    int found = 0;

    if (gs_session_db == NULL || session_id <= 0)
        return 0;
    stmt = gs_db_prepare(gs_session_db, "SELECT 1 FROM session WHERE id = ?;");
    if (stmt == NULL)
        return 0;
    gs_db_bind_int(stmt, 1, session_id);
    found = gs_db_step(stmt) == 1;
    gs_db_finalise(stmt);
    return found;
}
