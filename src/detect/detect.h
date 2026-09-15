/* detect.h
 *
 * Reads what the machine is. Every field is filled in on every platform,
 * with the string "unknown" where the platform will not say.
 */
#ifndef GS_DETECT_H
#define GS_DETECT_H

#include <stddef.h>
#include "gravestone.h"

#define GS_DETECT_MAX_DISKS 12

typedef struct {
    char      mount[128];      /* where it is attached */
    char      device[96];      /* what it is, as the system names it */
    char      fs[32];          /* the filesystem on it */
    long long total_bytes;
    long long free_bytes;
} gs_detect_disk_t;

typedef struct {
    char      os[64];
    char      kernel[128];
    char      arch[32];
    char      cpu_model[160];
    int       cpu_cores;          /* logical processors the system reports */
    long long ram_total_bytes;
    /* What the kernel says could be handed out right now, which is memory
     * nothing is holding plus the part of the file cache it can drop.
     * Zero when the platform will not say. */
    long long ram_free_bytes;
    /* Non zero when a graphical session is running on this machine. A
     * desktop brings a browser, a mail client and a window manager along
     * with it, and none of those appear on a machine in a rack. */
    int       desktop;
    /* Non zero when this is a guest inside a larger machine, such as a
     * virtual machine or a container. The memory reported is then an
     * allowance carved out for the guest, and the programs the person has
     * open are running outside it, so they cannot be charged against it. */
    int       guest;

    char      gpu[160];
    /* Every card the machine can see, added together, since a model too
     * large for one may still be split across two. Zero when no card
     * will say what it holds. */
    long long gpu_memory_bytes;
    /* What is free on those cards at this moment. Graphics memory has few
     * other tenants, so this reading holds still in a way the memory one
     * does not. Zero when no card will say. */
    long long gpu_free_bytes;
    int       gpu_count;

    int               disk_count;
    gs_detect_disk_t  disk[GS_DETECT_MAX_DISKS];
    long long         disk_total_bytes;   /* summed across every disk */
    long long         disk_free_bytes;
} gs_detect_report_t;

int gs_detect_read(gs_detect_report_t *out);

/* A short identifier for this configuration. Free space is left out on
 * purpose, since it moves every minute and would make every launch look
 * like a hardware change. Disk sizes and mount points are included, so
 * attaching a new drive counts as one. */
int gs_detect_fingerprint(const gs_detect_report_t *report, char *out,
                          size_t cap);

/* Renders the report as one line per field, for storing and for showing.
 * Returns the number of characters written. */
int gs_detect_describe(const gs_detect_report_t *report, char *out,
                       size_t cap);

extern const gs_module gs_detect_module;

#endif
