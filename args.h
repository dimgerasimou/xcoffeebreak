#ifndef ARGS_H
#define ARGS_H

typedef struct {
	unsigned long  lock_s;
	unsigned long  off_s;
	unsigned long  suspend_s;
	unsigned long  poll_ms;
	int       verbose;
	int       dry_run;
	char     *lock_cmd;
	char     *off_cmd;
	char     *suspend_cmd;
} Options;

int args_set(Options *o, const int argc, char *argv[]);
void args_free(Options *o);

#endif
