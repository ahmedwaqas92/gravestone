/* catalogue_test_room.c
 *
 * The half of the catalogue test that asks what room a machine has and
 * which models fit in it. The snapshot and the kind classifier are tested
 * in catalogue_test.c, which holds main and the counters.
 *
 * Split from that file because it went over the length one file is
 * allowed. Both halves are one program, sharing the check routine and the
 * machine builder through catalogue_test.h.
 */
#include "catalogue.h"
#include "catalogue_internal.h"
#include "catalogue_test.h"
#include "gravestone.h"
#include "detect.h"

#include <stdio.h>
#include <string.h>

gs_detect_report_t gs_catalogue_test_desk(long long ram, long long free_ram,
                                          long long vram,
                                          long long free_vram)
{
    gs_detect_report_t m;

    memset(&m, 0, sizeof m);
    m.ram_total_bytes = ram;
    m.ram_free_bytes = free_ram;
    m.gpu_memory_bytes = vram;
    m.gpu_free_bytes = free_vram;
    m.desktop = 1;
    return m;
}


/* What the machine is holding before a model is loaded. The share of
 * memory it takes has to fall as memory grows, since a browser and a mail
 * client are the same size on every machine, and it has to keep climbing,
 * since a person given more memory opens more things. */
void test_the_reserve(void)
{
    gs_detect_report_t headless = machine_of(8 * GB, 0);
    long long last_share = 1000;
    long long last_size = 0;
    int i;
    static const long long sizes[] = { 2, 4, 8, 16, 64, 512 };

    printf("what the machine is already holding\n");

    check(gs_catalogue_reserve(NULL) == 0, "no machine, nothing held");
    check(gs_catalogue_reserve(&headless) > 0,
          "a machine with no screen still holds its kernel");

    for (i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++) {
        gs_detect_report_t desk = desk_of(sizes[i] * GB, 0, 0, 0);
        long long held = gs_catalogue_reserve(&desk);
        long long share = held * 100 / (sizes[i] * GB);

        printf("        %4lld GB machine holds %5lld MB, %lld per cent\n",
               sizes[i], held / (1024 * 1024), share);
        check(held <= sizes[i] * GB, "the reserve never exceeds the machine");
        check(held >= last_size, "and never shrinks as memory grows");
        check(share <= last_share, "while its share of memory falls");
        last_share = share;
        last_size = held;
    }

    /* The modelled figure runs past what a small machine holds, and a
     * reserve of everything would refuse every model while naming the
     * model as the reason. A quarter is always left. */
    {
        gs_detect_report_t small = desk_of(2 * GB, 0, 0, 0);
        gs_catalogue_room_t room = gs_catalogue_room(&small);

        check(room.memory_bytes > 0,
              "a small desk machine keeps something for a model");
        check(room.reserved == 2 * GB / RESERVE_CAP_DEN * RESERVE_CAP_NUM,
              "with the modelled figure held to three quarters");
    }

    /* A reading off a machine is never capped, since one really holding
     * fifteen of its sixteen gigabytes has one left. */
    {
        gs_detect_report_t loaded = desk_of(16 * GB, 1 * GB, 0, 0);

        check(gs_catalogue_reserve(&loaded) == 15 * GB,
              "a reading past three quarters is still believed");
    }

    /* A guest is charged nothing for the person at the keyboard. The
     * browser and the mail client are running outside the allowance the
     * guest was given, so they cannot take memory from inside it. */
    {
        gs_detect_report_t host = desk_of(4 * GB, 0, 0, 0);
        gs_detect_report_t inside = desk_of(4 * GB, 0, 0, 0);

        inside.guest = 1;
        check(gs_catalogue_reserve(&inside) <
              gs_catalogue_reserve(&host),
              "a guest holds less back than the machine around it");
        check(gs_catalogue_room(&inside).memory_bytes >
              gs_catalogue_room(&host).memory_bytes,
              "so it keeps more for a model");
    }

    /* A machine in a rack has no browser and no mail client, so it keeps
     * far more of itself for the model. */
    {
        gs_detect_report_t desk = desk_of(16 * GB, 0, 0, 0);
        gs_detect_report_t rack = machine_of(16 * GB, 0);

        check(gs_catalogue_reserve(&rack) < gs_catalogue_reserve(&desk),
              "a machine with no screen holds less than one with a desk");
    }

    /* A reading taken while the machine is busy beats the modelled figure,
     * so a loaded machine is never told it has room it does not have. */
    {
        gs_detect_report_t busy = desk_of(16 * GB, 1 * GB, 0, 0);
        gs_detect_report_t idle = desk_of(16 * GB, 15 * GB, 0, 0);

        check(gs_catalogue_reserve(&busy) == 15 * GB,
              "a busy machine is taken at its word");
        check(gs_catalogue_reserve(&idle) > 1 * GB,
              "an idle one still carries what a person would open");
    }

    /* Free graphics memory is used where a card reports it, since that
     * reading holds still. */
    {
        gs_detect_report_t part = desk_of(8 * GB, 0, 8 * GB, 3 * GB);
        gs_detect_report_t quiet = desk_of(8 * GB, 0, 8 * GB, 0);

        check(gs_catalogue_room(&part).graphics_free == 3 * GB,
              "a card that reports free memory is believed");
        check(gs_catalogue_room(&quiet).graphics_free == 8 * GB,
              "one that says nothing falls back to its total");
        check(gs_catalogue_room(&part).graphics_total == 8 * GB,
              "the total is kept for showing either way");

        /* The inference server refuses to fill the card to the brim, so
         * a slice of what the card reports free never reaches a model. */
        check(gs_catalogue_room(&part).graphics_bytes ==
              3 * GB - GRAPHICS_HOLDBACK,
              "and the server keeps a slice of it back");
        check(gs_catalogue_room(&part).graphics_held == GRAPHICS_HOLDBACK,
              "which is reported rather than hidden");
        {
            /* A card smaller than the holdback offers a model nothing,
             * rather than a negative amount. */
            gs_detect_report_t small = desk_of(8 * GB, 0, 512 * 1024 * 1024,
                                               512 * 1024 * 1024);

            check(gs_catalogue_room(&small).graphics_bytes == 0,
                  "a card smaller than that slice offers nothing");
        }
    }

    /* Machines with no graphics card at all, which is what this project
     * was written for. Every model then runs on the processor, and the
     * only question is how much memory is left after the reserve. */
    {
        gs_detect_report_t pi = machine_of(1 * GB, 0);
        gs_detect_report_t pi4 = machine_of(4 * GB, 0);
        gs_catalogue_room_t small = gs_catalogue_room(&pi);
        gs_catalogue_room_t bigger = gs_catalogue_room(&pi4);

        check(small.memory_bytes > 0,
              "a one gigabyte board keeps room for a model");
        check(gs_catalogue_fit(small.memory_bytes / HEADROOM_NUM
                               * HEADROOM_DEN, &pi) == GS_FIT_PROCESSOR,
              "and what fits runs on the processor");
        check(!gs_catalogue_conversational(
                  gs_catalogue_fit(268435456LL, &pi)),
              "which never counts as conversational speed");
        check(bigger.memory_bytes > small.memory_bytes * 3,
              "four times the memory leaves more than three times the room");
        check(gs_catalogue_fit(bigger.memory_bytes, &pi4) == GS_FIT_NONE,
              "a model the size of the whole remainder still needs headroom");
    }

    /* On a machine that has a card, every model small enough for system
     * memory is also small enough for the card, so the card answers first
     * and the processor branch is never reached. A count of nought there
     * means no model needs the processor. */
    {
        gs_detect_report_t both = desk_of(8 * GB, 6 * GB, 8 * GB, 8 * GB);
        gs_catalogue_room_t room = gs_catalogue_room(&both);
        long long fills_memory = room.memory_bytes / HEADROOM_NUM
                               * HEADROOM_DEN;
        long long past_memory = room.memory_bytes / HEADROOM_NUM
                              * HEADROOM_DEN + HEADROOM_DEN;

        check(room.graphics_bytes > room.memory_bytes,
              "the card holds more than what is left of memory");
        check(gs_catalogue_fit(fills_memory, &both) == GS_FIT_EITHER,
              "a model filling memory could still run in either place");
        check(gs_catalogue_fit(past_memory, &both) == GS_FIT_GRAPHICS,
              "and one byte past memory can only run on the card");
    }

    /* How a divided model is divided. The card is filled first, since
     * every byte left in memory crosses the bus once for every word. */
    {
        /* Six gigabytes leaves about 5.5 after the reserve, so a model
         * needing more than that and more than the four gigabyte card is
         * divided. The size is derived rather than written down, since a
         * model too far past the card is refused for putting too little
         * of itself on it. */
        gs_detect_report_t m = machine_of(6 * GB, 8 * GB);
        gs_catalogue_room_t room = gs_catalogue_room(&m);
        long long on_card = 0;
        long long in_memory = 0;
        long long model = room.graphics_bytes * SPLIT_FLOOR_DEN
                        / SPLIT_FLOOR_NUM / HEADROOM_NUM * HEADROOM_DEN;
        long long need = model / HEADROOM_DEN * HEADROOM_NUM;

        check(gs_catalogue_fit(model, &m) == GS_FIT_PARTIAL,
              "a model past the card and inside the pair splits");
        check(gs_catalogue_share(model, &m, &on_card, &in_memory) == GS_OK,
              "and the division comes back");
        check(on_card == room.graphics_bytes,
              "the card is filled to the top");
        check(in_memory == need - room.graphics_bytes,
              "and the rest waits in memory");
        check(on_card + in_memory == need,
              "the two add up to the whole, working space counted in");

        /* A model that sits whole in one store has no division. */
        on_card = 99;
        in_memory = 99;
        check(gs_catalogue_share(1 * GB, &m, &on_card, &in_memory) != GS_OK,
              "a model held whole gives no division");
        check(on_card == 0 && in_memory == 0,
              "and leaves both figures at nought");
        check(gs_catalogue_share(500 * GB, &m, NULL, NULL) != GS_OK,
              "nor does one too large for the machine");
        check(gs_catalogue_share(0, &m, NULL, NULL) != GS_OK,
              "nor one of no size");
        check(gs_catalogue_share(model, NULL, NULL, NULL) != GS_OK,
              "nor one on no machine");
    }

    /* A machine with no card puts nothing on a card, so nothing splits. */
    {
        gs_detect_report_t pi = machine_of(4 * GB, 0);

        check(gs_catalogue_share(1 * GB, &pi, NULL, NULL) != GS_OK,
              "a machine with no card never divides a model");
    }

    /* A model with too little of itself on the card is left out of the
     * list altogether. It would load and then answer a word a minute,
     * which is worse than being told it will not run. */
    {
        /* Memory has to be smaller than the model needs, or the model
         * sits whole in memory and never divides. Four gigabytes leaves
         * about 3.85 after the reserve, against a card offering 3 once
         * the server has kept its slice. */
        gs_detect_report_t m = machine_of(4 * GB, 4 * GB);
        gs_catalogue_room_t room = gs_catalogue_room(&m);
        long long card = room.graphics_bytes;
        /* At the floor exactly, the card carries seven tenths of what the
         * model needs. */
        long long at_floor = card * SPLIT_FLOOR_DEN / SPLIT_FLOOR_NUM
                           / HEADROOM_NUM * HEADROOM_DEN;
        long long on_card = 0;
        long long in_memory = 0;

        check(gs_catalogue_fit(at_floor, &m) == GS_FIT_PARTIAL,
              "a model sitting at the floor is divided");
        check(gs_catalogue_listed(at_floor, &m),
              "and it belongs in the list a model is fetched from");
        check(gs_catalogue_share(at_floor, &m, &on_card, &in_memory)
              == GS_OK, "and its division comes back");
        /* A failed division above leaves both at nought, and dividing by
         * their sum would take the whole run down with it, so the guard
         * stands in front of the check rather than behind it. */
        check(on_card + in_memory > 0 &&
              on_card * 100 / (on_card + in_memory) >= 70,
              "with seven tenths or more of it on the card");

        /* Half again as large puts under half of itself on the card, so
         * it is left out of the list. It still runs, however slowly,
         * which is why the verdict and the listing are asked apart. */
        check(!gs_catalogue_listed(at_floor * 3 / 2, &m),
              "one half again as large is left out of the list");
        check(gs_catalogue_fit(at_floor * 3 / 2, &m) == GS_FIT_PARTIAL,
              "while still running, divided, for anybody who has it");

        /* The line is exact to the byte. Dividing the left of the test
         * before comparing would throw away up to nine bytes, which is
         * enough to admit a model a hair under the floor and report it
         * back as sixty nine per cent. */
        {
            long long b;
            long long largest = 0;
            long long worst = 100;

            for (b = at_floor - 200; b <= at_floor + 200; b++) {
                long long here = 0;
                long long there = 0;
                long long share;

                if (!gs_catalogue_listed(b, &m))
                    continue;
                if (gs_catalogue_share(b, &m, &here, &there) != GS_OK)
                    continue;
                if (here + there <= 0)
                    continue;
                share = here * 100 / (here + there);
                if (share < worst)
                    worst = share;
                largest = b;
            }
            check(worst >= 70,
                  "nothing under seven tenths reaches the list");
            check(largest > 0 && !gs_catalogue_listed(largest + 1, &m),
                  "and one byte past the largest listed is left out");
        }
    }

    /* The three answers a person asked for, one machine each. */
    {
        gs_detect_report_t pi = machine_of(8 * GB, 0);          /* no card */
        gs_detect_report_t roomy = machine_of(8 * GB, 8 * GB);
        gs_detect_report_t tight = machine_of(2 * GB, 16 * GB);
        long long model = 1 * GB;

        check(gs_catalogue_fit(model, &pi) == GS_FIT_PROCESSOR,
              "no card at all, so the processor is the only answer");
        check(gs_catalogue_fit(model, &roomy) == GS_FIT_EITHER,
              "a card and room in memory, so both are open");
        /* Two gigabytes needs 2.4 with the headroom, which is past the
         * 1.79 the machine has left and well inside the card. */
        check(gs_catalogue_room(&tight).memory_bytes < 2 * GB,
              "memory has under two gigabytes left after the reserve");
        check(gs_catalogue_fit(2 * GB, &tight) == GS_FIT_GRAPHICS,
              "a card larger than what memory has left, so only the card");
        check(strcmp(gs_catalogue_fit_name(GS_FIT_EITHER),
                     "card or processor") == 0,
              "and each of the three has its own name");
        check(strcmp(gs_catalogue_fit_name(GS_FIT_PROCESSOR),
                     "processor") == 0, "the processor one");
        check(strcmp(gs_catalogue_fit_name(GS_FIT_GRAPHICS), "card") == 0,
              "and the card one");

        /* The short form for a column too narrow for a phrase. */
        check(strcmp(gs_catalogue_fit_tag(GS_FIT_EITHER), "GPU+CPU") == 0,
              "both places, in a few letters");
        check(strcmp(gs_catalogue_fit_tag(GS_FIT_GRAPHICS), "GPU") == 0,
              "the card alone");
        check(strcmp(gs_catalogue_fit_tag(GS_FIT_PROCESSOR), "CPU") == 0,
              "the processor alone");
        check(strcmp(gs_catalogue_fit_tag(GS_FIT_PARTIAL), "SPLIT") == 0,
              "and shared between the two");
        {
            int f;

            for (f = 0; f <= (int)GS_FIT_EITHER; f++)
                check(strlen(gs_catalogue_fit_tag((gs_catalogue_fit_t)f))
                      <= 7, "every tag stays inside seven characters");
        }
    }

    /* The case this was built for. glm4 at 6.7 GB against this laptop,
     * where 12 of its 41 layers reached the card and the run produced
     * nothing in 300 seconds. */
    {
        gs_detect_report_t laptop = desk_of(3932160000LL, 2335000000LL,
                                            4294967296LL, 4157000000LL);

        gs_catalogue_room_t room = gs_catalogue_room(&laptop);
        long long largest = room.graphics_bytes / HEADROOM_NUM
                          * HEADROOM_DEN;

        check(gs_catalogue_fit(6700000000LL, &laptop) == GS_FIT_NONE,
              "glm4 is refused on the machine it crawled on");
        check(gs_catalogue_fit(largest, &laptop) == GS_FIT_GRAPHICS,
              "the largest model the card can hold lands on it");
        check(gs_catalogue_conversational(
                  gs_catalogue_fit(largest, &laptop)),
              "and that one answers at conversational speed");
        /* The slice the server keeps back is what pulled this down. A
         * model sized against the whole free card no longer fits it. */
        check(gs_catalogue_fit(room.graphics_free / HEADROOM_NUM
                               * HEADROOM_DEN, &laptop) != GS_FIT_GRAPHICS,
              "one sized against the whole free card does not");
        printf("        laptop: card reports %lld free, offers %lld, so "
               "the largest on it is %lld bytes\n",
               room.graphics_free, room.graphics_bytes, largest);
    }
}

