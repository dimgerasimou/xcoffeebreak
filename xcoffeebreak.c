#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "args.h"
#include "mpris.h"
#include "state.h"
#include "utils.h"
#include "x.h"

static volatile sig_atomic_t g_running = 1;

/* Forward declarations */
static void cleanup(Options *opt, Mpris *m);
static void setup(Options *opt, StateManager *sm, Mpris **m);
static void signals_init(void);
static void poll(Mpris **m, unsigned int timeout_ms);
static void sighandler(int sig);

void
cleanup(Options *opt, Mpris *m)
{
	args_free(opt);
	mpris_close(m);
	x11_cleanup();
}

void
setup(Options *opt, StateManager *sm, Mpris **m)
{
	signals_init();
	x11_init();
	mpris_open(m, opt->verbose);
	state_manager_init(sm, x11_idle_ms());
}

void
signals_init(void)
{
	/* Setup signal handlers FIRST, before any fork() calls */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* Avoid zombie children from non-blocking fork/exec */
	struct sigaction sachld;
	memset(&sachld, 0, sizeof(sachld));
	sachld.sa_handler = SIG_IGN;
	sachld.sa_flags = SA_NOCLDWAIT;
	sigaction(SIGCHLD, &sachld, NULL);
}

void
poll(Mpris **m, unsigned int timeout_ms)
{
	if (m && *m) {
		if (mpris_poll(*m, timeout_ms) < 0) {
			warn("[MPRIS] Lost DBus connection, running without inhibit");
			mpris_close(*m);
			*m = NULL;
			return;
		}
		return;
	}

	struct timespec ts = {
		.tv_sec = timeout_ms / 1000,
		.tv_nsec = (timeout_ms % 1000) * 1000000L
	};
	nanosleep(&ts, NULL);
}

void
sighandler(int sig)
{
	(void)sig;
	g_running = 0;
}

int
main(int argc, char *argv[])
{
	Options opt;
	StateManager sm;
	Mpris *m = NULL;

	if (args_set(&opt, argc, argv))
		return 1;

	setup(&opt, &sm, &m);

	while (g_running) {
		State st;

		poll(&m, opt.poll_ms);

		/* Check for suspend/resume */
		if (state_manager_check_suspend(&sm)) {
			state_manager_handle_resume(&sm, x11_idle_ms(), opt.verbose);
			continue;
		}

		st = state_manager_update(&sm, &opt, x11_idle_ms(), mpris_is_playing(m));

		/* Forward transitions execute commands */
		if (st > sm.current) {
			state_transition(&opt, sm.current, st);
			sm.current = st;
		}
	}

	cleanup(&opt, m);
	return 0;
}
