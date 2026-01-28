#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <time.h>

#include "mpris.h"
#include "utils.h"

#define MPRIS_FALLBACK_POLL_S 2     /* 0 = disabled */
#define MPRIS_STARVATION_MS   5000  /* force re-sync if no dbus activity */
#define DBUS_CALL_TIMEOUT_MS  200   /* Timeout for blocking DBus calls (ms) */


static Player *
player_find(Mpris *m, const char *name)
{
	for (Player *p = m->players; p; p = p->next)
		if (streq(p->name, name))
			return p;
	return NULL;
}

static Player *
player_add(Mpris *m, const char *name)
{
	Player *p = calloc(1, sizeof(*p));
	if (!p) {
		warn("[MPRIS] calloc failed");
		return NULL;
	}

	p->name = strdup(name);
	if (!p->name) {
		warn("[MPRIS] strdup failed");
		free(p);
		return NULL;
	}

	p->next = m->players;
	m->players = p;
	
	if (m->verbose)
		verbose(1, "[MPRIS] player added: %s", name);
	
	return p;
}

static void
player_remove(Mpris *m, const char *name)
{
	Player **pp = &m->players;
	while (*pp) {
		Player *p = *pp;
		if (streq(p->name, name)) {
			if (p->is_playing && m->playing_count > 0)
				m->playing_count--;
			*pp = p->next;
			
			if (m->verbose)
				verbose(1, "[MPRIS] player removed: %s", name);
			
			free(p->name);
			free(p);
			return;
		}
		pp = &p->next;
	}
}

static void
set_player_playing(Mpris *m, Player *p, bool playing)
{
	if (!p)
		return;
	if (p->is_playing == playing)
		return;

	/* Verbose logging for state changes */
	if (m->verbose) {
		verbose(1, "[MPRIS] %s %s -> %s", 
		        p->name,
		        p->is_playing ? "playing" : "stopped",
		        playing ? "playing" : "stopped");
	}

	p->is_playing = playing;
	if (playing)
		m->playing_count++;
	else if (m->playing_count > 0)
		m->playing_count--;
}


static int
watch_index_by_ptr(Mpris *m, DBusWatch *w)
{
	for (size_t i = 0; i < m->nwatches; i++)
		if (m->watches[i].watch == w)
			return (int)i;
	return -1;
}

static int
watches_reserve(Mpris *m, size_t need)
{
	if (need <= m->cap_watches)
		return 0;
	size_t newcap = m->cap_watches ? m->cap_watches * 2 : 16;
	if (newcap < need)
		newcap = need;

	WatchEnt *nw = realloc(m->watches, newcap * sizeof(*nw));
	if (!nw)
		return -1;
	m->watches = nw;
	m->cap_watches = newcap;
	return 0;
}

static dbus_bool_t
add_watch(DBusWatch *watch, void *data)
{
	Mpris *m = (Mpris *)data;
	if (!dbus_watch_get_enabled(watch))
		return TRUE;

	if (watch_index_by_ptr(m, watch) >= 0)
		return TRUE;

	if (watches_reserve(m, m->nwatches + 1) < 0) {
		warn("[MPRIS] out of memory (add_watch)");
		return FALSE;
	}

	int fd = dbus_watch_get_unix_fd(watch);
	m->watches[m->nwatches].watch = watch;
	m->watches[m->nwatches].fd = fd;
	m->nwatches++;
	return TRUE;
}

static void
remove_watch(DBusWatch *watch, void *data)
{
	Mpris *m = (Mpris *)data;
	int idx = watch_index_by_ptr(m, watch);
	if (idx < 0)
		return;

	/* compact */
	m->watches[idx] = m->watches[m->nwatches - 1];
	m->nwatches--;
}

static void
toggle_watch(DBusWatch *watch, void *data)
{
	/* Nothing to do here: we consult enabled/flags at poll time. */
	(void)watch;
	(void)data;
}

