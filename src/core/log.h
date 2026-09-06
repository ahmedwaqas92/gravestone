#ifndef GS_CORE_LOG_H
#define GS_CORE_LOG_H

typedef enum {
    GS_LOG_ERROR = 0,
    GS_LOG_WARN  = 1,
    GS_LOG_INFO  = 2,
    GS_LOG_DEBUG = 3
} gs_log_level_t;

void gs_log_set_level(gs_log_level_t level);
gs_log_level_t gs_log_get_level(void);
void gs_log_write(gs_log_level_t level, const char *fmt, ...);

#define gs_log_error(...) gs_log_write(GS_LOG_ERROR, __VA_ARGS__)
#define gs_log_warn(...)  gs_log_write(GS_LOG_WARN,  __VA_ARGS__)
#define gs_log_info(...)  gs_log_write(GS_LOG_INFO,  __VA_ARGS__)
#define gs_log_debug(...) gs_log_write(GS_LOG_DEBUG, __VA_ARGS__)

#endif