void test_fit_arithmetic(void)
{
    gs_detect_report_t none = machine_of(0, 0);
    gs_detect_report_t cpu_only = machine_of(8000000000LL, 0);
    gs_detect_report_t card = machine_of(4000000000LL, 6000000000LL);

    printf("the fit rule\n");

    /* needed = bytes / 5 * 6, with the division truncating first. */
    check(gs_catalogue_fit(5, &cpu_only) == GS_FIT_PROCESSOR,
          "5 bytes needs 6 and fits in system memory");
    check(gs_catalogue_fit(9, &none) == GS_FIT_NONE,
          "nothing fits on a machine with no memory at all");
    check(gs_catalogue_fit(0, &cpu_only) == GS_FIT_NONE,
          "a zero sized file never fits");
    check(gs_catalogue_fit(-1, &cpu_only) == GS_FIT_NONE,
          "a negative size never fits");
    check(gs_catalogue_fit(1000, NULL) == GS_FIT_NONE,
          "a null machine never fits");

    /* The limit is what is left after the reserve, so the figure is taken
     * from the room rather than written down here. Writing it down would
     * pin the test to today's constants instead of to the rule. */
    {
        gs_catalogue_room_t room = gs_catalogue_room(&cpu_only);
        long long biggest = room.memory_bytes / HEADROOM_NUM * HEADROOM_DEN;

        check(room.memory_bytes < room.memory_total,
              "the reserve takes a bite out of system memory");
        check(gs_catalogue_fit(biggest, &cpu_only) == GS_FIT_PROCESSOR,
              "the largest model that fits in what is left");
        check(gs_catalogue_fit(room.memory_bytes + 1, &cpu_only)
              == GS_FIT_NONE, "and one over the whole remainder does not");
    }

    /* A small model on a machine with a card and room to spare could run
     * in either, and the answer names both rather than picking one. */
    check(gs_catalogue_fit(1000000000LL, &card) == GS_FIT_EITHER,
          "a small model could run on the card or on the processor");
    check(gs_catalogue_conversational(gs_catalogue_fit(1000000000LL, &card)),
          "and it converses, since the card alone would hold it");
    {
        gs_catalogue_room_t room = gs_catalogue_room(&card);
        /* The need is worked out as bytes / 5 * 6, and that division
         * truncates, so a single byte more can still come to the same
         * need. Stepping by the divisor moves the quotient for certain. */
        long long over_card = room.graphics_bytes / HEADROOM_NUM
                            * HEADROOM_DEN + HEADROOM_DEN;

        check(gs_catalogue_fit(over_card, &card) == GS_FIT_PARTIAL,
              "a model larger than the card alone splits across both");
        check(gs_catalogue_fit(room.graphics_bytes / HEADROOM_NUM
                               * HEADROOM_DEN, &card) == GS_FIT_GRAPHICS,
              "the largest that still fits the card stays on it");
        check(gs_catalogue_fit(room.memory_bytes + room.graphics_bytes + 1,
                               &card) == GS_FIT_NONE,
              "a model larger than both together does not fit");
    }

    /* Only a model held entirely on the card answers at conversational
     * speed, since anything left in system memory crosses the bus once
     * for every word produced. */
    check(gs_catalogue_conversational(GS_FIT_GRAPHICS),
          "a model on the card converses");
    check(!gs_catalogue_conversational(GS_FIT_PARTIAL),
          "a split model does not");
    check(!gs_catalogue_conversational(GS_FIT_PROCESSOR),
          "nor one on the processor");
    check(!gs_catalogue_conversational(GS_FIT_NONE), "nor one that will not load");

    check(gs_catalogue_fit_name(GS_FIT_NONE) != NULL, "every fit has a name");
    check(gs_catalogue_fit_name(GS_FIT_GRAPHICS) != NULL, "graphics has one");
}

