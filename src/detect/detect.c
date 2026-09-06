/* detect.c
 *
 * Linux reads /proc, which is a set of files the kernel writes on demand
 * rather than files on a disk. macOS and Windows would answer through
 * their own calls, and each belongs behind its own block here.
 */
#include "detect.h"
#include "gravestone.h"
#include "hash.h"
#include "log.h"
#include "str.h"
#include "paths.h"
#include "proc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

static void set_unknown(char *field, size_t cap)
{
    snprintf(field, cap, "unknown");
}

/* Pulls the value from a "key : value" line in a /proc file. */
static int proc_field(const char *path, const char *key, char *out,
                      size_t cap)
{
    FILE *f = fopen(path, "r");
    char line[512];
    size_t keylen = strlen(key);
    int found = 0;

    if (f == NULL)
        return GS_ERR_IO;

    while (fgets(line, sizeof line, f) != NULL) {
        char *colon;
        char *value;

        if (strncmp(line, key, keylen) != 0)
            continue;
        colon = strchr(line, ':');
        if (colon == NULL)
            continue;
        value = colon + 1;
        while (*value == ' ' || *value == '\t')
            value++;
        value[strcspn(value, "\n")] = '\0';
        gs_str_copy(out, cap, value);
        found = 1;
        break;
    }
    fclose(f);
    return found ? GS_OK : GS_ERR;
}

static long long meminfo_kb(const char *key)
{
    char value[64];

    if (proc_field("/proc/meminfo", key, value, sizeof value) != GS_OK)
        return 0;
    return strtoll(value, NULL, 10);
}

static int count_processors(void)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[512];
    int count = 0;

    if (f == NULL)
        return 0;
    while (fgets(line, sizeof line, f) != NULL)
        if (strncmp(line, "processor", 9) == 0)
            count++;
    fclose(f);
    return count;
}

/* Filesystems the kernel invents rather than ones sitting on a drive. */
static int is_pseudo(const char *fs)
{
    static const char *names[] = {
        "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "cgroup", "cgroup2",
        "securityfs", "pstore", "bpf", "debugfs", "tracefs", "fusectl",
        "configfs", "mqueue", "hugetlbfs", "autofs", "binfmt_misc",
        "rpc_pipefs", "nsfs", "overlay", "ramfs", "rootfs", "none",
        "efivarfs", "selinuxfs", "fuse.portal", "fuse.gvfsd-fuse"
    };
    size_t i;

    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strcmp(fs, names[i]) == 0)
            return 1;
    return 0;
}

/* The mount table escapes space, tab, newline and backslash as a backslash
 * followed by three octal digits, so a Windows drive arrives as C:\134
 * rather than C:\. This puts the real characters back. */
static void unescape_mount(char *text)
{
    char *read = text;
    char *write = text;

    while (*read != '\0') {
        if (read[0] == '\\' && read[1] >= '0' && read[1] <= '7' &&
            read[2] >= '0' && read[2] <= '7' &&
            read[3] >= '0' && read[3] <= '7') {
            int value = (read[1] - '0') * 64 + (read[2] - '0') * 8 +
                        (read[3] - '0');
            *write++ = (char)value;
            read += 4;
            continue;
        }
        *write++ = *read++;
    }
    *write = '\0';
}

/* The options field starts with rw or ro, so a read only share never
 * counts as storage the machine can use. */
static int is_read_only(const char *options)
{
    return strncmp(options, "ro,", 3) == 0 || strcmp(options, "ro") == 0;
}

