/* gravestone.h
 *
 * Umbrella header. Every translation unit includes this after its own
 * header. It holds the module registry type and the shared return codes,
 * and nothing else. Feature specific declarations belong in that feature's
 * own header.
 */
#ifndef GRAVESTONE_H
#define GRAVESTONE_H

#define GS_OK        0
#define GS_ERR      (-1)
#define GS_ERR_ARG  (-2)
#define GS_ERR_MEM  (-3)
#define GS_ERR_IO   (-4)

/* A module is one directory under src/. It fills in this struct, declares
 * the instance in its own header, and app.c holds an array of pointers to
 * them. C runs no code at include time, so the array in app.c is the only
 * thing that makes a module reachable. */
typedef struct {
    const char *name;
    const char *summary;
    int  (*init)(void);
    int  (*run)(int argc, char **argv);
    void (*shutdown)(void);
} gs_module;

#endif /* GRAVESTONE_H */