/* Keeping a model and running one are separate questions. A model already
 * on the disk needs no free space to run, and asking for free space it
 * does not need would refuse a model that works. */
void test_keeping_against_running(void)
{
    gs_detect_report_t m;

    printf("room to keep a model, against room to run one\n");

    memset(&m, 0, sizeof m);
    m.ram_total_bytes = 32 * GB;
    m.gpu_memory_bytes = 8 * GB;
    m.gpu_free_bytes = 8 * GB;
    m.disk_count = 1;
    m.disk[0].total_bytes = 500 * GB;
    m.disk[0].free_bytes = 1 * GB;      /* nearly full */

    check(gs_catalogue_fit(4 * GB, &m) == GS_FIT_EITHER,
          "a model already here runs on a nearly full disk");
    check(!gs_catalogue_storable(4 * GB, &m),
          "while there is no room to fetch another copy of it");
    check(gs_catalogue_storable(1 * GB / 2, &m),
          "something small enough would still fit");

    m.disk[0].free_bytes = 400 * GB;
    check(gs_catalogue_storable(4 * GB, &m), "and room makes it storable");

    m.disk_count = 0;
    check(gs_catalogue_storable(4 * GB, &m),
          "a machine reporting no disks is not held to the question");
    check(!gs_catalogue_storable(0, &m), "nothing of no size is storable");
    check(!gs_catalogue_storable(4 * GB, NULL), "nor on no machine");
}