static void read_disks(gs_detect_report_t *out)
{
    FILE *f = fopen("/proc/mounts", "r");
    char line[1024];

    out->disk_count = 0;
    out->disk_total_bytes = 0;
    out->disk_free_bytes = 0;

    if (f == NULL)
        return;

    while (fgets(line, sizeof line, f) != NULL &&
           out->disk_count < GS_DETECT_MAX_DISKS) {
        char device[128], mount[256], fs[64], options[512];
        struct statvfs vfs;
        long long total, avail;
        int i, seen = 0;

        if (sscanf(line, "%127s %255s %63s %511s", device, mount, fs,
                   options) != 4)
            continue;
        if (is_pseudo(fs) || is_read_only(options))
            continue;

        unescape_mount(device);
        unescape_mount(mount);

        /* A mount can be listed while the drive behind it is unreachable,
         * which a Windows drive under WSL does when it has gone away. */
        if (statvfs(mount, &vfs) != 0) {
            gs_log_debug("detect: %s is mounted from %s but cannot be "
                         "reached: %s", mount, device, strerror(errno));
            continue;
        }

        total = (long long)vfs.f_blocks * (long long)vfs.f_frsize;
        avail = (long long)vfs.f_bavail * (long long)vfs.f_frsize;
        if (total <= 0)
            continue;

        /* One drive mounted twice is still one drive. */
        for (i = 0; i < out->disk_count; i++)
            if (strcmp(out->disk[i].device, device) == 0)
                seen = 1;
        if (seen)
            continue;

        gs_str_copy(out->disk[out->disk_count].mount,
                    sizeof out->disk[0].mount, mount);
        gs_str_copy(out->disk[out->disk_count].device,
                    sizeof out->disk[0].device, device);
        gs_str_copy(out->disk[out->disk_count].fs,
                    sizeof out->disk[0].fs, fs);
        out->disk[out->disk_count].total_bytes = total;
        out->disk[out->disk_count].free_bytes = avail;
        out->disk_count++;

        out->disk_total_bytes += total;
        out->disk_free_bytes += avail;
    }
    fclose(f);
}

/* The vendor tool answers with a name and the memory on the card. Falling
 * back through the other routes keeps this honest on a machine without it,
 * where the field reads "unknown" rather than a guess. */
static void read_gpu(gs_detect_report_t *out)
{
    char answer[512];
    char *comma;

    set_unknown(out->gpu, sizeof out->gpu);
    out->gpu_memory_bytes = 0;

    /* A hung tool must never hold the interface still, so it is given five
     * seconds and no more. */
    if (gs_proc_capture(
            "timeout 5 nvidia-smi --query-gpu=name,memory.total "
            "--format=csv,noheader", answer, sizeof answer) == GS_OK) {
        answer[strcspn(answer, "\n")] = '\0';
        comma = strchr(answer, ',');
        if (comma != NULL) {
            long long mib;

            *comma = '\0';
            mib = strtoll(comma + 1, NULL, 10);
            if (mib > 0)
                out->gpu_memory_bytes = mib * 1024 * 1024;
        }
        gs_str_copy(out->gpu, sizeof out->gpu, answer);
        return;
    }

    if (gs_proc_capture(
            "timeout 5 lspci | grep -i -m1 'vga\\|3d\\|display'",
            answer, sizeof answer) == GS_OK) {
        char *colon = strrchr(answer, ':');
        gs_str_copy(out->gpu, sizeof out->gpu,
                    colon != NULL ? colon + 1 : answer);
        return;
    }

    if (gs_proc_capture(
            "cat /sys/class/drm/card0/device/label 2>/dev/null",
            answer, sizeof answer) == GS_OK)
        gs_str_copy(out->gpu, sizeof out->gpu, answer);
}

int gs_detect_read(gs_detect_report_t *out)
{
    struct utsname sys;
    long long kb;

    if (out == NULL)
        return GS_ERR_ARG;

    memset(out, 0, sizeof *out);
    set_unknown(out->os, sizeof out->os);
    set_unknown(out->kernel, sizeof out->kernel);
    set_unknown(out->arch, sizeof out->arch);
    set_unknown(out->cpu_model, sizeof out->cpu_model);

    if (uname(&sys) == 0) {
        gs_str_copy(out->os, sizeof out->os, sys.sysname);
        gs_str_copy(out->kernel, sizeof out->kernel, sys.release);
        gs_str_copy(out->arch, sizeof out->arch, sys.machine);
    }

    if (proc_field("/proc/cpuinfo", "model name", out->cpu_model,
                   sizeof out->cpu_model) != GS_OK)
        proc_field("/proc/cpuinfo", "Model", out->cpu_model,
                   sizeof out->cpu_model);

    out->cpu_cores = count_processors();
    if (out->cpu_cores <= 0)
        out->cpu_cores = (int)sysconf(_SC_NPROCESSORS_ONLN);

    kb = meminfo_kb("MemTotal");
    out->ram_total_bytes = kb * 1024;

    read_disks(out);
    read_gpu(out);

    gs_log_debug("detect: %s %s, %d processors, %lld bytes of memory, "
                 "%d disks, gpu %s",
                 out->os, out->arch, out->cpu_cores, out->ram_total_bytes,
                 out->disk_count, out->gpu);
    return GS_OK;
}

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
