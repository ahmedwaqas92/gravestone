/* db_test.c
 *
 * Runs against a scratch file in the system temporary directory, which is
 * removed afterwards. Nothing here touches the real data directory.
 */
#include "db.h"
#include "gravestone.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static int checks;

static const char *PATH = "/tmp/gravestone-db-test.sqlite";

static void check(int condition, const char *what)
{
    checks++;
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

static void scrub(void)
{
    unlink(PATH);
    unlink("/tmp/gravestone-db-test.sqlite-wal");
    unlink("/tmp/gravestone-db-test.sqlite-shm");
}

static void test_bad_arguments(void)
{
    printf("bad arguments\n");
    check(gs_db_open(NULL) == NULL, "null path refused");
    check(gs_db_open("") == NULL, "empty path refused");
    check(gs_db_exec(NULL, "SELECT 1;") == GS_ERR_ARG, "exec on null refused");
    check(gs_db_prepare(NULL, "SELECT 1;") == NULL, "prepare on null refused");
    check(gs_db_step(NULL) == GS_ERR_ARG, "step on null refused");
    check(gs_db_bind_int(NULL, 1, 5) == GS_ERR_ARG, "bind on null refused");
    check(gs_db_text(NULL, 0) == NULL, "text from null is NULL");
    check(gs_db_int(NULL, 0) == 0, "int from null is 0");
    check(gs_db_is_null(NULL, 0) == 1, "null check on null says null");
    check(strcmp(gs_db_error(NULL), "no database") == 0,
          "error on null reads sensibly");
    gs_db_close(NULL);
    gs_db_finalise(NULL);
    check(1, "closing and finalising NULL does not crash");
}

static void test_round_trip(void)
{
    gs_db_t *db;
    gs_db_stmt_t *stmt;
    int rows = 0;

    printf("storing and reading back\n");
    scrub();

    db = gs_db_open(PATH);
    check(db != NULL, "database opened");
    if (db == NULL)
        return;

    printf("        engine version %s\n", gs_db_engine_version());

    {
        struct stat st;
        check(stat(PATH, &st) == 0 && (st.st_mode & 0077) == 0,
              "the file is readable by its owner alone");
        printf("        permissions %04o\n",
               (unsigned)(st.st_mode & 07777));
    }

    check(gs_db_exec(db,
        "CREATE TABLE t ("
        "  id    INTEGER PRIMARY KEY,"
        "  name  TEXT NOT NULL,"
        "  size  INTEGER NOT NULL,"
        "  blob  BLOB,"
        "  spare TEXT"
        ");") == GS_OK, "table created");

    stmt = gs_db_prepare(db,
        "INSERT INTO t (name, size, blob, spare) VALUES (?, ?, ?, ?);");
    check(stmt != NULL, "insert prepared");
    if (stmt == NULL)
        return;

    check(gs_db_bind_text(stmt, 1, "a card") == GS_OK, "text bound");
    check(gs_db_bind_int(stmt, 2, 8589934592LL) == GS_OK,
          "eight gigabytes bound without overflowing");
    check(gs_db_bind_blob(stmt, 3, "\x00\x01\x02\xff", 4) == GS_OK,
          "blob with a zero byte bound");
    check(gs_db_bind_null(stmt, 4) == GS_OK, "null bound");
    check(gs_db_step(stmt) == 0, "insert ran to completion");
    check(gs_db_last_insert_id(db) == 1, "row id came back as 1");
    gs_db_finalise(stmt);

    stmt = gs_db_prepare(db,
        "SELECT name, size, blob, spare FROM t WHERE id = ?;");
    check(stmt != NULL, "select prepared");
    if (stmt == NULL)
        return;
    gs_db_bind_int(stmt, 1, 1);

    while (gs_db_step(stmt) == 1) {
        int bytes = 0;
        const unsigned char *blob = gs_db_blob(stmt, 2, &bytes);

        rows++;
        check(strcmp(gs_db_text(stmt, 0), "a card") == 0, "text came back");
        check(gs_db_int(stmt, 1) == 8589934592LL,
              "eight gigabytes came back intact");
        check(bytes == 4, "blob length came back");
        check(blob != NULL && blob[0] == 0x00 && blob[3] == 0xff,
              "blob survived its zero byte");
        check(gs_db_is_null(stmt, 3), "the null column reads as null");
    }
    check(rows == 1, "exactly one row matched");
    gs_db_finalise(stmt);
    gs_db_close(db);
}

static void test_transaction(void)
{
    gs_db_t *db;
    gs_db_stmt_t *stmt;
    long long count = -1;

    printf("all or nothing writes\n");

    db = gs_db_open(PATH);
    if (db == NULL) {
        check(0, "database reopened");
        return;
    }
    check(1, "database reopened, so the file survived closing");

    check(gs_db_begin(db) == GS_OK, "transaction started");
    check(gs_db_exec(db,
        "INSERT INTO t (name, size) VALUES ('doomed', 1);") == GS_OK,
        "row written inside the transaction");
    check(gs_db_rollback(db) == GS_OK, "transaction rolled back");

    stmt = gs_db_prepare(db, "SELECT COUNT(*) FROM t;");
    if (stmt != NULL && gs_db_step(stmt) == 1)
        count = gs_db_int(stmt, 0);
    gs_db_finalise(stmt);
    check(count == 1, "the rolled back row left no trace");

    check(gs_db_begin(db) == GS_OK, "second transaction started");
    gs_db_exec(db, "INSERT INTO t (name, size) VALUES ('kept', 2);");
    check(gs_db_commit(db) == GS_OK, "transaction committed");

    stmt = gs_db_prepare(db, "SELECT COUNT(*) FROM t;");
    count = -1;
    if (stmt != NULL && gs_db_step(stmt) == 1)
        count = gs_db_int(stmt, 0);
    gs_db_finalise(stmt);
    check(count == 2, "the committed row stayed");

    gs_db_close(db);
}

static void test_failures_are_reported(void)
{
    gs_db_t *db;

    printf("failures reach the caller\n");

    db = gs_db_open(PATH);
    if (db == NULL) {
        check(0, "database opened");
        return;
    }

    check(gs_db_exec(db, "SELECT * FROM does_not_exist;") == GS_ERR,
          "a query against a missing table fails");
    check(strlen(gs_db_error(db)) > 0, "the reason is available");
    printf("        reported: %s\n", gs_db_error(db));

    check(gs_db_prepare(db, "THIS IS NOT SQL") == NULL,
          "nonsense refuses to prepare");

    /* Foreign keys are switched on at open, which SQLite leaves off. */
    gs_db_exec(db, "CREATE TABLE parent (id INTEGER PRIMARY KEY);");
    gs_db_exec(db, "CREATE TABLE child (id INTEGER PRIMARY KEY,"
                   " parent_id INTEGER REFERENCES parent(id));");
    check(gs_db_exec(db,
        "INSERT INTO child (parent_id) VALUES (999);") == GS_ERR,
        "a row pointing at a missing parent is refused");

    gs_db_close(db);
}

static void test_reuse(void)
{
    gs_db_t *db;
    gs_db_stmt_t *stmt;
    int i;
    long long count = 0;

    printf("reusing one statement\n");
    scrub();

    db = gs_db_open(PATH);
    if (db == NULL) {
        check(0, "database opened");
        return;
    }
    gs_db_exec(db, "CREATE TABLE n (v INTEGER);");

    stmt = gs_db_prepare(db, "INSERT INTO n (v) VALUES (?);");
    check(stmt != NULL, "statement prepared once");
    if (stmt == NULL) {
        gs_db_close(db);
        return;
    }

    gs_db_begin(db);
    for (i = 0; i < 1000; i++) {
        gs_db_bind_int(stmt, 1, i);
        if (gs_db_step(stmt) != 0)
            break;
        gs_db_reset(stmt);
    }
    gs_db_commit(db);
    gs_db_finalise(stmt);
    check(i == 1000, "a thousand rows written through one statement");

    stmt = gs_db_prepare(db, "SELECT COUNT(*), SUM(v) FROM n;");
    if (stmt != NULL && gs_db_step(stmt) == 1) {
        count = gs_db_int(stmt, 0);
        check(gs_db_int(stmt, 1) == 499500,
              "the sum of nought to nine hundred and ninety nine is right");
    }
    gs_db_finalise(stmt);
    check(count == 1000, "all thousand rows are there");

    gs_db_close(db);
    scrub();
}

int main(void)
{
    gs_log_set_level(GS_LOG_ERROR);

    test_bad_arguments();
    test_round_trip();
    test_transaction();
    test_failures_are_reported();
    test_reuse();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
