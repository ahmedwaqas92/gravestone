/* detect_linux.c
 *
 * Reading this machine on Linux, from the files the kernel keeps in
 * /proc and from the mount table. /proc holds files the kernel writes on
 * demand rather than files on a disk.
 *
 * The Windows reading lives in detect_win32.c. Both fill in the same
 * report, so nothing above this directory knows which one ran.
 */
#include "detect.h"
#include "gravestone.h"
#include "hash.h"
#include "log.h"
#include "str.h"
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

/* Whether anybody is sitting in front of this machine. A browser and a
 * mail client only turn up where there is a screen to show them on, so a
 * display server address in the environment is the evidence.
 *
 * The address has to name this machine. A session reached over ssh with
 * forwarding carries a host in front of the colon, as in localhost:10.0,
 * and the programs it draws are running on the machine at the far end.
 * Charging a rack machine for somebody else's browser would refuse
 * models it can run perfectly well. */
static int desktop_running(void)
{
    const char *wayland = getenv("WAYLAND_DISPLAY");
    const char *x11 = getenv("DISPLAY");

    if (wayland != NULL && wayland[0] != '\0')
        return 1;
    return x11 != NULL && x11[0] == ':';
}

/* Whether this is a guest inside a larger machine. The memory a guest
 * reports is an allowance somebody else decided, and the browser and the
 * mail client are running outside that allowance, so charging the guest
 * for them would refuse models it can run.
 *
 * A container is spotted by the marker its runtime leaves behind, and a
 * virtual machine by the name the kernel gives itself. Nothing here asks
 * a tool, since a guest with no tools installed is the common case. */
static int running_as_guest(void)
{
    char text[256];
    FILE *f;

    if (access("/.dockerenv", F_OK) == 0)
        return 1;
    if (access("/run/.containerenv", F_OK) == 0)
        return 1;

    f = fopen("/proc/version", "r");
    if (f != NULL) {
        size_t got = fread(text, 1, sizeof text - 1, f);

        fclose(f);
        text[got] = '\0';
        /* WSL names itself in the kernel version string. */
        if (strstr(text, "microsoft") != NULL ||
            strstr(text, "Microsoft") != NULL)
            return 1;
    }

    f = fopen("/sys/class/dmi/id/product_name", "r");
    if (f != NULL) {
        char *end;

        if (fgets(text, sizeof text, f) == NULL)
            text[0] = '\0';
        fclose(f);
        end = strchr(text, '\n');
        if (end != NULL)
            *end = '\0';
        /* The names the common hypervisors write into the firmware. */
        if (strstr(text, "VirtualBox") != NULL ||
            strstr(text, "VMware") != NULL ||
            strstr(text, "KVM") != NULL ||
            strstr(text, "Virtual Machine") != NULL ||
            strstr(text, "HVM domU") != NULL)
            return 1;
    }
    return 0;
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
    /* Room for a long list, since one line runs about fifty characters
     * and a machine with a dozen cards would be cut short by a smaller
     * buffer, losing memory that a model could have used. */
    char answer[2048];
    char *comma;

    set_unknown(out->gpu, sizeof out->gpu);
    out->gpu_memory_bytes = 0;
    out->gpu_free_bytes = 0;

    /* Every card the machine can see counts, since a model too large for
     * one may still be split across two. The memory of all of them is
     * added up and the first one names the set.
     *
     * A hung tool must never hold the interface still, so it is given
     * five seconds and no more. */
    if (gs_proc_capture(
            "timeout 5 nvidia-smi --query-gpu=name,memory.total,memory.free "
            "--format=csv,noheader", answer, sizeof answer) == GS_OK) {
        char *line = answer;
        int cards = 0;

        while (line != NULL && *line != '\0') {
            char *end = strchr(line, '\n');

            if (end != NULL)
                *end = '\0';
            comma = strchr(line, ',');
            if (comma != NULL) {
                long long mib;
                char *second;

                *comma = '\0';
                mib = strtoll(comma + 1, NULL, 10);
                if (mib > 0)
                    out->gpu_memory_bytes += mib * 1024 * 1024;
                /* The free figure follows the total on the same line. A
                 * card that will not say leaves it at nought, which the
                 * room maths reads as no reading rather than no memory. */
                second = strchr(comma + 1, ',');
                if (second != NULL) {
                    long long spare = strtoll(second + 1, NULL, 10);

                    if (spare > 0)
                        out->gpu_free_bytes += spare * 1024 * 1024;
                }
                if (cards == 0)
                    gs_str_copy(out->gpu, sizeof out->gpu, line);
                cards++;
            }
            line = end != NULL ? end + 1 : NULL;
        }
        if (cards > 1) {
            char many[sizeof out->gpu + 24];

            snprintf(many, sizeof many, "%s and %d more", out->gpu,
                     cards - 1);
            gs_str_copy(out->gpu, sizeof out->gpu, many);
        }
        out->gpu_count = cards;
        if (cards > 0)
            return;
    }

    if (gs_proc_capture(
            "timeout 5 lspci | grep -i -m1 'vga\\|3d\\|display'",
            answer, sizeof answer) == GS_OK) {
        char *colon = strrchr(answer, ':');
        gs_str_copy(out->gpu, sizeof out->gpu,
                    colon != NULL ? colon + 1 : answer);
        /* Named without its memory, and still one card. */
        out->gpu_count = 1;
        return;
    }

    if (gs_proc_capture(
            "cat /sys/class/drm/card0/device/label 2>/dev/null",
            answer, sizeof answer) == GS_OK) {
        gs_str_copy(out->gpu, sizeof out->gpu, answer);
        out->gpu_count = 1;
    }
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

    /* MemAvailable is the kernel's own estimate of what a new allocation
     * could take without pushing anything out to swap, so it counts the
     * reclaimable part of the file cache. An older kernel without the
     * field falls back to MemFree, which understates it. */
    kb = meminfo_kb("MemAvailable");
    if (kb <= 0)
        kb = meminfo_kb("MemFree");
    out->ram_free_bytes = kb * 1024;

    out->desktop = desktop_running();
    out->guest = running_as_guest();

    read_disks(out);
    read_gpu(out);

    gs_log_debug("detect: %s %s, %d processors, %lld bytes of memory, "
                 "%d disks, gpu %s",
                 out->os, out->arch, out->cpu_cores, out->ram_total_bytes,
                 out->disk_count, out->gpu);
    return GS_OK;
}

