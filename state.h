#ifndef XCOFFEEBREAK_STATE_H
#define XCOFFEEBREAK_STATE_H

#include <stdbool.h>
#include "args.h"

/* Suspend detection threshold: system suspended if clock jumps by this much */
#define SUSPEND_DETECT_MS 5000

/* X11 idle time can jitter slightly; ignore small backward jumps */
#define X11_IDLE_JITTER_MS 250

typedef enum {
	ST_ACTIVE = 0,
	ST_LOCKED,
	ST_OFF,
	ST_SUSPENDED,
} State;

typedef struct {
	State          current;
	unsigned long  baseline_idle_ms;
	unsigned long  last_raw_idle_ms;
	unsigned long  last_clock_ms;
	bool           last_playing;
} StateManager;

/* Initialize state manager with current idle time */
void state_manager_init(StateManager *sm, unsigned long initial_idle_ms);

/* Handle system resume from suspend - resets baseline and state */
void state_manager_handle_resume(StateManager *sm, unsigned long raw_idle_ms, bool verbose);

/* Update state based on idle time and media playback status
 * Returns the new desired state */
State state_manager_update(StateManager *sm, const Options *opt,
                           unsigned long raw_idle_ms, bool playing);

/* Check if system suspended by detecting large clock jumps
 * Returns true if suspend detected */
bool state_manager_check_suspend(StateManager *sm);

/* Get name of state for logging */
const char *state_name(State st);

/* Execute state transition commands (only forward transitions) */
void state_transition(const Options *opt, State from, State to);

/* Determine desired state based on idle time */
State state_desired(const Options *opt, unsigned long idle_s);

#endif /* XCOFFEEBREAK_STATE_H */
