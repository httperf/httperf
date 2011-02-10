/*
 * Copyright (C) 2007, 2008 Ted Bullock <tbullock@comlore.com>
 * 
 * This file is part of httperf, a web server performance measurment tool.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * In addition, as a special exception, the copyright holders give permission
 * to link the code of this work with the OpenSSL project's "OpenSSL" library
 * (or with modified versions of it that use the same license as the "OpenSSL" 
 * library), and distribute linked combinations including the two.  You must
 * obey the GNU General Public License in all respects for all of the code
 * used other than "OpenSSL".  If you modify this file, you may extend this
 * exception to your version of the file, but you are not obligated to do so.
 * If you do not wish to do so, delete this exception statement from your
 * version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT 
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA 
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>		/* For strrchr() */
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>

#include <generic_types.h>
#include <event.h>
#include <evdns.h>

#include <list.h>

/*
 * Events allocated onto the heap
 */
static struct List *active_events = NULL;

static const char *prog_name = NULL;
static unsigned long num_conn = 0, num_closed = 0;
static struct timeval start_time;
static struct sockaddr_in server_addr;

static char    *server = NULL;
static int      desired = 0;	/* Number of desired connections */
static int      port = 0;

/*
 * Frees the linked list of active event structures
 */
static void
cleanup()
{
	if (active_events != NULL) {
		while (!is_list_empty(active_events)) {
			Any_Type        a = list_pop(active_events);
			struct event   *evsock = (struct event *) a.vp;
			event_del(evsock);
			free(a.vp);
		}
		list_free(active_events);
	}
}

/*
 * Signal handler callback to be executed by event_dispatch upon receipt of
 * SIGINT (usually Control-C
 */
void
sigint_exit(int fd, short event, void *arg)
{
	struct event   *signal_int = arg;
	struct timeval  stop_time;
	double          delta_t = 0;

	gettimeofday(&stop_time, NULL);

	delta_t = ((stop_time.tv_sec - start_time.tv_sec)
		   + 1e-6 * (stop_time.tv_usec - start_time.tv_usec));

	printf("%s: Total conns created = %lu; close() rate = %g conn/sec\n",
	       prog_name, num_conn, num_closed / delta_t);

#ifdef DEBUG
	printf("%s: caught SIGINT... Exiting.\n", __func__);
#endif /* DEBUG */

	evdns_shutdown(0);
	event_del(signal_int);

	cleanup();
}

/*
 * Connection disconnect handler.  Once a connection is dropped by the remote
 * host, this function is executed and a new connection is established.
 *
 * Note, that this re-uses the event structure originally allocated in 
 * dns_lookup_callback
 */
void
reconnect(int sd, short event, void *arg)
{
	struct sockaddr_in sin = server_addr;
	struct event   *evsock = (struct event *) arg;

	close(sd);
	num_closed++;

	sd = socket(AF_INET, SOCK_STREAM, 0);

	if (sd == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	if (connect(sd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
		perror("connect");
		cleanup();
		exit(EXIT_FAILURE);
	}

	event_set(evsock, sd, EV_READ, reconnect, evsock);
	event_add(evsock, NULL);
	num_conn++;

}

/*
 * For the explanation of these parameters, please refer to the libevent evdns
 * callback API
 *
 * Upon receipt of a valid dns lookup result, attempts to open `desired`
 * connections and allocates memory for the associated event structures
 */
void
dns_lookup_callback(int result, char type, int count, int ttl, void *addresses,
		    void *arg)
{
	uint32_t        i;
	uint8_t         oct[4];
	struct in_addr *address_list = (struct in_addr *) addresses;

	if (result != DNS_ERR_NONE) {
		printf("DNS Lookup: result(%s)\n",
		       evdns_err_to_string(result));
		exit(EXIT_FAILURE);
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr = address_list[0];

	/*
	 * Echo the resolved address 
	 */
	printf("Resolved %s\n\t(%s)\n", server, inet_ntoa(address_list[0]));

	/*
	 * Open the number of `desired` connections 
	 */
	for (i = 0; i < desired; i++) {
		struct sockaddr_in sin;
		int             sd = socket(AF_INET, SOCK_STREAM, 0);

		struct event   *evsock = NULL;

		if (sd == -1) {
			perror("socket");
			continue;
		}

		sin = server_addr;

		if (connect(sd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
			perror("connect");
			cleanup();
			exit(EXIT_FAILURE);
		}

		evsock = (struct event *) malloc(sizeof(struct event));

		if (evsock == NULL) {
			cleanup();
			exit(EXIT_FAILURE);
		}

		event_set(evsock, sd, EV_READ, reconnect, evsock);
		list_push(active_events, (Any_Type) (void *) evsock);
		num_conn++;

		event_add(evsock, NULL);

	}
}

int
main(int argc, char *argv[])
{
	struct event    signal_int;

	active_events = list_create();
	if (active_events == NULL)
		goto init_failure;

	event_init();
	evdns_init();

	/*
	 * Initalize one event 
	 */
	event_set(&signal_int, SIGINT, EV_SIGNAL, sigint_exit, &signal_int);

	event_add(&signal_int, NULL);

	prog_name = strrchr(argv[0], '/');
	if (prog_name)
		++prog_name;
	else
		prog_name = argv[0];

	if (argc != 4) {
		fprintf(stderr, "Usage: `%s server port numidle'\n",
			prog_name);
		goto init_failure;
	}

	server = argv[1];
	port = atoi(argv[2]);
	desired = atoi(argv[3]);

	printf("%s: Using libevent-%s for %s event notification system.\n"
	       "Control-c to exit\n\n", prog_name, event_get_version(),
	       event_get_method());

	gettimeofday(&start_time, NULL);

	evdns_resolve_ipv4(server, 0, dns_lookup_callback, NULL);

	event_dispatch();

	/*
	 * Event loop will only exit upon receiving SIGINT.  Make sure we pass
	 * this on to the parent process 
	 */
	if (signal(SIGINT, SIG_DFL) < 0)
		perror("signal");
	else
		kill(getpid(), SIGINT);

      init_failure:
	cleanup();
	exit(EXIT_FAILURE);

	/*
	 * Should never reach here 
	 */
	return 0;
}
