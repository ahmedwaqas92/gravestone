/* store.h
 *
 * Remembers what this install found on this machine. Identity and
 * inventory are kept apart on purpose. The identity is a random value
 * written once, so it survives every hardware change. The inventory is a
 * series of snapshots, one per configuration the machine has been in.
 */
#ifndef GS_STORE_H
#define GS_STORE_H

#include <stddef.h>
#include "gravestone.h"
#include "detect.h"

int  gs_store_open(void);
void gs_store_close(void);

/* Sixteen hexadecimal characters naming this install. Generated on first
 * open and never derived from any component. */
int gs_store_install_id(char *out, size_t cap);

/* Records the report. A configuration already stored has its last seen
 * time updated rather than being written again, so launching daily costs
 * nothing. Sets *changed to 1 when the hardware differs from last time. */
int gs_store_mount(const gs_detect_report_t *report, int *changed);

/* Non zero when the last open removed readings taken by an older build. */
int gs_store_cleared_stale(void);

/* Non zero when the last open created the install rather than finding it.
 * An install that already existed and yet holds no readings has had them
 * taken away, which is a different situation from a first run. */
int gs_store_install_is_new(void);

/* Non zero once any snapshot exists for this install. */
int gs_store_is_mounted(void);

/* The newest snapshot. taken_at and fingerprint may be NULL. */
int gs_store_latest(gs_detect_report_t *report, char *fingerprint,
                    size_t print_cap, long long *mounted_at,
                    long long *last_seen);

/* How many distinct configurations this machine has been through. */
int gs_store_snapshot_count(void);

extern const gs_module gs_store_module;

#endif
