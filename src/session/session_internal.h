/* session_internal.h
 *
 * Shared between the conversation store and the reading side of it.
 * Nothing outside src/session/ may include this.
 */
#ifndef GS_SESSION_INTERNAL_H
#define GS_SESSION_INTERNAL_H

#include "db.h"

/* The one handle every call here works through. Zero while closed. */
extern gs_db_t *gs_session_db;

/* Seconds since the epoch, which is what every stored time holds. */
long long gs_session_now(void);

#endif
