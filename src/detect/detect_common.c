/* detect_common.c
 *
 * The parts of reading a machine that belong to no platform: turning a
 * report into a short identifier, turning it into lines a person can
 * read, and the module record that carries all of it.
 */
#include "detect.h"
#include "gravestone.h"
#include "hash.h"
#include "log.h"
#include "str.h"

#include <stdio.h>
#include <string.h>

int gs_detect_fingerprint(const gs_detect_report_t *report, char *out,
                          size_t cap)
{
    char material[1024];

    if (report == NULL || out == NULL || cap < 17)
        return GS_ERR_ARG;

    /* Free space is left out, since it changes constantly and would make
     * every launch look like new hardware. */
    {
        int used = snprintf(material, sizeof material,
                            "%s|%s|%s|%s|%d|%lld|%s|%lld",
                            report->os, report->kernel, report->arch,
                            report->cpu_model, report->cpu_cores,
                            report->ram_total_bytes, report->gpu,
                            report->gpu_memory_bytes);
        int i;

        for (i = 0; i < report->disk_count && used > 0 &&
                    (size_t)used < sizeof material; i++)
            used += snprintf(material + used, sizeof material - (size_t)used,
                             "|%s:%s:%lld", report->disk[i].mount,
                             report->disk[i].fs,
                             report->disk[i].total_bytes);
    }

    return gs_hash_hex(gs_hash_text(material), out, cap);
}

int gs_detect_describe(const gs_detect_report_t *report, char *out,
                       size_t cap)
{
    if (report == NULL || out == NULL || cap == 0)
        return GS_ERR_ARG;

    {
        int used = snprintf(out, cap,
            "os\t%s\n"
            "kernel\t%s\n"
            "arch\t%s\n"
            "cpu\t%s\n"
            "cores\t%d\n"
            "ram\t%lld\n"
            "gpu\t%s\n"
            "gpu_memory\t%lld\n"
            "disk_total\t%lld\n"
            "disk_free\t%lld\n"
            "disks\t%d\n",
            report->os, report->kernel, report->arch, report->cpu_model,
            report->cpu_cores, report->ram_total_bytes, report->gpu,
            report->gpu_memory_bytes, report->disk_total_bytes,
            report->disk_free_bytes, report->disk_count);
        int i;

        for (i = 0; i < report->disk_count && used > 0 &&
                    (size_t)used < cap; i++)
            used += snprintf(out + used, cap - (size_t)used,
                             "disk\t%s\t%s\t%s\t%lld\t%lld\n",
                             report->disk[i].mount, report->disk[i].device,
                             report->disk[i].fs, report->disk[i].total_bytes,
                             report->disk[i].free_bytes);
        return used;
    }
}

static int detect_init(void)
{
    return GS_OK;
}

static int detect_run(int argc, char **argv)
{
    gs_detect_report_t report;
    char text[2048];
    char print[17];

    (void)argc;
    (void)argv;

    if (gs_detect_read(&report) != GS_OK) {
        gs_log_error("detect: could not read the machine");
        return GS_ERR;
    }
    gs_detect_describe(&report, text, sizeof text);
    gs_detect_fingerprint(&report, print, sizeof print);

    printf("%s", text);
    printf("fingerprint\t%s\n", print);
    return GS_OK;
}

static void detect_shutdown(void)
{
}

const gs_module gs_detect_module = {
    "detect",
    "read this machine and print what it is",
    detect_init,
    detect_run,
    detect_shutdown
};
