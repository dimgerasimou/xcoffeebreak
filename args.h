#ifndef XCOFFEBREAK_ARGS_H
#define XCOFFEBREAK_ARGS_H

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

/*
 * Initalizes options with defaults, sets the cmdline
 * options, and validates the structure.
 *
 * Returns 0 on success, -1 on failure.
 */
int args_set(Options *o, const int argc, char *argv[]);

/* Frees the alloced data and sets everything to 0 */
void args_free(Options *o);

#endif /* XCOFFEBREAK_ARGS_H */
