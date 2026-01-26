#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "args.h"
#include "utils.h"

enum {
	OPT_LOCK_S = 1000,
	OPT_LOCK_CMD,
	OPT_OFF_S,
	OPT_OFF_CMD,
	OPT_SUSPEND_S,
	OPT_SUSPEND_CMD,
	OPT_POLL_MS,
	OPT_HELP,
};

static void
args_defaults(Options *o)
{
	o->lock_s = 300;
	o->lock_cmd = estrdup("slock");
	o->off_s = 420;
	o->off_cmd = estrdup("xset dpms force off");
	o->suspend_s = 900;
	o->suspend_cmd = estrdup("systemctl suspend");
	o->poll_ms = 500;
}

static int
parse_uint32(uint32_t *n, const char *s)
{
	char *end;
	unsigned long v;

	if (!s || !*s || !n)
		return -1;

	errno = 0;
	v = strtoul(s, &end, 10);

	if (errno != 0 || end == s || *end != '\0' || v > UINT32_MAX)
		return -1;

	*n = (uint32_t)v;
	return 0;
}

static void
usage(void)
{
	fputs("usage: xcoffeebreak [--help][--poll_ms milliseconds]\n"
	      "                    [--lock_s seconds][--lock_cmd cmd]\n"
	      "                    [--off_s seconds][--off_cmd cmd]\n"
	      "                    [--suspend_s seconds][--suspend_cmd cmd]\n"
	      "\n"
	      "--help              Print this message and exit\n"
	      "--poll_ms           Set polling rate in milliseconds\n"
	      "--lock_s            Set locker time in seconds\n"
	      "--lock_cmd          Set locker command\n"
	      "--off_s             Set screen off time in seconds\n"
	      "--off_cmd           Set screen off command\n"
	      "--suspend_s         Set suspend time in seconds\n"
	      "--suspend_cmd       Set suspend command\n"
	      "\n"
	      "Defaults:\n"
	      "  poll_ms     500\n"
	      "  lock_s      300\n"
	      "  lock_cmd    slock\n"
	      "  off_s       420\n"
	      "  off_cmd     xset dpms force off\n"
	      "  suspend_s   900\n"
	      "  suspend_cmd systemctl suspend\n", stderr);

}

static int
args_argv(Options *o, const int argc, char *argv[])
{
	struct option longopts[] = {
		{ "lock_s",      required_argument, 0, OPT_LOCK_S      },
		{ "lock_cmd",    required_argument, 0, OPT_LOCK_CMD    },
		{ "off_s",       required_argument, 0, OPT_OFF_S       },
		{ "off_cmd",     required_argument, 0, OPT_OFF_CMD     },
		{ "suspend_s",   required_argument, 0, OPT_SUSPEND_S   },
		{ "suspend_cmd", required_argument, 0, OPT_SUSPEND_CMD },
		{ "poll_ms",     required_argument, 0, OPT_POLL_MS     },
		{ "help",        no_argument,       0, OPT_HELP        },
		{ 0,             0,                 0, 0               },
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
		switch (opt) {
		case OPT_LOCK_S:
			if (parse_uint32(&(o->lock_s), optarg)) {
				warn("invalid argument for --lock_s");
				return -1;
			}
			break;

		case OPT_LOCK_CMD:
			free(o->lock_cmd);
			o->lock_cmd = estrdup(optarg);
			break;

		case OPT_OFF_S:
			if (parse_uint32(&o->off_s, optarg)) {
				warn("invalid argument for --off_s");
				return -1;
			}
			break;

		case OPT_OFF_CMD:
			free(o->off_cmd);
			o->off_cmd = estrdup(optarg);
			break;

		case OPT_SUSPEND_S:
			if (parse_uint32(&o->suspend_s, optarg)) {
				warn("invalid argument for --suspend_s");
				return -1;
			}
			break;

		case OPT_SUSPEND_CMD:
			free(o->suspend_cmd);
			o->suspend_cmd = estrdup(optarg);
			break;

		case OPT_POLL_MS:
			if (parse_uint32(&o->poll_ms, optarg)) {
				warn("invalid argument for --poll_ms");
				return -1;
			}
			break;

		case OPT_HELP:
			usage();
			exit(0);

		default:
			fputc('\n', stderr);
			usage();
			exit(1);
		}
	}

	return 0;
}

static int
args_validate(Options *o)
{
	if (o->lock_s <= 0 || o->off_s <= 0 || o->suspend_s <= 0) {
		warn("timeouts must be > 0");
		return -1;
	}

	if (!(o->lock_s < o->off_s && o->off_s < o->suspend_s)) {
		warn("require: lock_s < off_s < suspend_s");
		return -1;
	}

	if (o->poll_ms < 50)
		o->poll_ms = 50;

	if (!o->lock_cmd || !o->off_cmd || !o->suspend_cmd) {
		warn("commands are not set properly");
		return -1;
	}

	return 0;
}

int
args_set(Options *o, const int argc, char *argv[])
{
	args_defaults(o);

	if (args_argv(o, argc, argv)) {
		args_free(o);
		return -1;
	}

	if (args_validate(o)) {
		args_free(o);
		return -1;
	}

	return 0;
}

void
args_free(Options *o) {
	free(o->lock_cmd);
	free(o->off_cmd);
	free(o->suspend_cmd);
	memset(o, 0, sizeof(*o));
}
