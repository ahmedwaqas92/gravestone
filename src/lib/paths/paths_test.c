#include "paths.h"
#include "gravestone.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, checks;

static void check(int c, const char *what)
{
    checks++;
    if (c) printf("  ok    %s\n", what);
    else { printf("  FAIL  %s\n", what); failures++; }
}

int main(void)
{
    char dir[512], file[512];
    struct stat st;
    const char *scratch = "/tmp/gravestone-paths-test/deep/nested";

    gs_log_set_level(GS_LOG_ERROR);

    printf("bad arguments\n");
    check(gs_paths_data_dir(NULL, 100) == GS_ERR_ARG, "null buffer refused");
    check(gs_paths_data_dir(dir, 1) == GS_ERR_ARG, "tiny buffer refused");
    check(gs_paths_db_file(NULL, 100) == GS_ERR_ARG,
          "null buffer refused for the file");

    printf("the real directory\n");
    check(gs_paths_data_dir(dir, sizeof dir) == GS_OK, "directory resolved");
    printf("        %s\n", dir);
    check(strstr(dir, "gravestone") != NULL, "the name appears in the path");
    check(dir[0] == '/', "the path is absolute");
    check(stat(dir, &st) == 0 && S_ISDIR(st.st_mode), "it exists");
    check((st.st_mode & 0077) == 0,
          "no other account on this machine can reach it");

    check(gs_paths_db_file(file, sizeof file) == GS_OK, "file path resolved");
    printf("        %s\n", file);
    check(strncmp(file, dir, strlen(dir)) == 0,
          "the file sits inside the directory");
    check(strstr(file, ".sqlite") != NULL, "it is named as a database");

    printf("an override, for tests\n");
    system("rm -rf /tmp/gravestone-paths-test");
    gs_paths_override(scratch);
    check(gs_paths_data_dir(dir, sizeof dir) == GS_OK, "override resolved");
    check(strcmp(dir, scratch) == 0, "the override is what came back");
    check(stat(scratch, &st) == 0 && S_ISDIR(st.st_mode),
          "every level of the tree was created");
    check((st.st_mode & 0077) == 0, "the created directory is private");

    gs_paths_override(NULL);
    check(gs_paths_data_dir(dir, sizeof dir) == GS_OK &&
          strcmp(dir, scratch) != 0, "clearing the override restores it");
    system("rm -rf /tmp/gravestone-paths-test");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
