#define _POSIX_C_SOURCE 200809L

#include <signal.h>
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

static int            detect_suspend_resume(unsigned long *last_clock_ms);
static int            run(const char *cmd);
static void           sighandler(int sig);
static State          state_desired(const Options *opt, unsigned long idle_s);
static const char    *state_name(State st);
static void           state_transition(const Options *opt, State from, State to);
static unsigned long  x11_idle_ms(Display *dpy, XScreenSaverInfo *info);

int
detect_suspend_resume(unsigned long *last_clock_ms)
{
	/*
	 * Detect system suspend/resume by monitoring monotonic clock jumps.
	 * If wall clock time jumps forward significantly more than expected,
	 * the system likely suspended. Returns 1 if suspend detected, 0 otherwise.
	 */
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	
	unsigned long now_ms = 
	    (unsigned long)ts.tv_sec * 1000UL +
	    (unsigned long)ts.tv_nsec / 1000000UL;
	
	if (*last_clock_ms == 0) {
		*last_clock_ms = now_ms;
		return 0;
	}
	
	/* Check if time jumped forward by more than 5 seconds
	 * (accounting for potential unsigned wraparound) */
	unsigned long delta_ms = now_ms - *last_clock_ms;
	*last_clock_ms = now_ms;
	
	/* Suspend detected if more than 5 seconds passed in one poll cycle */
	return (delta_ms > 5000UL);
}

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
state_desired(const Options *opt, unsigned long idle_s)
{
	if (idle_s >= (unsigned long)opt->suspend_s) return ST_SUSPENDED;
	if (idle_s >= (unsigned long)opt->off_s)     return ST_OFF;
	if (idle_s >= (unsigned long)opt->lock_s)    return ST_LOCKED;
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
	/*
	 * State transition behavior:
	 * 
	 * We only execute actions when moving FORWARD through states
	 * (ACTIVE -> LOCKED -> OFF -> SUSPENDED). When moving backward
	 * (e.g., user activity detected), we simply update the state
	 * variable without executing unlock/wake commands.
	 * 
	 * This means:
	 * - If user is at SUSPENDED and becomes active briefly, then idle
	 *   again, we won't re-execute lock/off/suspend commands until
	 *   the idle timer crosses each threshold again from ACTIVE.
	 * - This prevents command spam and allows external wake mechanisms
	 *   (like systemd resume) to handle state restoration.
	 * - The baseline idle time is reset on user activity, so the timer
	 *   effectively restarts from zero.
	 */
	
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

		verbose(opt->verbose, "[STATE] %s -> %s (%s)",
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

	/* Setup signal handlers FIRST, before any fork() calls in run() */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* Avoid zombie children from non-blocking run() */
	struct sigaction sachld;
	memset(&sachld, 0, sizeof(sachld));
	sachld.sa_handler = SIG_IGN;
	sachld.sa_flags = SA_NOCLDWAIT;
	sigaction(SIGCHLD, &sachld, NULL);

	/* X */
	Display *dpy = XOpenDisplay(NULL);
	if (!dpy)
		die("[X11] cannot open X display");

	XScreenSaverInfo *xss = XScreenSaverAllocInfo();
	if (!xss)
		die("[X11] XScreenSaverAllocInfo failed");

	/* MPRIS */
	Mpris m;
	if (mpris_init(&m, opt.verbose) < 0) {
		warn("[MPRIS] init failed (running without inhibit)");
		memset(&m, 0, sizeof(m));
	}

	State st = ST_ACTIVE;

	/*
	 * Baseline idle time management:
	 * 
	 * effective_idle_ms = max(0, raw_idle_ms - baseline_idle_ms)
	 * 
	 * We update baseline when:
	 *  1. User becomes active (raw idle decreased significantly)
	 *  2. Inhibit starts (prevents instant lock after long playback)
	 *  3. System resumes from suspend (X11 idle might be stale)
	 * 
	 * This allows idle time to accumulate naturally while preventing
	 * spurious timeouts.
	 */
	unsigned long baseline_idle_ms = x11_idle_ms(dpy, xss);
	int last_playing = (m.conn) ? mpris_is_playing(&m) : 0;
	unsigned long last_raw_idle = baseline_idle_ms;
	unsigned long last_clock_ms = 0;

	while (g_running) {
		/* Block up to POLL_MS, but wake immediately on MPRIS signal */
		if (m.conn) {
			mpris_poll(&m, opt.poll_ms);
			
			/* Check for DBus connection loss */
			if (mpris_check_connection(&m) < 0) {
				warn("[MPRIS] Lost DBus connection, running without inhibit");
				m.conn = NULL;
			}
		} else {
			struct timespec ts = { 
				.tv_sec = opt.poll_ms / 1000,
				.tv_nsec = (opt.poll_ms % 1000) * 1000000L 
			};
			nanosleep(&ts, NULL);
		}

		/* Detect suspend/resume - reset baseline if detected */
		if (detect_suspend_resume(&last_clock_ms)) {
			unsigned long raw_idle = x11_idle_ms(dpy, xss);
			baseline_idle_ms = raw_idle;
			last_raw_idle = raw_idle;
			if (st != ST_ACTIVE) {
				verbose(opt.verbose, "[STATE] %s -> %s (resume from suspend)",
				        state_name(st), state_name(ST_ACTIVE));
				st = ST_ACTIVE;
			}
			continue;
		}

		unsigned long raw_idle = x11_idle_ms(dpy, xss);

		int playing = (m.conn) ? mpris_is_playing(&m) : 0;

		/* Reset baseline ONLY on actual user activity.
		 * XScreenSaver idle can jitter slightly; ignore small backward jumps. */
		if (raw_idle + X11_IDLE_JITTER_MS < last_raw_idle) {
			baseline_idle_ms = raw_idle;
			if (st != ST_ACTIVE) {
				verbose(opt.verbose, "[STATE] %s -> %s (user activity)",
				        state_name(st), state_name(ST_ACTIVE));
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
			baseline_idle_ms = raw_idle;
			last_playing = playing;
			verbose(opt.verbose, "[MPRIS] inhibit ended (reset baseline)");
		}

		/* Inhibit all actions while playing */
		if (playing) {
			continue;
		}

		/* Calculate effective idle with overflow protection */
		unsigned long eff_idle_ms;
		if (raw_idle >= baseline_idle_ms) {
			eff_idle_ms = raw_idle - baseline_idle_ms;
		} else {
			/* Handle wraparound (unlikely but possible after ~49.7 days) */
			eff_idle_ms = 0;
		}
		
		unsigned long eff_idle_s = eff_idle_ms / 1000UL;

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
