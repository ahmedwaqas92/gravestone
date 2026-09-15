/* session.c
 *
 * Writing side. Conversations, the prompts inside them, and the answers
 * collected against each prompt.
 *
 * The tables live in the same file the rest of the program keeps its
 * settings in, reached through src/lib/db/ rather than through any other
 * module, because a wrapper is the only thing allowed to speak to the
 * engine.
 */
#include "session.h"
#include "session_internal.h"
#include "gravestone.h"
#include "log.h"
#include "paths.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

gs_db_t *gs_session_db;

/* Every table is made if it is missing rather than through a numbered
 * migration, since the settings side of this file owns the number and two
 * owners of one number would fight over it. */
static const char *const schema[] = {
    "CREATE TABLE IF NOT EXISTS session ("
    "  id         INTEGER PRIMARY KEY,"
    "  started_at INTEGER NOT NULL,"
    "  title      TEXT NOT NULL DEFAULT ''"
    ");",

    "CREATE TABLE IF NOT EXISTS turn ("
    "  id           INTEGER PRIMARY KEY,"
    "  session_id   INTEGER NOT NULL,"
    "  ordinal      INTEGER NOT NULL,"
    "  asked_at     INTEGER NOT NULL,"
    "  shown_answer INTEGER NOT NULL DEFAULT 0,"
    "  prompt       TEXT NOT NULL,"
    "  note         TEXT NOT NULL DEFAULT ''"
    ");",

    "CREATE TABLE IF NOT EXISTS answer ("
    "  id         INTEGER PRIMARY KEY,"
    "  turn_id    INTEGER NOT NULL,"
    "  sampled_at INTEGER NOT NULL,"
    "  verdict    INTEGER NOT NULL DEFAULT 0,"
    "  score      INTEGER NOT NULL DEFAULT 0,"
    "  model      TEXT NOT NULL,"
    "  text       TEXT NOT NULL"
    ");",

    /* Reading a conversation walks its turns in order, and reading a turn
     * walks its answers, so both get an index. */
    "CREATE INDEX IF NOT EXISTS turn_by_session"
    "  ON turn (session_id, ordinal);",
    "CREATE INDEX IF NOT EXISTS answer_by_turn"
    "  ON answer (turn_id, id);"
};
#define SCHEMA_COUNT ((int)(sizeof schema / sizeof schema[0]))

long long gs_session_now(void)
{
    return (long long)time(NULL);
}

int gs_session_open(void)
{
    char path[512];
    int i;

    if (gs_session_db != NULL)
        return GS_OK;
    if (gs_paths_db_file(path, sizeof path) != GS_OK)
        return GS_ERR;

    gs_session_db = gs_db_open(path);
    if (gs_session_db == NULL)
        return GS_ERR;

    for (i = 0; i < SCHEMA_COUNT; i++)
        if (gs_db_exec(gs_session_db, schema[i]) != GS_OK) {
            gs_log_error("session: %s", gs_db_error(gs_session_db));
            gs_db_close(gs_session_db);
            gs_session_db = NULL;
            return GS_ERR;
        }

    /* A table made before the note column existed keeps its old shape,
     * since CREATE TABLE IF NOT EXISTS leaves a table that is already
     * there alone. Adding the column a second time fails, and that
     * failure is the answer that it is already present, so it is
     * swallowed rather than reported. */
    (void)gs_db_exec(gs_session_db,
                     "ALTER TABLE turn ADD COLUMN note TEXT NOT NULL"
                     " DEFAULT '';");
    (void)gs_db_exec(gs_session_db,
                     "ALTER TABLE answer ADD COLUMN working TEXT NOT NULL"
                     " DEFAULT '';");
    return GS_OK;
}

