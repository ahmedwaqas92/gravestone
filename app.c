/* app.c
 *
 * The only file that runs the application. It holds main, the list of
 * modules, and the dispatch that picks one. No feature logic lives here.
 *
 * C runs nothing at include time, so a module becomes reachable only by
 * appearing in the array below. Adding one costs an entry here and a line
 * in the Makefile.
 */
#include "gravestone.h"
#include "log.h"
#include "catalogue.h"
#include "detect.h"
#include "library.h"
#include "harness.h"
#include "provider.h"
#include "session.h"
#include "verify.h"
#include "mount.h"
#include "store.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

static const gs_module *const modules[] = {
    &gs_ui_module,
    &gs_detect_module,
    &gs_mount_module,
    &gs_store_module,
    &gs_catalogue_module,
    &gs_library_module,
    &gs_session_module,
    &gs_provider_module,
    &gs_verify_module,
    &gs_harness_module,
};

static const int module_count = (int)(sizeof modules / sizeof modules[0]);

static void usage(const char *program)
{
    int i;

    fprintf(stderr, "usage: %s [command]\n\n", program);
    fprintf(stderr, "commands:\n");
    for (i = 0; i < module_count; i++)
        fprintf(stderr, "  %-10s %s\n", modules[i]->name,
                modules[i]->summary);
    fprintf(stderr, "\nrunning with no command opens the window.\n");
}

static const gs_module *find(const char *name)
{
    int i;

    for (i = 0; i < module_count; i++)
        if (strcmp(modules[i]->name, name) == 0)
            return modules[i];
    return NULL;
}

int main(int argc, char **argv)
{
    const gs_module *module;
    int rc;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 ||
                     strcmp(argv[1], "--help") == 0)) {
        usage(argv[0]);
        return 0;
    }

    module = argc > 1 ? find(argv[1]) : &gs_ui_module;

    if (module == NULL) {
        fprintf(stderr, "%s: no command called '%s'\n\n", argv[0], argv[1]);
        usage(argv[0]);
        return 2;
    }

    if (module->init != NULL) {
        rc = module->init();
        if (rc != GS_OK) {
            gs_log_error("app: %s failed to start", module->name);
            return 1;
        }
    }

    rc = module->run(argc > 1 ? argc - 1 : 1, argc > 1 ? argv + 1 : argv);

    if (module->shutdown != NULL)
        module->shutdown();

    return rc == GS_OK ? 0 : 1;
}
