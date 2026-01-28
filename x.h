#ifndef X_H
#define X_H

typedef struct X11 X11;

/*
 * Initalize X11 tools needed.
 *
 * Returns an initialized structure on success,
 * exits on failiure.
 */
X11* x11_init(void);

/* Close and free X11 structure (safe to call with NULL). */
void x11_cleanup(X11 *x);

/*
 * Gets idle time reported from XScreenSaverQueryInfo().
 *
 * Returns idle time in ms, exits on failure.
 */
unsigned long x11_idle_ms(X11 *x);

#endif /* X_H */