int gs_session_set_working(long long answer_id, const char *working)
{
    gs_db_stmt_t *stmt;

    if (gs_session_db == NULL || answer_id <= 0 || working == NULL)
        return GS_ERR_ARG;
    stmt = gs_db_prepare(gs_session_db,
                         "UPDATE answer SET working = ? WHERE id = ?;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_text(stmt, 1, working);
    gs_db_bind_int(stmt, 2, answer_id);
    if (gs_db_step(stmt) < 0) {
        gs_db_finalise(stmt);
        return GS_ERR;
    }
    gs_db_finalise(stmt);
    return GS_OK;
}

int gs_session_note(long long turn_id, const char *note)
{
    gs_db_stmt_t *stmt;

    if (gs_session_db == NULL || turn_id <= 0)
        return GS_ERR_ARG;
    if (note == NULL)
        note = "";

    stmt = gs_db_prepare(gs_session_db,
                         "UPDATE turn SET note = ? WHERE id = ?;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_text(stmt, 1, note);
    gs_db_bind_int(stmt, 2, turn_id);
    if (gs_db_step(stmt) != GS_OK) {
        gs_db_finalise(stmt);
        return GS_ERR;
    }
    gs_db_finalise(stmt);
    return GS_OK;
}

void gs_session_close(void)
{
    if (gs_session_db == NULL)
        return;
    gs_db_close(gs_session_db);
    gs_session_db = NULL;
}

/* A title a person can pick a conversation out of a list by. An empty one
 * is filled in from the prompt, cut at a word so it reads as a phrase. */
static void title_from_prompt(const char *prompt, char *out, size_t cap)
{
    size_t i;
    size_t last_space = 0;

    for (i = 0; i + 1 < cap && prompt[i] != '\0'; i++) {
        char c = prompt[i];

        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
        out[i] = c;
        if (c == ' ')
            last_space = i;
    }
    out[i] = '\0';
    /* Cut short, so the last word is dropped rather than halved. */
    if (prompt[i] != '\0' && last_space > 0)
        out[last_space] = '\0';
}

/* Runs an insert that ends in RETURNING id, and hands back the number of
 * the row it wrote. The number comes out of the same statement as the
 * insert. Asking the handle for its last inserted row afterwards was a
 * second call, and a run writing answers on its own thread could insert
 * in between, so the window was handed the number of the run's answer
 * and wrote against the wrong row. */
static int insert_returning(gs_db_stmt_t *stmt, long long *out_id)
{
    long long id = 0;
    int rc = gs_db_step(stmt);

    if (rc == 1) {
        id = gs_db_int(stmt, 0);
        /* The change is made on the first step, and stepping once more
         * lets the statement finish rather than being cut off. */
        (void)gs_db_step(stmt);
    }
    if (rc < 0 || id <= 0) {
        gs_log_error("session: %s", gs_db_error(gs_session_db));
        return GS_ERR;
    }
    if (out_id != NULL)
        *out_id = id;
    return GS_OK;
}

int gs_session_start(const char *title, long long *out_id)
{
    gs_db_stmt_t *stmt;
    char kept[GS_SESSION_TITLE];

    if (gs_session_db == NULL)
        return GS_ERR;
    kept[0] = '\0';
    if (title != NULL)
        title_from_prompt(title, kept, sizeof kept);

    stmt = gs_db_prepare(gs_session_db,
                         "INSERT INTO session (started_at, title)"
                         " VALUES (?, ?) RETURNING id;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, gs_session_now());
    gs_db_bind_text(stmt, 2, kept);
    if (insert_returning(stmt, out_id) != GS_OK) {
        gs_db_finalise(stmt);
        return GS_ERR;
    }
    gs_db_finalise(stmt);
    return GS_OK;
}

int gs_session_add_turn(long long session_id, const char *prompt,
                        long long *out_turn)
{
    gs_db_stmt_t *stmt;
    long long ordinal = 1;
    long long turn_id = 0;

    if (gs_session_db == NULL || prompt == NULL || prompt[0] == '\0')
        return GS_ERR_ARG;
    if (session_id <= 0)
        return GS_ERR_ARG;

    /* The next place in the order, counted from what is already there
     * rather than held in memory, so a second copy of the program cannot
     * write two turns into the same place. */
    stmt = gs_db_prepare(gs_session_db,
                         "SELECT COALESCE(MAX(ordinal), 0) + 1 FROM turn"
                         " WHERE session_id = ?;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, session_id);
    if (gs_db_step(stmt) == 1)
        ordinal = gs_db_int(stmt, 0);
    gs_db_finalise(stmt);

    stmt = gs_db_prepare(gs_session_db,
                         "INSERT INTO turn"
                         " (session_id, ordinal, asked_at, prompt)"
                         " VALUES (?, ?, ?, ?) RETURNING id;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, session_id);
    gs_db_bind_int(stmt, 2, ordinal);
    gs_db_bind_int(stmt, 3, gs_session_now());
    gs_db_bind_text(stmt, 4, prompt);
    if (insert_returning(stmt, &turn_id) != GS_OK) {
        gs_db_finalise(stmt);
        return GS_ERR;
    }
    gs_db_finalise(stmt);

    /* A conversation with no title takes the first prompt as its name. */
    if (ordinal == 1) {
        char kept[GS_SESSION_TITLE];

        title_from_prompt(prompt, kept, sizeof kept);
        stmt = gs_db_prepare(gs_session_db,
                             "UPDATE session SET title = ?"
                             " WHERE id = ? AND title = '';");
        if (stmt != NULL) {
            gs_db_bind_text(stmt, 1, kept);
            gs_db_bind_int(stmt, 2, session_id);
            gs_db_step(stmt);
            gs_db_finalise(stmt);
        }
    }

    if (out_turn != NULL)
        *out_turn = turn_id;
    return GS_OK;
}

int gs_session_add_answer(long long turn_id, const char *model,
                          const char *text, long long *out_answer)
{
    gs_db_stmt_t *stmt;
    int empty;

    if (gs_session_db == NULL || model == NULL || model[0] == '\0')
        return GS_ERR_ARG;
    if (turn_id <= 0 || text == NULL)
        return GS_ERR_ARG;

    /* An empty reply is a failure of its own rather than an absence, so
     * it is written down and marked, which is what lets the harness ask
     * again with a larger context. */
    empty = text[0] == '\0';

    stmt = gs_db_prepare(gs_session_db,
                         "INSERT INTO answer"
                         " (turn_id, sampled_at, verdict, model, text)"
                         " VALUES (?, ?, ?, ?, ?) RETURNING id;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, turn_id);
    gs_db_bind_int(stmt, 2, gs_session_now());
    gs_db_bind_int(stmt, 3, empty ? GS_SESSION_EMPTY : GS_SESSION_PENDING);
    gs_db_bind_text(stmt, 4, model);
    gs_db_bind_text(stmt, 5, text);
    if (insert_returning(stmt, out_answer) != GS_OK) {
        gs_db_finalise(stmt);
        return GS_ERR;
    }
    gs_db_finalise(stmt);
    return GS_OK;
}

int gs_session_judge(long long answer_id, gs_session_verdict_t verdict,
                     int score)
{
    gs_db_stmt_t *stmt;

    if (gs_session_db == NULL || answer_id <= 0)
        return GS_ERR_ARG;
    if (verdict < GS_SESSION_PENDING || verdict > GS_SESSION_EMPTY)
        return GS_ERR_ARG;
    if (score < 0)
        return GS_ERR_ARG;

    stmt = gs_db_prepare(gs_session_db,
                         "UPDATE answer SET verdict = ?, score = ?"
                         " WHERE id = ?;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, verdict);
    gs_db_bind_int(stmt, 2, score);
    gs_db_bind_int(stmt, 3, answer_id);
    if (gs_db_step(stmt) != GS_OK) {
        gs_db_finalise(stmt);
        return GS_ERR;
    }
    gs_db_finalise(stmt);
    return gs_db_changes(gs_session_db) > 0 ? GS_OK : GS_ERR;
}

int gs_session_show(long long turn_id, long long answer_id)
{
    gs_db_stmt_t *stmt;
    long long owner = 0;

    if (gs_session_db == NULL || turn_id <= 0 || answer_id <= 0)
        return GS_ERR_ARG;

    /* A turn shows one of its own answers. Anything else would put words
     * from one question under another. */
    stmt = gs_db_prepare(gs_session_db,
                         "SELECT turn_id FROM answer WHERE id = ?;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, answer_id);
    if (gs_db_step(stmt) == 1)
        owner = gs_db_int(stmt, 0);
    gs_db_finalise(stmt);
    if (owner != turn_id)
        return GS_ERR_ARG;

    stmt = gs_db_prepare(gs_session_db,
                         "UPDATE turn SET shown_answer = ? WHERE id = ?;");
    if (stmt == NULL)
        return GS_ERR;
    gs_db_bind_int(stmt, 1, answer_id);
    gs_db_bind_int(stmt, 2, turn_id);
    if (gs_db_step(stmt) != GS_OK) {
        gs_db_finalise(stmt);
        return GS_ERR;
    }
    gs_db_finalise(stmt);
    return gs_db_changes(gs_session_db) > 0 ? GS_OK : GS_ERR;
}

int gs_session_forget(long long session_id)
{
    gs_db_stmt_t *stmt;

    if (gs_session_db == NULL || session_id <= 0)
        return GS_ERR_ARG;

    if (gs_db_begin(gs_session_db) != GS_OK)
        return GS_ERR;

    stmt = gs_db_prepare(gs_session_db,
                         "DELETE FROM answer WHERE turn_id IN"
                         " (SELECT id FROM turn WHERE session_id = ?);");
    if (stmt != NULL) {
        gs_db_bind_int(stmt, 1, session_id);
        gs_db_step(stmt);
        gs_db_finalise(stmt);
    }
    stmt = gs_db_prepare(gs_session_db,
                         "DELETE FROM turn WHERE session_id = ?;");
    if (stmt != NULL) {
        gs_db_bind_int(stmt, 1, session_id);
        gs_db_step(stmt);
        gs_db_finalise(stmt);
    }
    stmt = gs_db_prepare(gs_session_db, "DELETE FROM session WHERE id = ?;");
    if (stmt == NULL) {
        gs_db_rollback(gs_session_db);
        return GS_ERR;
    }
    gs_db_bind_int(stmt, 1, session_id);
    gs_db_step(stmt);
    gs_db_finalise(stmt);

    if (gs_db_commit(gs_session_db) != GS_OK) {
        gs_db_rollback(gs_session_db);
        return GS_ERR;
    }
    return GS_OK;
}

/* ---- what the command line shows ---- */

static int session_init(void)
{
    return gs_session_open();
}

static int session_run(int argc, char **argv)
{
    gs_session_t list[32];
    int n;
    int i;

    (void)argc;
    (void)argv;

    n = gs_session_list(list, (int)(sizeof list / sizeof list[0]));
    if (n == 0) {
        printf("no conversations yet\n");
        return GS_OK;
    }
    for (i = 0; i < n; i++)
        printf("%lld\t%d turn%s\t%s\n", list[i].id, list[i].turns,
               list[i].turns == 1 ? "" : "s", list[i].title);
    return GS_OK;
}

static void session_shutdown(void)
{
    gs_session_close();
}

const gs_module gs_session_module = {
    "session",
    "list the conversations this install has kept",
    session_init,
    session_run,
    session_shutdown
};
