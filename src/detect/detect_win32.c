/* detect_win32.c
 *
 * Reading this machine on Windows, through the calls the system provides
 * rather than through files. Every field the report carries is filled,
 * with the string "unknown" where Windows will not say.
 *
 * The Linux reading lives in detect_linux.c. Both fill in the same
 * report, so nothing above this directory knows which one ran.
 */
#include "detect.h"
#include "gravestone.h"
#include "log.h"
#include "proc.h"
#include "str.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_unknown(char *field, size_t cap)
{
    gs_str_copy(field, cap, "unknown");
}

/* The processor's own name, which it reports through the registry the
 * system fills in at boot. */
static void read_processor(gs_detect_report_t *out)
{
    HKEY key;
    char name[160];
    DWORD size = sizeof name;
    SYSTEM_INFO info;

    set_unknown(out->cpu_model, sizeof out->cpu_model);

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                     "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueEx(key, "ProcessorNameString", NULL, NULL,
                            (LPBYTE)name, &size) == ERROR_SUCCESS) {
            name[sizeof name - 1] = '\0';
            gs_str_copy(out->cpu_model, sizeof out->cpu_model, name);
        }
        RegCloseKey(key);
    }

    GetSystemInfo(&info);
    out->cpu_cores = (int)info.dwNumberOfProcessors;

    switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        gs_str_copy(out->arch, sizeof out->arch, "x86_64");
        break;
    case PROCESSOR_ARCHITECTURE_ARM64:
        gs_str_copy(out->arch, sizeof out->arch, "arm64");
        break;
    case PROCESSOR_ARCHITECTURE_INTEL:
        gs_str_copy(out->arch, sizeof out->arch, "x86");
        break;
    default:
        set_unknown(out->arch, sizeof out->arch);
        break;
    }
}

static void read_memory(gs_detect_report_t *out)
{
    MEMORYSTATUSEX memory;

    memory.dwLength = sizeof memory;
    if (GlobalMemoryStatusEx(&memory)) {
        out->ram_total_bytes = (long long)memory.ullTotalPhys;
        /* What could be handed out now. Windows counts the standby list
         * as available in its own tools, and this call leaves it out, so
         * the figure understates what is really reachable. Understating
         * reserves more, which is the safer direction. */
        out->ram_free_bytes = (long long)memory.ullAvailPhys;
    }
}

/* The build number, which is what tells one Windows from another. */
static void read_system(gs_detect_report_t *out)
{
    HKEY key;
    char product[64] = {0};
    char build[32] = {0};
    DWORD size;

    gs_str_copy(out->os, sizeof out->os, "Windows");
    set_unknown(out->kernel, sizeof out->kernel);

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                     "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                     0, KEY_READ, &key) != ERROR_SUCCESS)
        return;

    size = sizeof product;
    if (RegQueryValueEx(key, "ProductName", NULL, NULL, (LPBYTE)product,
                        &size) == ERROR_SUCCESS) {
        product[sizeof product - 1] = '\0';
        gs_str_copy(out->os, sizeof out->os, product);
    }
    size = sizeof build;
    if (RegQueryValueEx(key, "CurrentBuild", NULL, NULL, (LPBYTE)build,
                        &size) == ERROR_SUCCESS) {
        build[sizeof build - 1] = '\0';
        gs_str_copy(out->kernel, sizeof out->kernel, build);
    }
    RegCloseKey(key);
}

/* Every drive letter the system has attached, with the room left on it.
 * A drive with no medium in it answers nothing and is passed over. */
