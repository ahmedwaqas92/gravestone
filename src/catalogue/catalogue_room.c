/* catalogue_room.c
 *
 * How much of a machine a model may actually draw on.
 *
 * Total memory is the size of the box. A model gets what is left after
 * the kernel, the desktop, and whatever the person already has open. That
 * remainder is what decides whether a model runs or crawls, so it is
 * worked out here rather than assumed.
 *
 * Two figures are produced and the larger wins. One is read off the
 * machine, which is exact for the moment it was taken and says nothing
 * about the moment after. The other is built from constants, which holds
 * still and describes a machine in ordinary use. Taking the larger keeps
 * a momentarily idle machine from promising room it will not have once
 * the person opens their mail.
 */
#include "catalogue.h"
#include "catalogue_internal.h"
#include "gravestone.h"

#include <string.h>

/* How many times memory doubles above the smallest machine worth aiming
 * at, scaled by 256 so the fraction survives without floating point.
 *
 * Integer arithmetic is used because a fit verdict has to come out the
 * same on every machine running this program, and floating point makes no
 * such promise across compilers and flags.
 *
 * The part of a doubling left over is straightened into a line, which
 * runs under the curve by up to nine per cent of one doubling at the
 * worst point. Reading low here reserves less, so the constants above are
 * the safer place to carry any margin.
 */
static long long doublings_scaled(long long num, long long den)
{
    long long whole = 0;

    if (den <= 0 || num <= den)
        return 0;

    while (num / den >= 2) {
        den *= 2;
        whole += 256;
    }
    return whole + 256 * (num - den) / den;
}

long long gs_catalogue_reserve(const gs_detect_report_t *machine)
{
    long long total;
    long long measured = 0;
    long long modelled;
    long long debt = 0;

    if (machine == NULL)
        return 0;
    total = machine->ram_total_bytes;
    if (total <= 0)
        return 0;

    /* Everything the machine is already holding, which covers the kernel,
     * the desktop and every open program in one number. A platform that
     * will not report free memory leaves this at nought. */
    if (machine->ram_free_bytes > 0 && machine->ram_free_bytes < total)
        measured = total - machine->ram_free_bytes;

    /* Doubling from the smallest machine, so a two gigabyte box carries
     * exactly the reading the constant was measured at.
     *
     * A guest is charged nothing for the person at the keyboard, since
     * the browser and the mail client are running outside the allowance
     * the guest was given and cannot take memory from inside it. What is
     * running inside shows up in the reading instead. */
    if (machine->desktop && !machine->guest)
        debt = DESK_DEBT_BASE
             * doublings_scaled(total + DESK_DEBT_FLOOR_BYTES,
                                DESK_DEBT_FLOOR_BYTES) / 256;

    modelled = (machine->desktop && !machine->guest ? OS_FLOOR_DESKTOP
                                                   : OS_FLOOR_HEADLESS)
             + total * KERNEL_SHARE_NUM / KERNEL_SHARE_DEN
             + debt;

    /* The modelled figure describes a machine in ordinary use, and on a
     * small one it comes to more than the machine holds. Reserving
     * everything would refuse every model while naming the model as the
     * reason, so the modelled figure alone is held to three quarters.
     *
     * A reading taken off the machine is never capped, since a machine
     * really holding fifteen of its sixteen gigabytes has one left, and
     * pretending otherwise would let a model through onto a machine that
     * then swaps. */
    if (modelled > total * RESERVE_CAP_NUM / RESERVE_CAP_DEN)
        modelled = total * RESERVE_CAP_NUM / RESERVE_CAP_DEN;

    if (modelled > measured)
        measured = modelled;
    if (measured > total)
        measured = total;
    return measured;
}

gs_catalogue_room_t gs_catalogue_room(const gs_detect_report_t *machine)
{
    gs_catalogue_room_t room;
    int i;

    memset(&room, 0, sizeof room);
    if (machine == NULL)
        return room;

    room.memory_total = machine->ram_total_bytes;
    room.reserved = gs_catalogue_reserve(machine);
    room.memory_bytes = room.memory_total - room.reserved;
    if (room.memory_bytes < 0)
        room.memory_bytes = 0;

    /* What the screen is already holding comes out of the free reading
     * the card gives, which detect asks for directly. The inference
     * server then keeps a further slice back for itself, refusing to
     * fill the card to the brim however small the model. Both come off
     * before a model is measured against what is left. */
    room.graphics_total = machine->gpu_memory_bytes;
    room.graphics_free = machine->gpu_free_bytes > 0
                       ? machine->gpu_free_bytes
                       : machine->gpu_memory_bytes;
    room.graphics_bytes = room.graphics_free - GRAPHICS_HOLDBACK;
    if (room.graphics_bytes < 0)
        room.graphics_bytes = 0;
    room.graphics_held = room.graphics_free - room.graphics_bytes;
    room.cores = machine->cpu_cores;

    /* Every disk in reach is counted, and the largest single one is kept
     * apart, since one file has to sit on one disk. A disk reported twice
     * under two names would be counted twice, so a repeat of the same
     * size and the same free space is taken as the same disk. */
    for (i = 0; i < machine->disk_count; i++) {
        long long free_here = machine->disk[i].free_bytes;
        int repeat = 0;
        int k;

        for (k = 0; k < i; k++)
            if (machine->disk[k].total_bytes == machine->disk[i].total_bytes &&
                machine->disk[k].free_bytes == free_here) {
                repeat = 1;
                break;
            }
        if (repeat)
            continue;

        room.storage_bytes += free_here;
        if (free_here > room.largest_disk)
            room.largest_disk = free_here;
    }
    return room;
}

int gs_catalogue_share(long long bytes, const gs_detect_report_t *machine,
                       long long *on_card, long long *in_memory)
{
    gs_catalogue_room_t room;
    long long needed;
    long long card;

    if (on_card != NULL)
        *on_card = 0;
    if (in_memory != NULL)
        *in_memory = 0;
    if (machine == NULL || bytes <= 0)
        return GS_ERR_ARG;
    if (gs_catalogue_fit(bytes, machine) != GS_FIT_PARTIAL)
        return GS_ERR;

    room = gs_catalogue_room(machine);
    needed = bytes / HEADROOM_DEN * HEADROOM_NUM;

    /* The card is filled first. Every byte that will not go on it is read
     * out of system memory once for every word produced, so leaving any
     * card memory unused would cost speed for nothing. */
    card = room.graphics_bytes;
    if (card > needed)
        card = needed;

    if (on_card != NULL)
        *on_card = card;
    if (in_memory != NULL)
        *in_memory = needed - card;
    return GS_OK;
}
