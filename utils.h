#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*
 * Prints formated message to stderr and exits.
 * If last char is ':', prints strerror with set errno.
 */
void die(const char *fmt, ...);

/*
 * Prints formated message to stderr and returns.
 * If last char is ':', prints strerror with set errno.
 */
void warn(const char *fmt, ...);

/*
 * Prints formated message to stderr with a timestamp and returns.
 * v: whatever verbose logging is enabled.
 */
void verbose(const bool v, const char *fmt, ...);

/* Calls calloc and exits on failure. */
void *ecalloc(size_t nmemb, size_t size);

/* Calls strdup and exits on failure. */
char *estrdup(const char *s);

/* String equality check - both arguments must be non-NULL */
#define streq(a, b) (strcmp((a), (b)) == 0)

#endif /* UTILS_H */