static void read_disks(gs_detect_report_t *out)
{
    char letters[512];
    DWORD written;
    const char *at;

    out->disk_count = 0;
    out->disk_total_bytes = 0;
    out->disk_free_bytes = 0;

    written = GetLogicalDriveStrings((DWORD)sizeof letters, letters);
    if (written == 0 || written > sizeof letters)
        return;

    for (at = letters; *at != '\0' && out->disk_count < GS_DETECT_MAX_DISKS;
         at += strlen(at) + 1) {
        ULARGE_INTEGER free_to_caller, total, free_total;
        gs_detect_disk_t *disk;
        UINT kind = GetDriveType(at);
        char name[MAX_PATH] = {0};
        char fs[64] = {0};

        /* A network share and a CD tray both answer slowly or not at
         * all, and neither is where a model would sit. */
        if (kind != DRIVE_FIXED && kind != DRIVE_REMOVABLE)
            continue;
        if (!GetDiskFreeSpaceEx(at, &free_to_caller, &total, &free_total))
            continue;

        disk = &out->disk[out->disk_count];
        gs_str_copy(disk->mount, sizeof disk->mount, at);
        gs_str_copy(disk->device, sizeof disk->device, at);
        set_unknown(disk->fs, sizeof disk->fs);
        if (GetVolumeInformation(at, name, sizeof name, NULL, NULL, NULL,
                                 fs, sizeof fs))
            gs_str_copy(disk->fs, sizeof disk->fs, fs);

        disk->total_bytes = (long long)total.QuadPart;
        disk->free_bytes = (long long)free_to_caller.QuadPart;
        out->disk_total_bytes += disk->total_bytes;
        out->disk_free_bytes += disk->free_bytes;
        out->disk_count++;
    }
}

/* The graphics card, from the display the system is using. Its memory is
 * left at zero, since Windows reports that through interfaces this
 * program does not carry. */
static void read_gpu(gs_detect_report_t *out)
{
    DISPLAY_DEVICE device;
    /* Room for a long list, since one line runs about fifty characters
     * and a machine with a dozen cards would be cut short. */
    char answer[2048];

    set_unknown(out->gpu, sizeof out->gpu);
    out->gpu_memory_bytes = 0;
    out->gpu_free_bytes = 0;

    /* The vendor tool ships with the driver and names the card along with
     * how much memory it holds. EnumDisplayDevices names whichever
     * adapter is driving the screen, which on a laptop is the one built
     * into the processor rather than the card the model would run on. */
    if (gs_proc_capture(
            "nvidia-smi --query-gpu=name,memory.total,memory.free "
            "--format=csv,noheader", answer, sizeof answer) == GS_OK) {
        char *line = answer;
        int cards = 0;

        while (line != NULL && *line != '\0') {
            char *end = strchr(line, '\n');
            char *comma;

            if (end != NULL)
                *end = '\0';
            comma = strchr(line, ',');
            if (comma != NULL) {
                char *second;
                long long mib;

                *comma = '\0';
                mib = strtoll(comma + 1, NULL, 10);
                if (mib > 0)
                    out->gpu_memory_bytes += mib * 1024 * 1024;
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
        /* The memory of every card is added up, so the name has to say
         * that more than one was counted. A name reading like one card
         * beside a figure covering several would misread as a single
         * enormous card. */
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

    memset(&device, 0, sizeof device);
    device.cb = sizeof device;
    if (EnumDisplayDevices(NULL, 0, &device, 0)) {
        gs_str_copy(out->gpu, sizeof out->gpu, device.DeviceString);
        /* A card named without its memory still counts as one card, so a
         * caller asking how many are present is answered. */
        out->gpu_count = 1;
    }
}

int gs_detect_read(gs_detect_report_t *out)
{
    if (out == NULL)
        return GS_ERR_ARG;

    memset(out, 0, sizeof *out);
    set_unknown(out->os, sizeof out->os);
    set_unknown(out->kernel, sizeof out->kernel);
    set_unknown(out->arch, sizeof out->arch);
    set_unknown(out->cpu_model, sizeof out->cpu_model);
    set_unknown(out->gpu, sizeof out->gpu);

    /* Windows runs a graphical session on all but a stripped server, and
     * treating a rack machine as a desktop only reserves more memory than
     * it needs, which errs the safe way. */
    out->desktop = 1;
    /* Windows here is the machine itself rather than a guest inside one.
     * A Windows virtual machine would report otherwise, and reading that
     * needs interfaces this program does not carry, so it is left at
     * nought and the reserve errs towards holding more back. */
    out->guest = 0;

    read_system(out);
    read_processor(out);
    read_memory(out);
    read_disks(out);
    read_gpu(out);

    gs_log_debug("detect: %s %s, %d processors, %lld bytes of memory, "
                 "%d disks, gpu %s", out->os, out->arch, out->cpu_cores,
                 out->ram_total_bytes, out->disk_count, out->gpu);
    return GS_OK;
}