static short
dbus_flags_to_poll(unsigned int flags)
{
	short ev = 0;

	if (flags & DBUS_WATCH_READABLE)
		ev |= POLLIN;
	if (flags & DBUS_WATCH_WRITABLE)
		ev |= POLLOUT;
	return ev;
}

static unsigned int
poll_revents_to_dbus(short revents)
{
	unsigned int flags = 0;

	if (revents & POLLIN)
		flags |= DBUS_WATCH_READABLE;
	if (revents & POLLOUT)
		flags |= DBUS_WATCH_WRITABLE;
	if (revents & POLLERR)
		flags |= DBUS_WATCH_ERROR;
	if (revents & POLLHUP)
		flags |= DBUS_WATCH_HANGUP;
	return flags;
}

/* --------------------------- MPRIS DBus helpers -------------------------- */

static int
dbus_call_get_playbackstatus(Mpris *m, const char *service, int *out_playing)
{
	/* org.freedesktop.DBus.Properties.Get("org.mpris.MediaPlayer2.Player","PlaybackStatus") */
	DBusMessage *msg = dbus_message_new_method_call(
		service,
		"/org/mpris/MediaPlayer2",
		"org.freedesktop.DBus.Properties",
		"Get");
	if (!msg)
		return -1;

	const char *iface = "org.mpris.MediaPlayer2.Player";
	const char *prop  = "PlaybackStatus";
	if (!dbus_message_append_args(msg,
	                             DBUS_TYPE_STRING, &iface,
	                             DBUS_TYPE_STRING, &prop,
	                             DBUS_TYPE_INVALID)) {
		dbus_message_unref(msg);
		return -1;
	}

	DBusError err;
	dbus_error_init(&err);
	/* Use shorter timeout to avoid blocking main loop */
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(m->conn, msg, DBUS_CALL_TIMEOUT_MS, &err);
	dbus_message_unref(msg);

	if (!reply) {
		/* Not fatal: player might not implement it yet or disappeared */
		if (dbus_error_is_set(&err)) dbus_error_free(&err);
		return -1;
	}

	/* reply has signature: (v) */
	DBusMessageIter it;
	dbus_message_iter_init(reply, &it);
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_VARIANT) {
		dbus_message_unref(reply);
		return -1;
	}

	DBusMessageIter v;
	dbus_message_iter_recurse(&it, &v);
	if (dbus_message_iter_get_arg_type(&v) != DBUS_TYPE_STRING) {
		dbus_message_unref(reply);
		return -1;
	}

	const char *status = NULL;
	dbus_message_iter_get_basic(&v, &status);
	*out_playing = (status && streq(status, "Playing")) ? 1 : 0;

	dbus_message_unref(reply);
	return 0;
}

static void
initial_sync_players(Mpris *m)
{
	/* List all bus names, add any org.mpris.MediaPlayer2.*, and read current PlaybackStatus once. */
	DBusMessage *msg = dbus_message_new_method_call(
		"org.freedesktop.DBus",
		"/org/freedesktop/DBus",
		"org.freedesktop.DBus",
		"ListNames");
	if (!msg)
		return;

	DBusError err;
	dbus_error_init(&err);
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(m->conn, msg, DBUS_CALL_TIMEOUT_MS, &err);
	dbus_message_unref(msg);
	if (!reply) {
		if (dbus_error_is_set(&err))
			dbus_error_free(&err);
		return;
	}

	DBusMessageIter it;
	if (!dbus_message_iter_init(reply, &it) ||
	    dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) {
		dbus_message_unref(reply);
		return;
	}

	DBusMessageIter arr;
	dbus_message_iter_recurse(&it, &arr);
	while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
		const char *name = NULL;
		dbus_message_iter_get_basic(&arr, &name);

		if (name && strncmp(name, "org.mpris.MediaPlayer2.", 23) == 0) {
			Player *p = player_find(m, name);
			if (!p)
				p = player_add(m, name);

			int playing = 0;
			if (p && dbus_call_get_playbackstatus(m, name, &playing) == 0)
				set_player_playing(m, p, playing);
		}

		dbus_message_iter_next(&arr);
	}

	dbus_message_unref(reply);
}

