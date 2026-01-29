/* See LICENSE file for copyright and license details. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "utils.h"

void
die(const char *fmt, ...)
{
	va_list ap;
	int saved_errno = errno;

	fputs("xcoffeebreak: [FATAL] ", stderr);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s", strerror(saved_errno));
	fputc('\n', stderr);


	exit(1);
}

void
warn(const char *fmt, ...)
{
	va_list ap;
	int saved_errno = errno;

	fputs("xcoffeebreak: [WARN] ", stderr);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s", strerror(saved_errno));
	fputc('\n', stderr);
}

void
verbose(const bool v, const char *fmt, ...)
{
	struct tm tm;
	va_list ap;
	time_t now;

	if (!v)
		return;

	now = time(NULL);
	if (now == (time_t)-1 || !localtime_r(&now, &tm))
		die("failed to populate struct tm:");

	fprintf(stderr, "xcoffeebreak: [VERBOSE] [%04d-%02d-%02d %02d:%02d:%02d] ",
	        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	        tm.tm_hour, tm.tm_min, tm.tm_sec);

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
