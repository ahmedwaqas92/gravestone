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

    char      gpu[160];
    long long gpu_memory_bytes;   /* 0 when the card will not say */

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
