/* See LICENSE file for copyright and license details. */

#ifndef XCOFFEEBREAK_MPRIS_H
#define XCOFFEEBREAK_MPRIS_H

#include <stdbool.h>

typedef struct Mpris Mpris;

/*
 * Initalize an MPRIS/DBus monitor.
 *
 * verbose: enable logging.
 *
 * Returns an initialized structure on success,
 * NULL on failiure.
 */
Mpris *mpris_init(bool verbose);

/* Close and free an MPRIS handle (safe to call with NULL). */
void mpris_cleanup(Mpris *m);

/*
 * Poll DBus for MPRIS activity.
 *
 * Returns 0 on success, -1 if the DBus connection is lost (the handle
 * becomes unusable; caller should close it and continue without inhibit).
 */
int mpris_poll(Mpris *m, unsigned int timeout_ms);

/* True if any tracked player is in PlaybackStatus == "Playing". */
bool mpris_is_playing(const Mpris *m);

#endif /* XCOFFEEBREAK_MPRIS_H */
