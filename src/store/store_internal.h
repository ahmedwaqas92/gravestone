/* Shared between the .c files inside src/store/ only. */
#ifndef GS_STORE_INTERNAL_H
#define GS_STORE_INTERNAL_H

#include "db.h"

/* The tables, and the steps that bring an older file up to date. Kept
 * apart from the queries so neither file grows past what fits in a head. */
extern const char *gs_store_schema;

int gs_store_migrate(gs_db_t *handle, int *cleared);

#endif
