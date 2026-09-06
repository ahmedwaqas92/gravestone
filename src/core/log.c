#include "log.h"
#include "gravestone.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gs_log_level_t current_level = GS_LOG_INFO;
static int level_from_env_read = 0;

static const char *label(gs_log_level_t level)
{
    switch (level) {
    case GS_LOG_ERROR: return "error";
    case GS_LOG_WARN:  return "warn ";
    case GS_LOG_INFO:  return "info ";
    default:           return "debug";
    }
}

static void read_env_once(void)
{
    const char *value;

    if (level_from_env_read)
        return;
    level_from_env_read = 1;

    value = getenv("GS_LOG");
    if (value == NULL)
        return;
    if (strcmp(value, "error") == 0) current_level = GS_LOG_ERROR;
    else if (strcmp(value, "warn") == 0)  current_level = GS_LOG_WARN;
    else if (strcmp(value, "info") == 0)  current_level = GS_LOG_INFO;
    else if (strcmp(value, "debug") == 0) current_level = GS_LOG_DEBUG;
}

void gs_log_set_level(gs_log_level_t level)
{
    current_level = level;
    level_from_env_read = 1;
}

gs_log_level_t gs_log_get_level(void)
{
    read_env_once();
    return current_level;
}

void gs_log_write(gs_log_level_t level, const char *fmt, ...)
{
    va_list args;

    read_env_once();
    if (level > current_level)
        return;

    fprintf(stderr, "[%s] ", label(level));
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}
