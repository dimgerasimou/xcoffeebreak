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
static void           signals_init(void);
static void           mpris_init_or_warn(Mpris *m, bool verbose);
static void           handle_poll(Mpris *m, unsigned int timeout_ms);
static void           sighandler(int sig);

static void
setup(Options *opt, StateManager *sm, Mpris *m)
{
	signals_init();
	x11_init();

	mpris_init_or_warn(m, opt->verbose);

	state_manager_init(sm, x11_idle_ms());
}

static void
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

static void
mpris_init_or_warn(Mpris *m, bool verbose)
{
	if (mpris_init(m, verbose) < 0) {
		warn("[MPRIS] init failed (running without inhibit)");
		memset(m, 0, sizeof(*m));
	}
}

static void
handle_poll(Mpris *m, unsigned int timeout_ms)
{
	if (m->conn) {
		mpris_poll(m, timeout_ms);

		/* Check for DBus connection loss */
		if (mpris_check_connection(m) < 0) {
			warn("[MPRIS] Lost DBus connection, running without inhibit");
			m->conn = NULL;
		}
	} else {
		struct timespec ts = {
			.tv_sec = timeout_ms / 1000,
			.tv_nsec = (timeout_ms % 1000) * 1000000L
		};
		nanosleep(&ts, NULL);
	}
}

static void
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
	Mpris m;

	if (args_set(&opt, argc, argv))
		return 1;

	setup(&opt, &sm, &m);

	while (g_running) {
		handle_poll(&m, opt.poll_ms);

		/* Check for suspend/resume */
		if (state_manager_check_suspend(&sm)) {
			state_manager_handle_resume(&sm, x11_idle_ms(), opt.verbose);
			continue;
		}

		unsigned long raw_idle = x11_idle_ms();
		bool playing = (m.conn) ? mpris_is_playing(&m) : false;

		State desired = state_manager_update(&sm, &opt, raw_idle, playing);

		/* Forward transitions execute commands */
		if (desired > sm.current) {
			state_transition(&opt, sm.current, desired);
			sm.current = desired;
		}
	}

	args_free(&opt);
	x11_cleanup();
	return 0;
}
