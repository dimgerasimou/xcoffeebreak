#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>
#include <stdlib.h>

void die(const char *fmt, ...);
void warn(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
char *estrdup(const char *s);

#endif /* UTILS_H */
