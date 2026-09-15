/* catalogue_test.h
 *
 * Shared between the two halves of the catalogue test and nowhere else.
 * The test grew past the length one file is allowed, so it runs as two
 * translation units making one program.
 */
#ifndef GS_CATALOGUE_TEST_H
#define GS_CATALOGUE_TEST_H

#include "detect.h"

/* Counts one check and prints its result. Defined in catalogue_test.c,
 * which owns the totals and main. */
void gs_catalogue_test_check(int condition, const char *what);

/* A machine with the memory sizes handed in and nothing else filled. */
gs_detect_report_t gs_catalogue_test_machine(long long ram, long long vram);

/* A machine with somebody sitting at it, given a reading of what is free.
 * Passing nought for a free figure means the platform would not say. */
gs_detect_report_t gs_catalogue_test_desk(long long ram, long long free_ram,
                                          long long vram,
                                          long long free_vram);

#define GB 1073741824LL

/* Both halves call these by the short names the checks read best with. */
#define check(c, w)      gs_catalogue_test_check((c), (w))
#define machine_of(r, v) gs_catalogue_test_machine((r), (v))
#define desk_of(r, f, v, g) gs_catalogue_test_desk((r), (f), (v), (g))

/* The sections living in catalogue_test_room.c, run from main. */
void test_the_reserve(void);
void test_fit_arithmetic(void);
void test_keeping_against_running(void);
void test_many_at_once(void);

#endif
