#include "proc.h"
#include "gravestone.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gs_proc_capture(const char *command, char *out, size_t cap)
{
    FILE *pipe;
    size_t filled = 0;
    int status;

    if (command == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;
    out[0] = '\0';

    /* Standard error is thrown away so a tool complaining about a missing
     * driver does not end up presented as a graphics card. */
    {
        char line[512];
        char full[600];

        snprintf(full, sizeof full, "%s 2>/dev/null", command);
        pipe = popen(full, "r");
        if (pipe == NULL) {
            gs_log_debug("proc: could not start %s", command);
            return GS_ERR_IO;
        }

        while (fgets(line, sizeof line, pipe) != NULL) {
            size_t len = strlen(line);
            size_t room = cap - 1 - filled;

            if (room == 0)
                break;
            if (len > room)
                len = room;
            memcpy(out + filled, line, len);
            filled += len;
            out[filled] = '\0';
        }
    }

    status = pclose(pipe);
    if (status != 0) {
        gs_log_debug("proc: %s exited with %d", command, status);
        out[0] = '\0';
        return GS_ERR;
    }

    /* A trailing newline is never wanted by a caller storing one value. */
    while (filled > 0 && (out[filled - 1] == '\n' || out[filled - 1] == '\r'))
        out[--filled] = '\0';

    return filled > 0 ? GS_OK : GS_ERR;
}

int gs_proc_exists(const char *name)
{
    char command[256];
    char answer[256];

    if (name == NULL || name[0] == '\0')
        return 0;
    /* Anything other than a plain name could carry shell syntax. */
    if (strpbrk(name, " \t;&|<>$`\\\"'\n") != NULL)
        return 0;

    snprintf(command, sizeof command, "command -v %s", name);
    return gs_proc_capture(command, answer, sizeof answer) == GS_OK;
}