/* ------------------------------ Signal parsing --------------------------- */

static int
read_variant_string(DBusMessageIter *variant, const char **out)
{
	if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_VARIANT) return 0;
	DBusMessageIter sub;
	dbus_message_iter_recurse(variant, &sub);
	if (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_STRING) return 0;
	dbus_message_iter_get_basic(&sub, out);
	return 1;
}

/* org.freedesktop.DBus.Properties.PropertiesChanged */
static void
handle_properties_changed(Mpris *m, DBusMessage *msg)
{
	const char *sender = dbus_message_get_sender(msg);
	if (!sender)
		return;

	if (strncmp(sender, "org.mpris.MediaPlayer2.", 23) != 0)
		return;

	Player *p = player_find(m, sender);
	if (!p) p = player_add(m, sender);
	if (!p) return;

	DBusMessageIter it;
	if (!dbus_message_iter_init(msg, &it)) return;

	/* 1st arg: interface name */
	const char *iface = NULL;
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) return;
	dbus_message_iter_get_basic(&it, &iface);
	if (!streq(iface, "org.mpris.MediaPlayer2.Player"))
		return;

	/* 2nd arg: changed properties (a{sv}) */
	if (!dbus_message_iter_next(&it)) return;
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) return;

	DBusMessageIter array;
	dbus_message_iter_recurse(&it, &array);

	int saw_status = 0;

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry;
		dbus_message_iter_recurse(&array, &entry);

		const char *key = NULL;
		if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) break;
		dbus_message_iter_get_basic(&entry, &key);

		if (!dbus_message_iter_next(&entry)) break;

		if (streq(key, "PlaybackStatus")) {
			saw_status = 1;
			const char *status = NULL;
			if (read_variant_string(&entry, &status)) {
				set_player_playing(m, p, streq(status, "Playing"));
			}
		}

		dbus_message_iter_next(&array);
	}

	/* Some players don't include PlaybackStatus in PropertiesChanged.
	 * If we didn't see it, do a one-off Get to resync. */
	if (!saw_status) {
		int playing = 0;
		if (dbus_call_get_playbackstatus(m, sender, &playing) == 0)
			set_player_playing(m, p, playing);
	}
}

/* org.freedesktop.DBus.NameOwnerChanged */
static void
handle_name_owner_changed(Mpris *m, DBusMessage *msg)
{
	const char *name = NULL, *old_owner = NULL, *new_owner = NULL;
	if (!dbus_message_get_args(msg, NULL,
	                          DBUS_TYPE_STRING, &name,
	                          DBUS_TYPE_STRING, &old_owner,
	                          DBUS_TYPE_STRING, &new_owner,
	                          DBUS_TYPE_INVALID))
		return;

	(void)old_owner;

	if (!name || strncmp(name, "org.mpris.MediaPlayer2.", 23) != 0)
		return;

	/* disappeared */
	if (new_owner && *new_owner == '\0') {
		player_remove(m, name);
		return;
	}

	/* appeared: add and do a one-time Get for current status */
	Player *p = player_find(m, name);
	if (!p)
		p = player_add(m, name);
	if (!p)
		return;

	int playing = 0;
	if (dbus_call_get_playbackstatus(m, name, &playing) == 0) {
		set_player_playing(m, p, playing);
	}
}

static size_t
dispatch_all_messages(Mpris *m)
{
	size_t n = 0;

	for (;;) {
		DBusMessage *msg = dbus_connection_pop_message(m->conn);
		if (!msg)
			break;

		n++;

		if (dbus_message_is_signal(msg, "org.freedesktop.DBus.Properties", "PropertiesChanged")) {
			handle_properties_changed(m, msg);
		} else if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
			handle_name_owner_changed(m, msg);
		}

		dbus_message_unref(msg);
	}

	return n;
}