/* Several models held at once, which is what the box shows while a person
 * ticks. Each one carries its own working space, since they would be
 * loaded together. */
void test_many_at_once(void)
{
    gs_detect_report_t m = desk_of(32 * GB, 30 * GB, 8 * GB, 8 * GB);
    long long sizes[4];
    long long one;

    printf("what a set of models would take together\n");

    check(gs_catalogue_needed(NULL, 3) == 0, "no list needs nothing");
    check(gs_catalogue_needed(sizes, 0) == 0, "an empty list too");
    check(gs_catalogue_needed(sizes, -1) == 0, "and a negative count");

    sizes[0] = 5 * GB;
    check(gs_catalogue_needed(sizes, 1) == 5 * GB / HEADROOM_DEN
                                           * HEADROOM_NUM,
          "one model needs its own size with the headroom on top");

    sizes[1] = 5 * GB;
    check(gs_catalogue_needed(sizes, 2) == 2 * (5 * GB / HEADROOM_DEN
                                                * HEADROOM_NUM),
          "two of them need twice that, since both would be held");

    check(gs_catalogue_fit_many(NULL, 2, &m) == 0, "no list, none fit");
    check(gs_catalogue_fit_many(sizes, 2, NULL) == 0, "no machine, none fit");
    check(gs_catalogue_fit_many(sizes, 0, &m) == 0, "an empty list too");

    /* The pool is what is left after the reserve, plus the card. */
    {
        gs_catalogue_room_t room = gs_catalogue_room(&m);

        one = (room.memory_bytes + room.graphics_bytes) / HEADROOM_NUM
              * HEADROOM_DEN;
    }

    sizes[0] = one;
    check(gs_catalogue_fit_many(sizes, 1, &m) == 1,
          "one model filling the pool fits on its own");
    sizes[1] = one;
    check(gs_catalogue_fit_many(sizes, 2, &m) == 1,
          "and a second beside it does not, so the count stops at one");

    sizes[0] = 0;
    check(gs_catalogue_fit_many(sizes, 2, &m) == 0,
          "a size of nothing stops the count where it stands");
}
