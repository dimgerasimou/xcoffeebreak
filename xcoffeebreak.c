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
static void cleanup(Options *opt, X11 *x, Mpris *m);
static void init(Options *opt, X11 **x, StateManager *sm, Mpris **m);
static void signals_init(void);
static void poll_wait(Mpris **m, unsigned int timeout_ms);
static void sighandler(int sig);

void
cleanup(Options *opt, X11 *x, Mpris *m)
{
	args_free(opt);
	mpris_cleanup(m);
	x11_cleanup(x);
}

void
init(Options *opt, X11 **x, StateManager *sm, Mpris **m)
{
	signals_init();
	*x = x11_init();
	*m = mpris_init(opt->verbose);
	state_manager_init(sm, x11_idle_ms(*x));
}

void
signals_init(void)
{
	struct sigaction sa;
	struct sigaction sachld;

	/* Setup signal handlers FIRST, before any fork() calls */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* Avoid zombie children from non-blocking fork/exec */
	memset(&sachld, 0, sizeof(sachld));
	sachld.sa_handler = SIG_IGN;
	sachld.sa_flags = SA_NOCLDWAIT;
	sigaction(SIGCHLD, &sachld, NULL);
}

void
poll_wait(Mpris **m, unsigned int timeout_ms)
{
	if (m && *m) {
		if (mpris_poll(*m, timeout_ms) < 0) {
			warn("[MPRIS] Lost DBus connection, running without inhibit");
			mpris_cleanup(*m);
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
	X11 *x = NULL;

	if (args_set(&opt, argc, argv))
		return 1;

	init(&opt, &x, &sm, &m);

	while (g_running) {
		State st;

		poll_wait(&m, opt.poll_ms);

		/* Check for suspend/resume */
		if (state_manager_check_suspend(&sm)) {
			state_manager_handle_resume(&sm, x11_idle_ms(x), opt.verbose);
			continue;
		}

		st = state_manager_update(&sm, &opt, x11_idle_ms(x), mpris_is_playing(m));

		/* Forward transitions execute commands */
		if (st > sm.current) {
			state_transition(&opt, sm.current, st);
			sm.current = st;
		}
	}

	cleanup(&opt, x, m);
	return 0;
}