/* ------------------------------ MPRIS public ----------------------------- */

int
mpris_init(Mpris *m, int verbose)
{
	memset(m, 0, sizeof(*m));
	m->verbose = verbose;

	DBusError err;
	dbus_error_init(&err);

	m->conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!m->conn) {
		warn("[MPRIS] dbus_bus_get failed: %s", err.message ? err.message : "unknown error");
		dbus_error_free(&err);
		return -1;
	}

	/* Match MPRIS PropertiesChanged */
	dbus_bus_add_match(m->conn,
		"type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
		&err);
	if (dbus_error_is_set(&err)) {
		warn("[MPRIS] add_match(PropertiesChanged) failed: %s", err.message);
		dbus_error_free(&err);
		return -1;
	}

	/* Track MPRIS names appearing/disappearing */
	dbus_bus_add_match(m->conn,
		"type='signal',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0namespace='org.mpris.MediaPlayer2'",
		&err);
	if (dbus_error_is_set(&err)) {
		warn("[MPRIS] add_match(NameOwnerChanged) failed: %s", err.message);
		dbus_error_free(&err);
		return -1;
	}

	if (!dbus_connection_set_watch_functions(m->conn, add_watch, remove_watch, toggle_watch, m, NULL)) {
		warn("[MPRIS] set_watch_functions failed");
		return -1;
	}

	/* Initial sync: discover existing players + fetch current status once */
	initial_sync_players(m);

	/* Drain any queued signals */
	dbus_connection_read_write(m->conn, 0);
	(void)dispatch_all_messages(m);

	return 0;
}

bool
mpris_is_playing(const Mpris *m)
{
	return m->playing_count > 0;
}

int
mpris_check_connection(Mpris *m)
{
	if (!m->conn)
		return -1;
		
	if (!dbus_connection_get_is_connected(m->conn)) {
		warn("[MPRIS] DBus connection lost");
		return -1;
	}
	
	return 0;
}

