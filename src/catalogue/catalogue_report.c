/* catalogue_report.c
 *
 * The command line side of this module. It prints what the machine has
 * room for, how the list divides between the card and system memory, and
 * the largest model that would still answer at conversational speed.
 *
 * Split from catalogue.c because that file went over the length one file
 * is allowed. The two share through catalogue_internal.h.
 */
#include "catalogue.h"
#include "catalogue_internal.h"
#include "gravestone.h"
#include "detect.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int catalogue_init(void)
{
    return GS_OK;
}

static int catalogue_run(int argc, char **argv)
{
    gs_detect_report_t machine;
    int *fits;
    int n, i, shown = 0;

    if (gs_catalogue_load() == 0) {
        gs_log_error("catalogue: nothing to show");
        return GS_ERR;
    }
    if (gs_detect_read(&machine) != GS_OK) {
        gs_catalogue_release();
        return GS_ERR;
    }

    fits = calloc(GS_CATALOGUE_MAX, sizeof *fits);
    if (fits == NULL) {
        gs_catalogue_release();
        return GS_ERR_MEM;
    }

    /* "catalogue audit" prints every row with its category, runnable or
     * not, so the classification can be read end to end. */
    if (argc > 1 && strcmp(argv[1], "audit") == 0) {
        for (i = 0; i < gs_catalogue_count(); i++) {
            const gs_catalogue_entry_t *e = gs_catalogue_at(i);

            printf("%s\t%s\t%s\n",
                   gs_catalogue_kind_name(gs_catalogue_kind(e)),
                   e->repository, e->file);
        }
        free(fits);
        gs_catalogue_release();
        return GS_OK;
    }

    n = gs_catalogue_runnable(&machine, fits, GS_CATALOGUE_MAX);
    printf("source\t%s\n", gs_catalogue_source());
    printf("known\t%d\n", gs_catalogue_count());
    printf("runnable\t%d\n", n);

    /* What the machine has left after the kernel, the desktop and the
     * open programs, since that is what decides the list above. */
    {
        gs_catalogue_room_t room = gs_catalogue_room(&machine);

        printf("memory\t%lld\ttotal\t%lld\treserved\t%lld\n",
               room.memory_bytes, room.memory_total, room.reserved);
        printf("graphics\t%lld\tfree\t%lld\ttotal\t%lld\theld\t%lld\n",
               room.graphics_bytes, room.graphics_free,
               room.graphics_total, room.graphics_held);
    }

    /* The largest model that answers at conversational speed, which is
     * the one a person actually wants. Everything above it loads and
     * crawls, so it is named on its own rather than left in the list. */
    {
        int quick = 0;
        int named = 0;

        for (i = 0; i < n; i++) {
            const gs_catalogue_entry_t *e = gs_catalogue_at(fits[i]);

            if (!gs_catalogue_conversational(
                    gs_catalogue_fit(e->bytes, &machine)))
                continue;
            quick++;
            if (named < 5) {
                printf("card\t%s\t%lld\t%s\t%s\n",
                       gs_catalogue_kind_name(gs_catalogue_kind(e)),
                       e->bytes, e->quantisation, e->file);
                named++;
            }
        }
        printf("conversational\t%d\n", quick);
    }

    /* One line per place a model could run, so the list says what the
     * machine offers rather than only how many rows survived. */
    {
        int per_fit[GS_FIT_EITHER + 1] = {0};
        int f;

        for (i = 0; i < n; i++)
            per_fit[gs_catalogue_fit(gs_catalogue_at(fits[i])->bytes,
                                     &machine)]++;
        for (f = 0; f <= (int)GS_FIT_EITHER; f++)
            printf("where\t%s\t%d\n",
                   gs_catalogue_fit_name((gs_catalogue_fit_t)f), per_fit[f]);
    }

    /* What the processor would carry on its own. Every model small enough
     * for system memory is also small enough for the card, so the card
     * claims all of them first and the processor count reads nought. A
     * machine with no card at all is the case this project was written
     * for, and this line says what it would do. */
    {
        gs_detect_report_t cardless = machine;
        int alone = 0;
        long long biggest = 0;

        cardless.gpu_memory_bytes = 0;
        cardless.gpu_free_bytes = 0;
        for (i = 0; i < gs_catalogue_count(); i++) {
            const gs_catalogue_entry_t *e = gs_catalogue_at(i);

            if (gs_catalogue_fit(e->bytes, &cardless) == GS_FIT_NONE)
                continue;
            if (!gs_catalogue_storable(e->bytes, &cardless))
                continue;
            alone++;
            if (e->bytes > biggest)
                biggest = e->bytes;
        }
        printf("processor alone\t%d\tlargest\t%lld\n", alone, biggest);
    }

    {
        int per_kind[GS_KIND_COUNT] = {0};
        int k;

        for (i = 0; i < n; i++)
            per_kind[gs_catalogue_kind(gs_catalogue_at(fits[i]))]++;
        for (k = 0; k < (int)GS_KIND_COUNT; k++)
            printf("kind\t%s\t%d\n",
                   gs_catalogue_kind_name((gs_catalogue_kind_t)k),
                   per_kind[k]);
    }

    for (i = 0; i < n && shown < 20; i++, shown++) {
        const gs_catalogue_entry_t *e = gs_catalogue_at(fits[i]);
        gs_catalogue_fit_t fit = gs_catalogue_fit(e->bytes, &machine);
        long long on_card = 0;
        long long in_memory = 0;

        printf("%s\t%s\t%lld\t%s\t%s",
               gs_catalogue_fit_name(fit),
               gs_catalogue_kind_name(gs_catalogue_kind(e)),
               e->bytes, e->quantisation, e->file);
        /* A divided model carries how it would be divided, since the part
         * left in system memory is read across the bus once for every
         * word and decides how slowly the answer arrives. */
        if (gs_catalogue_share(e->bytes, &machine, &on_card,
                               &in_memory) == GS_OK)
            printf("\tcard\t%lld\tmemory\t%lld", on_card, in_memory);
        printf("\n");
    }

    free(fits);
    gs_catalogue_release();
    return GS_OK;
}

static void catalogue_shutdown(void)
{
    gs_catalogue_release();
}

const gs_module gs_catalogue_module = {
    "catalogue",
    "list the models that could run on this machine",
    catalogue_init,
    catalogue_run,
    catalogue_shutdown
};
