#ifndef MPRIS_H
#define MPRIS_H

#include <stdbool.h>

typedef struct Mpris Mpris;

/**
 * Open an MPRIS/DBus monitor.
 *
 * On success, *out is set to a valid handle that must be closed with
 * mpris_close(). On failure, *out is set to NULL.
 *
 * verbose: non-zero enables logging via utils.h helpers.
 */
Mpris* mpris_init(bool verbose);

/** Close and free an MPRIS handle (safe to call with NULL). */
void mpris_cleanup(Mpris *m);

/**
 * Poll DBus for MPRIS activity.
 *
 * Returns 0 on success, -1 if the DBus connection is lost (the handle
 * becomes unusable; caller should close it and continue without inhibit).
 */
int  mpris_poll(Mpris *m, unsigned int timeout_ms);

/** True if any tracked player is in PlaybackStatus == "Playing". */
bool mpris_is_playing(const Mpris *m);

#endif /* MPRIS_H */
