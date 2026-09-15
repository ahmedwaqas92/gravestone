/* catalogue_internal.h
 *
 * Shared between the .c files in this directory and nowhere else.
 */
#ifndef GS_CATALOGUE_INTERNAL_H
#define GS_CATALOGUE_INTERNAL_H

#include "catalogue.h"

/* The working space a model needs beyond its own weights, as a fraction.
 * One and a fifth is an assumption rather than a measurement. */
#define HEADROOM_NUM 6
#define HEADROOM_DEN 5

/* What the kernel spends on its own bookkeeping, as a fraction of memory.
 * Linux keeps a record of 64 bytes for every 4096 byte page it owns,
 * which is 64 / 4096, or one and a half per cent. The figure is rounded
 * up to four per cent to cover the rest of the kernel's tables, and that
 * rounding is a judgement rather than a measurement. */
#define KERNEL_SHARE_NUM 4
#define KERNEL_SHARE_DEN 100

/* What the operating system itself holds before anybody opens anything.
 * Measured on one Windows 11 machine at over 1.26 GB of services, and
 * taken at a quarter of a gigabyte for a machine with no screen. */
#define OS_FLOOR_DESKTOP  1073741824LL   /* 1 GB */
#define OS_FLOOR_HEADLESS  268435456LL   /* 256 MB */

/* What the person at the keyboard has open. A browser, a mail client and
 * a spreadsheet do not grow when memory is added, and people fill the
 * space they are given, so the total climbs with memory and flattens.
 *
 * DESK_DEBT_BASE is the reading at the smallest machine worth aiming at,
 * doubling for every doubling of memory above it. Calibrated against one
 * laptop holding 3029139456 bytes of user programs in 8241831936 bytes of
 * memory, which is one reading behind a constant that shapes every
 * verdict.
 *
 * The figure is fitted through doublings_scaled rather than through an
 * exact logarithm, since that routine reads a little under the curve and
 * a constant fitted to the curve would arrive short. Feeding 8241831936
 * back through gives 565 on the doubling scale, and this constant then
 * gives 1372494161 * 565 / 256 = 3029137503, which is 1953 bytes under
 * the reading it was fitted to. Whole number division loses that much
 * both ways round, so the two cannot be made to meet exactly. */
#define DESK_DEBT_BASE 1372494161LL
#define DESK_DEBT_FLOOR_BYTES 2147483648LL /* the 2 GB machine it is measured from */

/* The least of a model that has to sit on the card for a divided model
 * to be worth listing, as a fraction.
 *
 * Every byte left in system memory is read across the bus once for every
 * word produced, so a model with a third of itself outside the card is
 * read a third of the way across that bus, per word, forever. Seven
 * tenths is where a divided model still answers inside a person's
 * patience on the machines measured so far, which is one laptop. The
 * figure is a judgement rather than a measurement. */
#define SPLIT_FLOOR_NUM GS_CATALOGUE_SPLIT_FLOOR
#define SPLIT_FLOOR_DEN 100

/* What the inference server keeps back on the card, beyond whatever the
 * screen is already holding.
 *
 * llama.cpp works downward from the whole model until what it would place
 * leaves this much of the card free, and it refuses to go below a hard
 * minimum under that. Measured once on this laptop, where the card
 * reported 3301 MiB free and the server placed 2271 MiB, six MiB short of
 * 3301 less this figure.
 *
 * What the screen holds is measured rather than assumed, since
 * nvidia-smi reports free memory directly and detect reads it.
 *
 * One reading on one machine with one runtime, so this is a starting
 * figure rather than a law. */
#define GRAPHICS_HOLDBACK 1073741824LL   /* 1 GiB */

/* The most of a machine the reserve may ever claim, as a fraction. A
 * small machine running a desktop can model out to more than it holds,
 * and a reserve of everything refuses every model while reporting the
 * model as too large, which names the wrong reason. Leaving a quarter
 * keeps the refusal honest, since the model is then measured against a
 * real remainder. */
#define RESERVE_CAP_NUM 3
#define RESERVE_CAP_DEN 4

/* How much of the machine is spoken for before a model is loaded. */
long long gs_catalogue_reserve(const gs_detect_report_t *machine);

#endif
