#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

#include "mpris.h"
#include "utils.h"
#include "args.h"

static volatile sig_atomic_t g_running = 1;

static void
on_signal(int sig)
{
	(void)sig;
	g_running = 0;
}

/* Run a command via /bin/sh -c ... */
static int
run_sh(const char *cmd)
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

	int st = 0;
	if (waitpid(pid, &st, 0) < 0) {
		warn("waitpid:");
		return -1;
	}
	return st;
}

static unsigned long
x11_idle_ms(Display *dpy)
{
	XScreenSaverInfo *info = XScreenSaverAllocInfo();
	if (!info)
		return 0;

	XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), info);
	unsigned long idle = info->idle;
	XFree(info);
	return idle;
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
	sa.sa_handler = on_signal;
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* X */
	Display *dpy = XOpenDisplay(NULL);
	if (!dpy)
		die("cannot open X display");

	/* MPRIS */
	Mpris m;
	if (mpris_init(&m) < 0) {
		warn("MPRIS init failed (running without inhibit)");
		memset(&m, 0, sizeof(m));
	}

	/* Stage guards */
	int did_lock = 0, did_off = 0, did_suspend = 0;

	/*
	 * Baseline trick:
	 * effective_idle_ms = max(0, raw_idle_ms - baseline_idle_ms)
	 * We update baseline when:
	 *  - user becomes active (raw idle decreased)
	 *  - inhibit starts (so you don't instantly lock after long playback)
	 */
	unsigned long baseline_idle_ms = x11_idle_ms(dpy);
	int last_playing = (m.conn) ? mpris_is_playing(&m) : 0;

	while (g_running) {
		/* Block up to POLL_MS, but wake immediately on MPRIS signal */
		if (m.conn) mpris_poll(&m, (int)opt.poll_ms);
		else {
			struct timespec ts = { .tv_sec = opt.poll_ms / 1000,
			                       .tv_nsec = (opt.poll_ms % 1000) * 1000000L };
			nanosleep(&ts, NULL);
		}

		unsigned long raw_idle = x11_idle_ms(dpy);

		int playing = (m.conn) ? mpris_is_playing(&m) : 0;

		/* Reset baseline ONLY on actual user activity */
		if (raw_idle < baseline_idle_ms) {
			baseline_idle_ms = raw_idle;
			did_lock = did_off = did_suspend = 0;
		}

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

		if (!did_lock && eff_idle_s >= opt.lock_s) {
			did_lock = 1;
			(void)run_sh(opt.lock_cmd);
		}

		if (!did_off && eff_idle_s >= opt.off_s) {
			did_off = 1;
			(void)run_sh(opt.off_cmd);
		}

		if (!did_suspend && eff_idle_s >= opt.suspend_s) {
			did_suspend = 1;
			(void)run_sh(opt.suspend_cmd);
		}
	}

	args_free(&opt);
	XCloseDisplay(dpy);
	return 0;
}
