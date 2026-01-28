#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <unistd.h>

#include "state.h"
#include "utils.h"

static int run_cmd(const char *cmd);

void
state_manager_init(StateManager *sm, unsigned long initial_idle_ms)
{
	sm->current = ST_ACTIVE;
	sm->baseline_idle_ms = initial_idle_ms;
	sm->last_raw_idle_ms = initial_idle_ms;
	sm->last_clock_ms = 0;
	sm->last_playing = false;
}

void
state_manager_handle_resume(StateManager *sm, unsigned long raw_idle_ms, bool v)
{
	sm->baseline_idle_ms = raw_idle_ms;
	sm->last_raw_idle_ms = raw_idle_ms;

	if (sm->current != ST_ACTIVE) {
		verbose(v, "[STATE] %s -> %s (resume from suspend)", state_name(sm->current), state_name(ST_ACTIVE));
		sm->current = ST_ACTIVE;
	}
}

State
state_manager_update(StateManager *sm, const Options *opt,
                     unsigned long raw_idle_ms, bool playing)
{
	/*
	 * Baseline idle time management:
	 *
	 * effective_idle_ms = max(0, raw_idle_ms - baseline_idle_ms)
	 *
	 * We update baseline when:
	 *  1. User becomes active (raw idle decreased significantly)
	 *  2. Inhibit starts (prevents instant lock after long playback)
	 *  3. Inhibit ends (reset to allow fresh idle accumulation)
	 *  4. System resumes from suspend (handled separately)
	 */

	/* Detect user activity: idle time decreased beyond jitter threshold */
	if (raw_idle_ms + X11_IDLE_JITTER_MS < sm->last_raw_idle_ms) {
		sm->baseline_idle_ms = raw_idle_ms;
		if (sm->current != ST_ACTIVE) {
			verbose(opt->verbose, "[STATE] %s -> %s (user activity)",
			        state_name(sm->current), state_name(ST_ACTIVE));
			sm->current = ST_ACTIVE;
		}
	}
	sm->last_raw_idle_ms = raw_idle_ms;

	/* Handle inhibit state changes */
	if (playing && !sm->last_playing) {
		/* Inhibit started: reset baseline to prevent instant lock */
		sm->baseline_idle_ms = raw_idle_ms;
		sm->last_playing = playing;
	} else if (!playing && sm->last_playing) {
		/* Inhibit ended: reset baseline for fresh idle accumulation */
		sm->baseline_idle_ms = raw_idle_ms;
		sm->last_playing = playing;
		verbose(opt->verbose, "[MPRIS] inhibit ended (reset baseline)");
	}

	/* Don't update state while media is playing */
	if (playing) {
		return sm->current;
	}

	/* Calculate effective idle time with overflow protection */
	unsigned long eff_idle_ms;
	if (raw_idle_ms >= sm->baseline_idle_ms) {
		eff_idle_ms = raw_idle_ms - sm->baseline_idle_ms;
	} else {
		/* Handle wraparound (unlikely but possible after ~49.7 days) */
		eff_idle_ms = 0;
	}

	unsigned long eff_idle_s = eff_idle_ms / 1000UL;
	State desired = state_desired(opt, eff_idle_s);

	/* Backward transitions: just update state, no commands */
	if (desired < sm->current) {
		sm->current = desired;
	}

	return desired;
}

bool
state_manager_check_suspend(StateManager *sm)
{
	/*
	 * Detect system suspend/resume by monitoring monotonic clock jumps.
	 * If monotonic time jumps forward significantly, the system likely
	 * suspended and resumed.
	 */
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return false;

	unsigned long now_ms =
	    (unsigned long)ts.tv_sec * 1000UL +
	    (unsigned long)ts.tv_nsec / 1000000UL;

	if (sm->last_clock_ms == 0) {
		sm->last_clock_ms = now_ms;
		return false;
	}

	unsigned long delta_ms = now_ms - sm->last_clock_ms;
	sm->last_clock_ms = now_ms;

	/* Suspend detected if more than threshold passed in one poll cycle */
	return (delta_ms > SUSPEND_DETECT_MS);
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

State
state_desired(const Options *opt, unsigned long idle_s)
{
	if (idle_s >= opt->suspend_s) return ST_SUSPENDED;
	if (idle_s >= opt->off_s)     return ST_OFF;
	if (idle_s >= opt->lock_s)    return ST_LOCKED;
	return ST_ACTIVE;
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

	/* Only execute actions when moving forward */
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
			(void)run_cmd(cmd);

		from = (State)st;
	}
}

static int
run_cmd(const char *cmd)
{
	if (!cmd || !*cmd)
		return 0;

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
