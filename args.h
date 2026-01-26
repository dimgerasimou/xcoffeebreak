#ifndef ARGS_H
#define ARGS_H

#include <stdint.h>

typedef struct {
	uint32_t  lock_s;
	uint32_t  off_s;
	uint32_t  suspend_s;
	uint32_t  poll_ms;
	char     *lock_cmd;
	char     *off_cmd;
	char     *suspend_cmd;
} Options;

int args_set(Options *o, const int argc, char *argv[]);
void args_free(Options *o);

#endif