void
mpris_poll(Mpris *m, unsigned int timeout_ms)
{
	/* Allocate pollfd array dynamically based on number of watches */
	struct pollfd *pfds = NULL;
	size_t nfds = 0;
	size_t pfds_cap = 0;
	static unsigned long long last_fallback_ms = 0;
	static unsigned long long last_activity_ms = 0;

	/* ----------- normal DBus watch-based polling ----------- */

	/* First pass: determine unique fds and allocate array */
	for (size_t i = 0; i < m->nwatches; i++) {
		DBusWatch *w = m->watches[i].watch;
		if (!dbus_watch_get_enabled(w))
			continue;

		int fd = m->watches[i].fd;
		
		/* Check if we already have this fd */
		int found = 0;
		for (size_t k = 0; k < nfds; k++) {
			if (pfds && pfds[k].fd == fd) {
				found = 1;
				break;
			}
		}
		
		if (!found) {
			/* Need to expand array */
			if (nfds >= pfds_cap) {
				size_t new_cap = pfds_cap ? pfds_cap * 2 : 16;
				struct pollfd *new_pfds = realloc(pfds, new_cap * sizeof(*new_pfds));
				if (!new_pfds) {
					warn("[MPRIS] realloc failed for pollfd array:");
					free(pfds);
					return;
				}
				pfds = new_pfds;
				pfds_cap = new_cap;
			}
			
			pfds[nfds].fd = fd;
			pfds[nfds].events = 0;
			pfds[nfds].revents = 0;
			nfds++;
		}
	}
	
	/* Second pass: accumulate events for each fd */
	for (size_t i = 0; i < m->nwatches; i++) {
		DBusWatch *w = m->watches[i].watch;
		if (!dbus_watch_get_enabled(w))
			continue;

		int fd = m->watches[i].fd;
		unsigned int wflags = dbus_watch_get_flags(w);
		short events = dbus_flags_to_poll(wflags);

		/* Find this fd in our array and OR in the events */
		for (size_t k = 0; k < nfds; k++) {
			if (pfds[k].fd == fd) {
				pfds[k].events |= events;
				break;
			}
		}
	}

	int pret = poll(pfds, (nfds_t)nfds, (int)timeout_ms);

	/* For each watch, call dbus_watch_handle with matching revents for its fd
	 * BUT only pass flags that this specific watch requested */
	for (size_t i = 0; i < m->nwatches; i++) {
		DBusWatch *w = m->watches[i].watch;
		if (!dbus_watch_get_enabled(w)) continue;

		int fd = m->watches[i].fd;
		unsigned int wflags = dbus_watch_get_flags(w);

		/* Find revents for this fd */
		short revents = 0;
		for (size_t k = 0; k < nfds; k++) {
			if (pfds[k].fd == fd) { 
				revents = pfds[k].revents; 
				break; 
			}
		}

		if (revents) {
			/* Convert revents to dbus flags */
			unsigned int all_flags = poll_revents_to_dbus(revents);
			
			/* Mask to only flags this watch cares about */
			unsigned int watch_events = dbus_flags_to_poll(wflags);
			unsigned int masked_flags = 0;
			
			if ((watch_events & POLLIN) && (revents & POLLIN))
				masked_flags |= DBUS_WATCH_READABLE;
			if ((watch_events & POLLOUT) && (revents & POLLOUT))
				masked_flags |= DBUS_WATCH_WRITABLE;
			if (revents & (POLLERR | POLLHUP))
				masked_flags |= (all_flags & (DBUS_WATCH_ERROR | DBUS_WATCH_HANGUP));
			
			if (masked_flags) {
				dbus_watch_handle(w, masked_flags);
			}
		}
	}
	
	free(pfds);

	/* Read queued data and handle signals */
	dbus_connection_read_write(m->conn, 0);

	size_t nmsg = dispatch_all_messages(m);

	/* ----------- watch starvation guard ----------- */
	struct timespec ats;
	if (clock_gettime(CLOCK_MONOTONIC, &ats) == 0) {
		unsigned long long now_ms =
		    (unsigned long long)ats.tv_sec * 1000ULL +
		    (unsigned long long)ats.tv_nsec / 1000000ULL;

		if (last_activity_ms == 0)
			last_activity_ms = now_ms;

		if (pret > 0 || nmsg > 0) {
			last_activity_ms = now_ms;
		} else if (now_ms - last_activity_ms >= (unsigned long long)MPRIS_STARVATION_MS) {
			/* If we didn't see any DBus activity for a while, force a re-scan. */
			initial_sync_players(m);
			dbus_connection_read_write(m->conn, 0);
			(void)dispatch_all_messages(m);
			last_activity_ms = now_ms;
		}
	}
	
#if MPRIS_FALLBACK_POLL_S > 0
	/* ----------- optional fallback polling (monotonic) ----------- */
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		unsigned long long now_ms =
		    (unsigned long long)ts.tv_sec * 1000ULL +
		    (unsigned long long)ts.tv_nsec / 1000000ULL;

		if (last_fallback_ms == 0)
			last_fallback_ms = now_ms;

		if (now_ms - last_fallback_ms >=
		    (unsigned long long)MPRIS_FALLBACK_POLL_S * 1000ULL) {

			last_fallback_ms = now_ms;

			/* re-sync PlaybackStatus for all known players */
			for (Player *p = m->players; p; p = p->next) {
				int playing = 0;
				if (dbus_call_get_playbackstatus(m, p->name, &playing) == 0)
					set_player_playing(m, p, playing);
			}
		}
	}
#endif
}
