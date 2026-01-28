#ifndef ARGS_H
#define ARGS_H

#include <stdbool.h>

typedef struct {
	unsigned long  lock_s;
	unsigned long  off_s;
	unsigned long  suspend_s;
	unsigned long  poll_ms;
	bool           verbose;
	bool           dry_run;
	char          *lock_cmd;
	char          *off_cmd;
	char          *suspend_cmd;
} Options;

int args_set(Options *o, const int argc, char *argv[]);
void args_free(Options *o);

#endif
