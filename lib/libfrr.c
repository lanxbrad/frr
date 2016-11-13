/*
 * libfrr overall management functions
 *
 * Copyright (C) 2016  David Lamparter for NetDEF, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <zebra.h>

#include "libfrr.h"
#include "getopt.h"
#include "vty.h"
#include "command.h"
#include "version.h"

static char comb_optstr[256];
static struct option comb_lo[64];
static struct option *comb_next_lo = &comb_lo[0];
static char comb_helpstr[4096];

struct optspec {
	const char *optstr;
	const char *helpstr;
	const struct option *longopts;
};

static void opt_extend(const struct optspec *os)
{
	const struct option *lo;

	strcat(comb_optstr, os->optstr);
	strcat(comb_helpstr, os->helpstr);
	for (lo = os->longopts; lo->name; lo++)
		memcpy(comb_next_lo++, lo, sizeof(*lo));
}


#define OPTION_VTYSOCK 1000

static const struct option lo_always[] = {
	{ "help",    no_argument, NULL, 'h' },
	{ "version", no_argument, NULL, 'v' },
	{ "vty_socket", required_argument, NULL, OPTION_VTYSOCK },
	{ NULL }
};
static const struct optspec os_always = {
	"hv",
	"  -h, --help         Display this help and exit\n"
	"  -v, --version      Print program version\n"
	"      --vty_socket   Override vty socket path\n",
	lo_always
};


static const struct option lo_vty[] = {
	{ "vty_addr",   required_argument, NULL, 'A'},
	{ "vty_port",   required_argument, NULL, 'P'},
	{ NULL }
};
static const struct optspec os_vty = {
	"A:P:",
	"  -A, --vty_addr     Set vty's bind address\n"
	"  -P, --vty_port     Set vty's port number\n",
	lo_vty
};


static const struct option lo_user[] = {
	{ "user",  required_argument, NULL, 'u'},
	{ "group", required_argument, NULL, 'g'},
	{ NULL }
};
static const struct optspec os_user = {
	"u:g:",
	"  -u, --user         User to run as\n"
	"  -g, --group        Group to run as\n",
	lo_user
};


static struct frr_daemon_info *di = NULL;

void frr_preinit(struct frr_daemon_info *daemon, int argc, char **argv)
{
	di = daemon;

	/* basename(), opencoded. */
	char *p = strrchr(argv[0], '/');
	di->progname = p ? p + 1 : argv[0];

	umask(0027);

	opt_extend(&os_always);
	if (!(di->flags & FRR_NO_PRIVSEP))
		opt_extend(&os_user);
	if (!(di->flags & FRR_NO_TCPVTY))
		opt_extend(&os_vty);
}

void frr_opt_add(const char *optstr, const struct option *longopts,
		const char *helpstr)
{
	const struct optspec main_opts = { optstr, helpstr, longopts };
	opt_extend(&main_opts);
}

void frr_help_exit(int status)
{
	FILE *target = status ? stderr : stdout;

	if (status != 0)
		fprintf(stderr, "Invalid options.\n\n");

	if (di->printhelp)
		di->printhelp(target);
	else
		fprintf(target, "Usage: %s [OPTION...]\n\n%s%s%s\n\n%s",
				di->progname,
				di->proghelp,
				di->copyright ? "\n\n" : "",
				di->copyright ? di->copyright : "",
				comb_helpstr);
	fprintf(target, "\nReport bugs to %s\n", FRR_BUG_ADDRESS);
	exit(status);
}

static int errors = 0;

static int frr_opt(int opt)
{
	static int vty_port_set = 0;
	static int vty_addr_set = 0;
	char *err;

	switch (opt) {
	case 'h':
		frr_help_exit(0);
		break;
	case 'v':
		print_version(di->progname);
		exit(0);
		break;
	case 'A':
		if (di->flags & FRR_NO_TCPVTY)
			return 1;
		if (vty_addr_set) {
			fprintf(stderr, "-A option specified more than once!\n");
			errors++;
			break;
		}
		vty_addr_set = 1;
		di->vty_addr = optarg;
		break;
	case 'P':
		if (di->flags & FRR_NO_TCPVTY)
			return 1;
		if (vty_port_set) {
			fprintf(stderr, "-P option specified more than once!\n");
			errors++;
			break;
		}
		vty_port_set = 1;
		di->vty_port = strtoul(optarg, &err, 0);
		if (*err || !*optarg) {
			fprintf(stderr, "invalid port number \"%s\" for -P option\n",
					optarg);
			errors++;
			break;
		}
		break;
	case OPTION_VTYSOCK:
		if (di->vty_sock_path) {
			fprintf(stderr, "--vty_socket option specified more than once!\n");
			errors++;
			break;
		}
		di->vty_sock_path = optarg;
		break;
	case 'u':
		if (di->flags & FRR_NO_PRIVSEP)
			return 1;
		di->privs->user = optarg;
		break;
	case 'g':
		if (di->flags & FRR_NO_PRIVSEP)
			return 1;
		di->privs->group = optarg;
		break;
	default:
		return 1;
	}
	return 0;
}

int frr_getopt(int argc, char * const argv[], int *longindex)
{
	int opt;
	int lidx;

	comb_next_lo->name = NULL;

	do {
		opt = getopt_long(argc, argv, comb_optstr, comb_lo, &lidx);
		if (frr_opt(opt))
			break;
	} while (opt != -1);

	if (opt == -1 && errors)
		frr_help_exit(1);
	if (longindex)
		*longindex = lidx;
	return opt;
}

struct thread_master *frr_init(void)
{
	struct thread_master *master;

	srandom(time(NULL));

	zlog_default = openzlog (di->progname, di->log_id, di->instance,
			LOG_CONS|LOG_NDELAY|LOG_PID, LOG_DAEMON);
#if defined(HAVE_CUMULUS)
	zlog_set_level (NULL, ZLOG_DEST_SYSLOG, zlog_default->default_lvl);
#endif

	zprivs_init(di->privs);

	master = thread_master_create();
	signal_init(master, di->n_signals, di->signals);

	return master;
}

void frr_vty_serv(const char *path)
{
	if (di->vty_sock_path) {
		char newpath[MAXPATHLEN];
		const char *name;
		name = strrchr(path, '/');
		name = name ? name + 1 : path;

		snprintf(newpath, sizeof(newpath), "%s/%s",
				di->vty_sock_path, name);
		vty_serv_sock(di->vty_addr, di->vty_port, newpath);
	} else
		vty_serv_sock(di->vty_addr, di->vty_port, path);
}

