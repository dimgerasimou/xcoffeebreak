#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "utils.h"

void
warn(const char *fmt, ...)
{
	va_list ap;
	int saved_errno = errno;

	fputs("xcoffeebreak: ", stderr);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s", strerror(saved_errno));
	fputc('\n', stderr);
}

void
die(const char *fmt, ...)
{
	va_list ap;
	int saved_errno = errno;

	fputs("xcoffeebreak: ", stderr);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s", strerror(saved_errno));
	fputc('\n', stderr);


	exit(1);
}

void
verbose(const unsigned int v, const char *fmt, ...)
{
	va_list ap;

	if (!v)
		return;

	fputs("xcoffeebreak: [VERBOSE]", stderr);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fputc('\n', stderr);
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size)))
		die("calloc:");
	return p;
}

char *
estrdup(const char *s)
{
	char *p;

	if (!(p = strdup(s)))
		die("strdup:");
	return p;
}
