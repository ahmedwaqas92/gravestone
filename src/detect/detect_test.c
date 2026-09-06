#include "detect.h"
#include "gravestone.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

static void check(int c, const char *what)
{
    checks++;
    if (c) printf("  ok    %s\n", what);
    else { printf("  FAIL  %s\n", what); failures++; }
}

int main(void)
{
    gs_detect_report_t a, b, c;
    char pa[17], pb[17], pc[17];
    char text[2048];

    gs_log_set_level(GS_LOG_ERROR);

    printf("bad arguments\n");
    check(gs_detect_read(NULL) == GS_ERR_ARG, "null report refused");
    check(gs_detect_fingerprint(NULL, pa, sizeof pa) == GS_ERR_ARG,
          "null report refused by the fingerprint");
    check(gs_detect_fingerprint(&a, NULL, 17) == GS_ERR_ARG,
          "null buffer refused");
    check(gs_detect_fingerprint(&a, pa, 4) == GS_ERR_ARG,
          "a buffer too small for the answer is refused");
    check(gs_detect_describe(NULL, text, sizeof text) == GS_ERR_ARG,
          "null report refused by describe");

    printf("reading this machine\n");
    check(gs_detect_read(&a) == GS_OK, "the machine answered");
    printf("        %s %s, %d cores, %lld bytes of memory\n",
           a.os, a.arch, a.cpu_cores, a.ram_total_bytes);
    check(a.os[0] != '\0', "an operating system name came back");
    check(a.arch[0] != '\0', "an architecture came back");
    check(a.cpu_cores > 0, "at least one processor was counted");
    check(a.ram_total_bytes > 0, "memory was measured");
    check(a.disk_total_bytes > 0, "the disk was measured");
    check(a.disk_free_bytes >= 0, "free space is not negative");
    check(a.disk_free_bytes <= a.disk_total_bytes,
          "free space fits inside the disk");
    check(a.gpu[0] != '\0', "the graphics field is filled in, even as unknown");
    check(a.gpu_memory_bytes >= 0, "graphics memory is not negative");
    printf("        graphics: %s, %lld bytes on the card\n",
           a.gpu, a.gpu_memory_bytes);

    printf("every disk, rather than one\n");
    printf("        %d disks found\n", a.disk_count);
    check(a.disk_count >= 1, "at least one disk was found");
    check(a.disk_count <= GS_DETECT_MAX_DISKS, "the list stayed in bounds");

    {
        long long summed_total = 0, summed_free = 0;
        int i, all_named = 1, all_sized = 1, no_escapes = 1, unique = 1;

        for (i = 0; i < a.disk_count; i++) {
            int j;

            printf("        %-14s %-12s %-6s %lld bytes\n",
                   a.disk[i].mount, a.disk[i].device, a.disk[i].fs,
                   a.disk[i].total_bytes);
            summed_total += a.disk[i].total_bytes;
            summed_free += a.disk[i].free_bytes;
            if (a.disk[i].mount[0] == '\0' || a.disk[i].fs[0] == '\0' ||
                a.disk[i].device[0] == '\0')
                all_named = 0;
            if (a.disk[i].total_bytes <= 0 || a.disk[i].free_bytes < 0 ||
                a.disk[i].free_bytes > a.disk[i].total_bytes)
                all_sized = 0;
            /* The mount table escapes a backslash as \134, which must be
             * decoded before anything shows it to a person. */
            if (strstr(a.disk[i].mount, "\\1") != NULL ||
                strstr(a.disk[i].device, "\\1") != NULL)
                no_escapes = 0;
            for (j = 0; j < i; j++)
                if (strcmp(a.disk[i].device, a.disk[j].device) == 0)
                    unique = 0;
        }
        check(all_named, "every disk carries a mount point, device and type");
        check(all_sized, "every disk has a sane size and free figure");
        check(no_escapes, "octal escapes were decoded out of the names");
        check(unique, "one drive mounted twice appears once");
        check(summed_total == a.disk_total_bytes,
              "the total is the sum of the disks");
        check(summed_free == a.disk_free_bytes,
              "the free figure is the sum of the disks");
    }

    printf("fingerprints\n");
    check(gs_detect_read(&b) == GS_OK, "read a second time");
    check(gs_detect_fingerprint(&a, pa, sizeof pa) == GS_OK, "first hashed");
    check(gs_detect_fingerprint(&b, pb, sizeof pb) == GS_OK, "second hashed");
    printf("        %s\n", pa);
    check(strlen(pa) == 16, "sixteen characters long");
    check(strspn(pa, "0123456789abcdef") == 16, "all of them hexadecimal");
    check(strcmp(pa, pb) == 0,
          "two readings of one machine give the same fingerprint");

    /* Free space moves constantly, so it has to stay out of the hash. */
    c = a;
    c.disk_free_bytes = a.disk_free_bytes / 2;
    gs_detect_fingerprint(&c, pc, sizeof pc);
    check(strcmp(pa, pc) == 0, "free space does not change the fingerprint");

    c = a;
    c.ram_total_bytes = a.ram_total_bytes * 2;
    gs_detect_fingerprint(&c, pc, sizeof pc);
    check(strcmp(pa, pc) != 0, "doubling the memory does change it");

    c = a;
    c.cpu_cores = a.cpu_cores + 1;
    gs_detect_fingerprint(&c, pc, sizeof pc);
    check(strcmp(pa, pc) != 0, "one more core changes it");

    c = a;
    strcpy(c.gpu, "some other card");
    gs_detect_fingerprint(&c, pc, sizeof pc);
    check(strcmp(pa, pc) != 0, "a different graphics card changes it");

    c = a;
    c.gpu_memory_bytes = a.gpu_memory_bytes + 1073741824LL;
    gs_detect_fingerprint(&c, pc, sizeof pc);
    check(strcmp(pa, pc) != 0, "more memory on the card changes it");

    if (a.disk_count > 0) {
        c = a;
        c.disk[0].free_bytes = a.disk[0].free_bytes / 2;
        gs_detect_fingerprint(&c, pc, sizeof pc);
        check(strcmp(pa, pc) == 0,
              "free space on a disk does not change it");

        c = a;
        c.disk[0].total_bytes = a.disk[0].total_bytes * 2;
        gs_detect_fingerprint(&c, pc, sizeof pc);
        check(strcmp(pa, pc) != 0, "a larger disk does change it");

        c = a;
        c.disk_count = a.disk_count - 1;
        gs_detect_fingerprint(&c, pc, sizeof pc);
        check(strcmp(pa, pc) != 0, "removing a disk changes it");
    }

    printf("describing\n");
    check(gs_detect_describe(&a, text, sizeof text) > 0, "describe produced text");
    check(strstr(text, "cores\t") != NULL, "the core count is in there");
    check(strstr(text, "ram\t") != NULL, "the memory is in there");
    check(strstr(text, "gpu_memory\t") != NULL,
          "the graphics memory is in there");
    check(strstr(text, "disk\t") != NULL, "each disk is listed");
    {
        const char *scan = text;
        int lines = 0;

        while ((scan = strstr(scan, "\ndisk\t")) != NULL) {
            lines++;
            scan += 2;
        }
        check(lines == a.disk_count,
              "one line per disk, matching the count");
    }
    check(gs_detect_describe(&a, text, 8) > 0,
          "a short buffer still reports the length it wanted");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
