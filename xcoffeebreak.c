#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

#include "mpris.h"
#include "utils.h"
#include "args.h"

#define X11_IDLE_JITTER_MS 250

static volatile sig_atomic_t g_running = 1;

typedef enum {
	ST_ACTIVE = 0,
	ST_LOCKED,
	ST_OFF,
	ST_SUSPENDED,
} State;

static int            run(const char *cmd);
static void           sighandler(int sig);
static State          state_desired(const Options *opt, long idle_s);
static const char    *state_name(State st);
static void           state_transition(const Options *opt, State from, State to);
static unsigned long  x11_idle_ms(Display *dpy, XScreenSaverInfo *info);

int
run(const char *cmd)
{
	if (!cmd || !*cmd) return 0;

	pid_t pid = fork();
	if (pid < 0) {
		warn("fork:");
		return -1;
	}
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}

	return 0;
}

void
sighandler(int sig)
{
	(void)sig;
	g_running = 0;
}

State
state_desired(const Options *opt, long idle_s)
{
	if (idle_s >= (long)opt->suspend_s) return ST_SUSPENDED;
	if (idle_s >= (long)opt->off_s)     return ST_OFF;
	if (idle_s >= (long)opt->lock_s)    return ST_LOCKED;
	return ST_ACTIVE;
}

const char *
state_name(State st)
{
	switch (st) {
	case ST_ACTIVE:    return "ACTIVE";
	case ST_LOCKED:    return "LOCKED";
	case ST_OFF:       return "OFF";
	case ST_SUSPENDED: return "SUSPENDED";
	default:           return "?";
	}
}

void
state_transition(const Options *opt, State from, State to)
{
	/* Only execute actions when moving forward. */
	for (int st = (int)from + 1; st <= (int)to; st++) {
		const char *cmd = NULL;
		const char *what = NULL;

		switch ((State)st) {
		case ST_LOCKED:
			what = "lock";
			cmd = opt->lock_cmd;
			break;

		case ST_OFF:
			what = "off";
			cmd = opt->off_cmd;
			break;

		case ST_SUSPENDED:
			what = "suspend";
			cmd = opt->suspend_cmd;
			break;

		default:
			die("invalid state: %d", st);
			break;
		}

		if (!cmd)
			continue;

		verbose(opt->verbose, "%s -> %s (%s)",
		        state_name(from), state_name((State)st), what);

		if (!opt->dry_run)
			(void)run(cmd);

		from = (State)st;
	}
}

unsigned long
x11_idle_ms(Display *dpy, XScreenSaverInfo *info)
{
	if (!info)
		return 0;

	XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), info);
	return info->idle;
}

int
main(int argc, char *argv[])
{
	Options opt;

	if (args_set(&opt, argc, argv))
		return 1;

	/* Signals */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* Avoid zombie children from non-blocking run_sh() */
	struct sigaction sachld;
	memset(&sachld, 0, sizeof(sachld));
	sachld.sa_handler = SIG_IGN;
	sachld.sa_flags = SA_NOCLDWAIT;
	sigaction(SIGCHLD, &sachld, NULL);

	/* X */
	Display *dpy = XOpenDisplay(NULL);
	if (!dpy)
		die("cannot open X display");

	XScreenSaverInfo *xss = XScreenSaverAllocInfo();
	if (!xss)
		die("XScreenSaverAllocInfo failed");

	/* MPRIS */
	Mpris m;
	if (mpris_init(&m) < 0) {
		warn("MPRIS init failed (running without inhibit)");
		memset(&m, 0, sizeof(m));
	}

	State st = ST_ACTIVE;

	/*
	 * Baseline trick:
	 * effective_idle_ms = max(0, raw_idle_ms - baseline_idle_ms)
	 * We update baseline when:
	 *  - user becomes active (raw idle decreased)
	 *  - inhibit starts (so you don't instantly lock after long playback)
	 */
	unsigned long baseline_idle_ms = x11_idle_ms(dpy, xss);
	int last_playing = (m.conn) ? mpris_is_playing(&m) : 0;
	unsigned long last_raw_idle = baseline_idle_ms;

	while (g_running) {
		/* Block up to POLL_MS, but wake immediately on MPRIS signal */
		if (m.conn) {
			mpris_poll(&m, (int)opt.poll_ms);
		} else {
			struct timespec ts = { .tv_sec = opt.poll_ms / 1000,
			                       .tv_nsec = (opt.poll_ms % 1000) * 1000000L };
			nanosleep(&ts, NULL);
		}

		unsigned long raw_idle = x11_idle_ms(dpy, xss);

		int playing = (m.conn) ? mpris_is_playing(&m) : 0;

		/* Reset baseline ONLY on actual user activity.
		 * XScreenSaver idle can jitter slightly; ignore small backward jumps. */
		if (raw_idle + X11_IDLE_JITTER_MS < last_raw_idle) {
			baseline_idle_ms = raw_idle;
			if (st != ST_ACTIVE) {
				verbose(opt.verbose, "%s -> %s (%s)",
				        state_name(st), state_name(ST_ACTIVE), "user activity");
				st = ST_ACTIVE;
			}
		}
		last_raw_idle = raw_idle;

		/* On inhibit START: update baseline to prevent instant lock */
		if (playing && !last_playing) {
			baseline_idle_ms = raw_idle;
			last_playing = playing;
		}

		/* On inhibit END: just update state, keep accumulating idle time */
		if (!playing && last_playing) {
			last_playing = playing;
			/* Do NOT reset baseline - user is still idle! */
		}

		/* Inhibit all actions while playing */
		if (playing) {
			continue;
		}

		unsigned long eff_idle = (raw_idle > baseline_idle_ms) ? (raw_idle - baseline_idle_ms) : 0;
		long eff_idle_s = (long)(eff_idle / 1000UL);

		State want = state_desired(&opt, eff_idle_s);

		/* If we moved backward (e.g., baseline reset), just update state (no actions). */
		if (want < st)
			st = want;

		/* Forward transitions execute actions once per threshold crossing. */
		if (want > st) {
			state_transition(&opt, st, want);
			st = want;
		}
	}

	args_free(&opt);
	XFree(xss);
	XCloseDisplay(dpy);
	return 0;
}
