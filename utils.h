#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void die(const char *fmt, ...);
void warn(const char *fmt, ...);
void verbose(const unsigned int v, const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
char *estrdup(const char *s);

/* String equality check - both arguments must be non-NULL */
#define streq(a, b) (strcmp((a), (b)) == 0)

#endif /* UTILS_H */
