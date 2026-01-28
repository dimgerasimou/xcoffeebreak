#ifndef MPRIS_H
#define MPRIS_H

#include <dbus/dbus.h>
#include <stdlib.h>

typedef struct Player {
	char *name;              /* org.mpris.MediaPlayer2.* */
	int   is_playing;        /* cached */
	struct Player *next;
} Player;

typedef struct WatchEnt {
	DBusWatch *watch;
	int fd;
} WatchEnt;

typedef struct Mpris {
	DBusConnection *conn;

	WatchEnt *watches;
	size_t nwatches;
	size_t cap_watches;

	Player *players;
	unsigned int playing_count;       /* number of players in Playing */
	int verbose;                      /* for logging */
} Mpris;

void mpris_poll(Mpris *m, unsigned int timeout_ms);

int mpris_is_playing(const Mpris *m);

int mpris_init(Mpris *m, int verbose);

int mpris_check_connection(Mpris *m);

#endif /* MPRIS_H */
