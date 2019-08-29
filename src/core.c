/*
 * Copyright (C) 2000-2007 Hewlett-Packard Company
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

#ifdef __FreeBSD__
#define	HAVE_KEVENT
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_EPOLL
#include <sys/epoll.h>
#endif

#ifdef HAVE_KEVENT
#include <sys/event.h>

/*
 * Older systems using kevent() always specify the time in
 * milliseconds and do not have a flag to select a different scale.
 */
#ifndef NOTE_MSECONDS
#define	NOTE_MSECONDS		0
#endif
#endif

#ifdef __FreeBSD__
#include <ifaddrs.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <generic_types.h>
#include <sys/resource.h>	/* after sys/types.h for BSD (in generic_types.h) */

#include <object.h>
#include <timer.h>
#include <httperf.h>
#include <call.h>
#include <conn.h>
#include <core.h>
#include <localevent.h>
#include <http.h>

#define HASH_TABLE_SIZE	1024	/* can't have more than this many servers */
#define MIN_IP_PORT	IPPORT_RESERVED
#define MAX_IP_PORT	65535
#define BITSPERLONG	(8*sizeof (u_long))

struct local_addr {
	struct in_addr ip;
	u_long port_free_map[((MAX_IP_PORT - MIN_IP_PORT + BITSPERLONG)
		    / BITSPERLONG)];
	u_long mask;
	int previous;
};

struct address_pool {
	struct local_addr *addresses;
	int count;
	int last;
};

static volatile int      running = 1;
static int      iteration;
static u_long   max_burst_len;
#ifdef HAVE_KEVENT
static int	kq, max_sd = 0;
#else
#ifdef HAVE_EPOLL
#define	EPOLL_N_MAX		8192
static int epoll_fd, max_sd = 0;
static struct epoll_event *epoll_events;
static int epoll_timeout;
#else
static fd_set   rdfds, wrfds;
static int      min_sd = 0x7fffffff, max_sd = 0, alloced_sd_to_conn = 0;
static struct timeval select_timeout;
#endif
#endif
static struct sockaddr_in myaddr;
static struct address_pool myaddrs;
#ifndef HAVE_KEVENT
Conn          **sd_to_conn;
#endif
static char     http10req[] =
    " HTTP/1.0\r\nUser-Agent: httperf/" VERSION
    "\r\nConnection: keep-alive\r\nHost: ";
static char     http11req[] =
    " HTTP/1.1\r\nUser-Agent: httperf/" VERSION "\r\nHost: ";

static char     http10req_nohost[] =
    " HTTP/1.0\r\nUser-Agent: httperf/" VERSION
    "\r\nConnection: keep-alive";
static char     http11req_nohost[] =
    " HTTP/1.1\r\nUser-Agent: httperf/" VERSION;

#ifndef SOL_TCP
# define SOL_TCP 6		/* probably ought to do getprotlbyname () */
#endif

#ifdef TIME_SYSCALLS
# define SYSCALL(n,s)							\
  {									\
    Time start, stop;							\
    do									\
      {									\
	errno = 0;							\
	start = timer_now_forced ();					\
	s;				 /* execute the syscall */	\
	stop = timer_now_forced ();					\
	syscall_time[SC_##n] += stop - start;				\
	++syscall_count[SC_##n];					\
      }									\
    while (errno == EINTR);						\
  }

enum Syscalls {
	SC_BIND, SC_CONNECT, SC_READ, SC_SELECT, SC_SOCKET, SC_WRITEV,
	SC_SSL_READ, SC_SSL_WRITEV, SC_KEVENT,
	SC_EPOLL_CREATE, SC_EPOLL_CTL, SC_EPOLL_WAIT,
	SC_NUM_SYSCALLS
};

static const char *const syscall_name[SC_NUM_SYSCALLS] = {
	"bind", "connct", "read", "select", "socket", "writev",
	"ssl_read", "ssl_writev", "kevent",
	"epoll_create", "epoll_ctl", "epoll_wait"
};
static Time     syscall_time[SC_NUM_SYSCALLS];
static u_int    syscall_count[SC_NUM_SYSCALLS];
#else
# define SYSCALL(n,s)				\
  {						\
    do						\
      {						\
	errno = 0;				\
	s;					\
      }						\
    while (errno == EINTR);			\
  }
#endif

struct hash_entry {
	const char     *hostname;
	int             port;
	struct sockaddr_in sin;
} hash_table[HASH_TABLE_SIZE];

static int
hash_code(const char *server, size_t server_len, int port)
{
	u_char         *cp = (u_char *) server;
	u_long          h = port;
	u_long          g;
	int             ch;

	/*
	 * Basically the ELF hash algorithm: 
	 */

	while ((ch = *cp++) != '\0') {
		h = (h << 4) + ch;
		if ((g = (h & 0xf0000000)) != 0) {
			h ^= g >> 24;
			h &= ~g;
		}
	}
	return h;
}

static struct hash_entry *
hash_enter(const char *server, size_t server_len, int port,
	   struct sockaddr_in *sin)
{
	struct hash_entry *he;

	int             index =
	    hash_code(server, server_len, port) % HASH_TABLE_SIZE;

	while (hash_table[index].hostname) {
		++index;
		if (index >= HASH_TABLE_SIZE)
			index = 0;
	}
	he = hash_table + index;
	he->hostname = server;
	he->port = port;
	he->sin = *sin;
	return he;
}

static struct sockaddr_in *
hash_lookup(const char *server, size_t server_len, int port)
{
	int             index, start_index;

	index = start_index =
	    hash_code(server, server_len, port) % HASH_TABLE_SIZE;
	while (hash_table[index].hostname) {
		if (hash_table[index].port == port
		    && strcmp(hash_table[index].hostname, server) == 0)
			return &hash_table[index].sin;

		++index;
		if (index >= HASH_TABLE_SIZE)
			index = 0;
		if (index == start_index)
			break;
	}
	return 0;
}

static int
lffs(long w)
{
#ifdef __FreeBSD__
	return ffsl(w);
#else
	int             r;

	if (sizeof(w) == sizeof(int))
		r = ffs(w);
	else {
		r = ffs(w);
#if SIZEOF_LONG > 4
		if (r == 0) {
			r = ffs(w >> (8 * sizeof(int)));
			if (r > 0)
				r += 8 * sizeof(int);
		}
#endif
	}
	return r;
#endif
}

static void
port_put(struct local_addr *addr, int port)
{
	int             i, bit;

	port -= MIN_IP_PORT;
	i = port / BITSPERLONG;
	bit = port % BITSPERLONG;
	addr->port_free_map[i] |= (1UL << bit);
}

static int
port_get(struct local_addr *addr)
{
	int             port, bit, i;

	i = addr->previous;
	if ((addr->port_free_map[i] & addr->mask) == 0) {
		do {
			++i;
			if (i >= NELEMS(addr->port_free_map))
				i = 0;
			if (i == addr->previous) {
				if (DBG > 0)
					fprintf(stderr,
						"%s.port_get: Yikes! I'm out of port numbers!\n",
						prog_name);
				return -1;
			}
		}
		while (addr->port_free_map[i] == 0);
		addr->mask = ~0UL;
	}
	addr->previous = i;

	bit = lffs(addr->port_free_map[i] & addr->mask) - 1;
	if (bit >= BITSPERLONG - 1)
		addr->mask = 0;
	else
		addr->mask = ~((1UL << (bit + 1)) - 1);
	addr->port_free_map[i] &= ~(1UL << bit);
	port = bit + i * BITSPERLONG + MIN_IP_PORT;
	return port;
}

static void
conn_failure(Conn * s, int err)
{
	Any_Type        arg;

	arg.l = err;
	event_signal(EV_CONN_FAILED, (Object *) s, arg);

	core_close(s);
}

static void
conn_timeout(struct Timer *t, Any_Type arg)
{
	Conn           *s = arg.vp;
	Time            now;
	Call           *c;

	assert(object_is_conn(s));
	s->watchdog = 0;

	if (DBG > 0) {
		c = 0;
		if (s->sd >= 0) {
			now = timer_now();
			if (s->reading
				&& s->recvq && now >= s->recvq->timeout)
				c = s->recvq;
			else if (s->writing
				&& s->sendq && now >= s->sendq->timeout)
				c = s->sendq;
		}
		if (DBG > 0) {
			fprintf(stderr, "connection_timeout");
			if (c)
				fprintf(stderr, ".%lu", c->id);
			fprintf(stderr, ": t=%p, connection=%p\n", t, s);
		}
	}

	arg.l = 0;
	event_signal(EV_CONN_TIMEOUT, (Object *) s, arg);

	core_close(s);
}

enum IO_DIR { READ, WRITE };

static void
clear_active(Conn * s, enum IO_DIR dir)
{
 	int             sd = s->sd;
#ifdef HAVE_KEVENT
	struct kevent	ev;

	EV_SET(&ev, sd, dir == WRITE ? EVFILT_WRITE : EVFILT_READ, EV_DELETE,
	    0, 0, s);
	if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0) {
		fprintf(stderr, "failed to add %s filter\n", write ?
		    "write" : "read");
		exit(1);
	}
#else
#ifdef HAVE_EPOLL
	struct epoll_event ev;
	int error;

	if (dir == WRITE)
		ev.events = EPOLLIN;
	else
		ev.events = EPOLLOUT;
	ev.data.ptr = s;

	error = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sd, &ev);
	if (error < 0) {
		error = errno;
		fprintf(stderr, "failed to EPOLL_CTL_DEL\n");
		exit(1);
	}
#else
	fd_set *	fdset;
	
	if (dir == WRITE)
		fdset = &wrfds;
	else
		fdset = &rdfds;
	FD_CLR(sd, fdset);
#endif
#endif
	if (dir == WRITE)
		s->writing = 0;
	else
		s->reading = 0;
}

static void
set_active(Conn * s, enum IO_DIR dir)
{
 	int             sd = s->sd;
	Any_Type        arg;
	Time            timeout;
#ifdef HAVE_KEVENT
	struct kevent	ev;

	EV_SET(&ev, sd, dir == WRITE ? EVFILT_WRITE : EVFILT_READ, EV_ADD,
	    0, 0, s);
	if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0) {
		fprintf(stderr, "failed to add %s filter\n", write ?
		    "write" : "read");
		exit(1);
	}
#else
#ifdef HAVE_EPOLL
	struct epoll_event ev;
	int error;

	if (dir == WRITE)
		ev.events = EPOLLOUT;
	else
		ev.events = EPOLLIN;
	ev.data.ptr = s;

	if (s->epoll_added)
		error = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sd, &ev);
	else {
		error = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sd, &ev);
		s->epoll_added = 1;
	}
	if (error < 0) {
		error = errno;
		fprintf(stderr, "failed to EPOLL_CTL_MOD\n");
		exit(1);
	}
#else
	fd_set *	fdset;
	
	if (dir == WRITE)
		fdset = &wrfds;
	else
		fdset = &rdfds;
	FD_SET(sd, fdset);
	if (sd < min_sd)
		min_sd = sd;
#endif
#endif
	if (sd >= max_sd)
		max_sd = sd;
	if (dir == WRITE)
		s->writing = 1;
	else
		s->reading = 1;

	if (s->watchdog)
		return;

	timeout = 0.0;
	if (s->sendq)
		timeout = s->sendq->timeout;
	if (s->recvq && (timeout == 0.0 || timeout > s->recvq->timeout))
		timeout = s->recvq->timeout;

	if (timeout > 0.0) {
		arg.vp = s;
		s->watchdog = timer_schedule(conn_timeout, arg,
					     timeout - timer_now());
	}
}

static void
do_send(Conn * conn)
{
	int             async_errno;
	socklen_t       len;
	struct iovec   *iovp;
	int             sd = conn->sd;
	ssize_t         nsent = 0;
	Any_Type        arg;
	Call           *call;

	while (1) {
		call = conn->sendq;
		assert(call);

		arg.l = 0;
		event_signal(EV_CALL_SEND_RAW_DATA, (Object *) call, arg);

#ifdef HAVE_SSL
		if (param.use_ssl) {
			extern ssize_t  SSL_writev(SSL *, const struct iovec *,
						   int);
			SYSCALL(SSL_WRITEV, nsent =
				SSL_writev(conn->ssl,
					   call->req.iov + call->req.iov_index,
					   (NELEMS(call->req.iov)
					    - call->req.iov_index)));
		} else
#endif
		{
			SYSCALL(WRITEV,
				nsent =
				writev(sd, call->req.iov + call->req.iov_index,
				       (NELEMS(call->req.iov)
					- call->req.iov_index)));
		}

		if (DBG > 0)
			fprintf(stderr, "do_send.%lu: wrote %ld bytes on %p\n",
				call->id, (long) nsent, conn);

		if (nsent < 0) {
			if (errno == EAGAIN)
				return;

			len = sizeof(async_errno);
			if (getsockopt
			    (sd, SOL_SOCKET, SO_ERROR, &async_errno, &len) == 0
			    && async_errno != 0)
				errno = async_errno;

			if (DBG > 0)
				fprintf(stderr,
					"%s.do_send: writev() failed: %s\n",
					prog_name, strerror(errno));

			conn_failure(conn, errno);
			return;
		}

		call->req.size += nsent;

		iovp = call->req.iov + call->req.iov_index;
		while (iovp < call->req.iov + NELEMS(call->req.iov)) {
			if (nsent < iovp->iov_len) {
				iovp->iov_len -= nsent;
				iovp->iov_base =
				    (caddr_t) ((char *) iovp->iov_base +
					       nsent);
				break;
			} else {
				/*
				 * we're done with this fragment: 
				 */
				nsent -= iovp->iov_len;
				*iovp = call->req.iov_saved;
				++iovp;
				call->req.iov_saved = *iovp;
			}
		}
		call->req.iov_index = iovp - call->req.iov;
		if (call->req.iov_index < NELEMS(call->req.iov)) {
			/*
			 * there are more header bytes to write 
			 */
			call->timeout =
			    param.timeout ? timer_now() + param.timeout : 0.0;
			set_active(conn, WRITE);
			return;
		}

		/*
		 * we're done with sending this request 
		 */
		conn->sendq = call->sendq_next;
		if (!conn->sendq) {
			conn->sendq_tail = 0;
			clear_active(conn, WRITE);
		}
		arg.l = 0;
		event_signal(EV_CALL_SEND_STOP, (Object *) call, arg);
		if (conn->state >= S_CLOSING) {
			call_dec_ref(call);
			return;
		}

		/*
		 * get ready to receive matching reply (note that we
		 * implicitly pass on the reference to the call from the sendq 
		 * to the recvq): 
		 */
		call->recvq_next = 0;
		if (!conn->recvq)
			conn->recvq = conn->recvq_tail = call;
		else {
			conn->recvq_tail->recvq_next = call;
			conn->recvq_tail = call;
		}
		call->timeout = param.timeout + param.think_timeout;
		if (call->timeout > 0.0)
			call->timeout += timer_now();
		set_active(conn, READ);
		if (conn->state < S_REPLY_STATUS)
			conn->state = S_REPLY_STATUS;	/* expecting reply
							 * status */

		if (!conn->sendq)
			return;

		arg.l = 0;
		event_signal(EV_CALL_SEND_START, (Object *) conn->sendq, arg);
		if (conn->state >= S_CLOSING)
			return;
	}
}

static void
recv_done(Call * call)
{
	Conn           *conn = call->conn;
	Any_Type        arg;

	conn->recvq = call->recvq_next;
	if (!conn->recvq) {
		clear_active(conn, READ);
		conn->recvq_tail = 0;
	}
	/*
	 * we're done with receiving this request 
	 */
	arg.l = 0;
	event_signal(EV_CALL_RECV_STOP, (Object *) call, arg);

	call_dec_ref(call);
}

static void
do_recv(Conn * s)
{
	char           *cp, buf[8193];
	Call           *c = s->recvq;
	int             i, saved_errno;
	ssize_t         nread = 0;
	size_t          buf_len;

	assert(c);

#ifdef HAVE_SSL
	if (param.use_ssl) {
		SYSCALL(SSL_READ,
			nread = SSL_read(s->ssl, buf, sizeof(buf) - 1));
	} else
#endif
	{
		SYSCALL(READ, nread = read(s->sd, buf, sizeof(buf) - 1));
	}
	saved_errno = errno;
	if (nread <= 0) {
		if (DBG > 0) {
			fprintf(stderr,
				"do_recv.%lu: received %lu reply bytes on %p\n",
				c->id,
				(u_long) (c->reply.header_bytes +
					  c->reply.content_bytes), s);
			if (nread < 0)
				fprintf(stderr,
					"%s.do_recv: read() failed: %s\n",
					prog_name, strerror(saved_errno));
		}
		if (nread < 0) {
			if (saved_errno != EAGAIN)
				conn_failure(s, saved_errno);
		} else if (s->state != S_REPLY_DATA)
			conn_failure(s, ECONNRESET);
		else {
			if (s->state < S_CLOSING)
				s->state = S_REPLY_DONE;
			recv_done(c);
		}
		return;
	}
	buf[nread] = '\0';	/* ensure buffer is '\0' terminated */

	if (DBG > 3) {
		/*
		 * dump received data in hex & ascii: 
		 */

		fprintf(stderr, "do_recv.%lu: received reply data:\n", c->id);
		for (cp = buf; cp < buf + nread;) {
			fprintf(stderr, "  %04x:",
				(int) (c->reply.header_bytes +
				       c->reply.content_bytes + (cp - buf)));
			for (i = 0; i < 16 && i < buf + nread - cp; ++i)
				fprintf(stderr, " %02x", cp[i] & 0xff);
			i *= 3;
			while (i++ < 50)
				fputc(' ', stderr);
			for (i = 0; i < 16 && cp < buf + nread; ++i, ++cp)
				fprintf(stderr, "%c",
					isprint(*cp) ? *cp : '.');
			fprintf(stderr, "\n");
		}
	}

	/*
	 * process the replies in this buffer: 
	 */

	buf_len = nread;
	cp = buf;
	do {
		c = s->recvq;
		assert(c);
		/*
		 * sets right start time, but doesn't update each packet 
		 */
		if (s->state == S_REPLY_STATUS) {
			c->timeout = param.timeout + param.think_timeout;
			if (c->timeout > 0.0)
				c->timeout += timer_now();
		}

		http_process_reply_bytes(c, &cp, &buf_len);
		if (s->state == S_REPLY_DONE) {
			recv_done(c);
			if (s->state >= S_CLOSING)
				return;

			s->state = S_REPLY_STATUS;
		}
	}
	while (buf_len > 0);

	if (s->recvq)
		set_active(c->conn, READ);
}

struct sockaddr_in *
core_addr_intern(const char *server, size_t server_len, int port)
{
	struct sockaddr_in sin;
	struct hash_entry *h;
	struct hostent *he;
	Any_Type        arg;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);

	arg.cvp = server;
	event_signal(EV_HOSTNAME_LOOKUP_START, 0, arg);
	he = gethostbyname(server);
	event_signal(EV_HOSTNAME_LOOKUP_STOP, 0, arg);
	if (he) {
		if (he->h_addrtype != AF_INET
		    || he->h_length != sizeof(sin.sin_addr)) {
			fprintf(stderr,
				"%s: can't deal with addr family %d or size %d\n",
				prog_name, he->h_addrtype, he->h_length);
			exit(1);
		}
		memcpy(&sin.sin_addr, he->h_addr_list[0],
		       sizeof(sin.sin_addr));
	} else {
		if (!inet_aton(server, &sin.sin_addr)) {
			fprintf(stderr,
				"%s.core_addr_intern: invalid server address %s\n",
				prog_name, server);
			exit(1);
		}
	}
	h = hash_enter(server, server_len, port, &sin);
	if (!h)
		return 0;
	return &h->sin;
}

static void
core_add_address(struct in_addr ip)
{
	struct local_addr *addr;

	myaddrs.addresses = realloc(myaddrs.addresses,
	    sizeof(struct local_addr) * (myaddrs.count + 1));
	if (myaddrs.addresses == NULL) {
		fprintf(stderr,
			"%s: out of memory parsing address list\n",
			prog_name);
		exit(1);
	}
	addr = &myaddrs.addresses[myaddrs.count];
	addr->ip = ip;
	memset(&addr->port_free_map, 0xff, sizeof(addr->port_free_map));
	addr->mask = ~0UL;
	addr->previous = 0;
	myaddrs.count++;
}

/*
 * Parses the value provided to --myaddr.  A value can either be a
 * hostname or IP, or an IP range.  Multiple values can be specified
 * in which case all matches are added to a pool which new connections
 * use in a round-robin fashion.  An interface name may also be
 * specified in which case all IP addresses assigned to that interface
 * are used.
 */
void
core_add_addresses(const char *spec)
{
	struct hostent *he;
	struct in_addr ip;
#ifdef __FreeBSD__
	struct ifaddrs *iflist, *ifa;
#endif
	char *cp;

	/* First try to resolve the argument as a hostname. */
	he = gethostbyname(spec);
	if (he) {
		if (he->h_addrtype != AF_INET ||
		    he->h_length != sizeof(struct in_addr)) {
			fprintf(stderr,
				"%s: can't deal with addr family %d or size %d\n",
				prog_name, he->h_addrtype, he->h_length);
			exit(1);
		}
		core_add_address(*(struct in_addr *)he->h_addr_list[0]);
		return;
	}

	/* If there seems to be an IP range, try that next. */
	cp = strchr(spec, '-');
	if (cp != NULL) {
		char *start_s;
		struct in_addr end_ip;

		start_s = strndup(spec, cp - spec);
		if (!inet_aton(start_s, &ip)) {
			fprintf(stderr, "%s: invalid starting address %s\n",
			    prog_name, start_s);
			exit(1);
		}
		if (!inet_aton(cp + 1, &end_ip)) {
			fprintf(stderr, "%s: invalid ending address %s\n",
			    prog_name, cp + 1);
			exit(1);
		}

		while (ip.s_addr != end_ip.s_addr) {
			core_add_address(ip);
			ip.s_addr += htonl(1);
		}
		core_add_address(end_ip);
		return;
	}

	/* Check for a single IP. */
	if (inet_aton(spec, &ip)) {
		core_add_address(ip);
		return;
	}

#ifdef __FreeBSD__
	/* Check for an interface name. */
	if (getifaddrs(&iflist) == 0) {
		int found;

		found = 0;
		for (ifa = iflist; ifa != NULL; ifa = ifa->ifa_next) {
			if (strcmp(ifa->ifa_name, spec) != 0)
				continue;
			if (found == 0)
				found = 1;
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			found = 2;
			core_add_address(
			    ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr);
		}
		freeifaddrs(iflist);
		if (found == 2)
			return;
		if (found == 1) {
			fprintf(stderr,
			    "%s: no valid addresses found on interface %s\n",
			    prog_name, spec);
			exit(1);
		}
	}
#endif

	fprintf(stderr, "%s: invalid address list %s\n",
	    prog_name, spec);
	exit(1);
}

static struct local_addr *
core_get_next_myaddr(void)
{
	struct local_addr *addr;

	assert(myaddrs.last >= 0 && myaddrs.last < myaddrs.count);
	addr = &myaddrs.addresses[myaddrs.last];
	myaddrs.last++;
	if (myaddrs.last == myaddrs.count)
		myaddrs.last = 0;
	return (addr);
}

static void
core_runtime_timer(struct Timer *t, Any_Type arg)
{

	core_exit();
}

void
core_init(void)
{
	struct rlimit   rlimit;
	Any_Type        arg;

	memset(&hash_table, 0, sizeof(hash_table));
#if !defined(HAVE_KEVENT) && !defined(HAVE_EPOLL)
	memset(&rdfds, 0, sizeof(rdfds));
	memset(&wrfds, 0, sizeof(wrfds));
#endif
	memset(&myaddr, 0, sizeof(myaddr));
#ifdef __FreeBSD__
	myaddr.sin_len = sizeof(myaddr);
#endif
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (myaddrs.count == 0)
		core_add_address(myaddr.sin_addr);

	/*
	 * Don't disturb just because a TCP connection closed on us... 
	 */
	signal(SIGPIPE, SIG_IGN);

#ifdef HAVE_KEVENT
	kq = kqueue();
	if (kq < 0) {
		fprintf(stderr,
		    "%s: failed to create kqueue: %s", prog_name,
		    strerror(errno));
		exit(1);
	}

	/*
	 * TIMER_INTERVAL doesn't exist anymore, so just take a wild
	 * guess.
	 */
	struct kevent ev;
	EV_SET(&ev, 0, EVFILT_TIMER, EV_ADD, NOTE_MSECONDS, 1, NULL);
	if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0) {
		fprintf(stderr,
		    "%s: failed to add timer event: %s", prog_name,
		    strerror(errno));
	}	
#else
#ifdef HAVE_EPOLL
	epoll_fd = epoll_create(EPOLL_N_MAX);
	if (epoll_fd < 0) {
		fprintf(stderr,
		    "%s: failed to create epoll: %s", prog_name,
		    strerror(errno));
		exit(1);
	}
	epoll_events = calloc(EPOLL_N_MAX, sizeof(struct epoll_event));
	if (epoll_events == NULL) {
		fprintf(stderr,
		    "%s: failed to create epoll_events: %s", prog_name,
		    strerror(errno));
		exit(1);
	}
	epoll_timeout = 0;
#else
#ifdef DONT_POLL
	/*
	 * This causes select() to take several milliseconds on both Linux/x86 
	 * and HP-UX 10.20.  
	 */
	select_timeout.tv_sec = (u_long) TIMER_INTERVAL;
	select_timeout.tv_usec = (u_long) (TIMER_INTERVAL * 1e6);
#else
	/*
	 * This causes httperf to become a CPU hog as it polls for
	 * filedescriptors to become readable/writable.  This is OK as long as 
	 * httperf is the only (interesting) user-level process that executes
	 * on a machine.  
	 */
	select_timeout.tv_sec = 0;
	select_timeout.tv_usec = 0;
#endif
#endif
#endif

	/*
	 * boost open file limit to the max: 
	 */
	if (getrlimit(RLIMIT_NOFILE, &rlimit) < 0) {
		fprintf(stderr,
			"%s: failed to get number of open file limit: %s",
			prog_name, strerror(errno));
		exit(1);
	}

	rlimit.rlim_cur = rlimit.rlim_max;
	if (setrlimit(RLIMIT_NOFILE, &rlimit) < 0) {
		fprintf(stderr,
			"%s: failed to increase number of open file limit: %s",
			prog_name, strerror(errno));
		exit(1);
	}

	if (verbose)
		printf("%s: maximum number of open descriptors = %ld\n",
		       prog_name, rlimit.rlim_max);

	if (param.servers)
		conn_add_servers();
	else if (param.server)
		core_addr_intern(param.server, strlen(param.server), param.port);

	if (param.runtime) {
		arg.l = 0;
		timer_schedule(core_runtime_timer, arg, param.runtime);
	}
}

#ifdef HAVE_SSL

void
core_ssl_connect(Conn * s)
{
	Any_Type        arg;
	int             ssl_err;

	if (DBG > 2)
		fprintf(stderr, "core_ssl_connect(conn=%p)\n", (void *) s);

	if (SSL_set_fd(s->ssl, s->sd) == 0) {
		ERR_print_errors_fp(stderr);
		exit(-1);
	}

	ssl_err = SSL_connect(s->ssl);
	if (ssl_err < 0) {
		int             reason = SSL_get_error(s->ssl, ssl_err);

		if (reason == SSL_ERROR_WANT_READ
		    || reason == SSL_ERROR_WANT_WRITE) {
			if (DBG > 2)
				fprintf(stderr,
					"core_ssl_connect: want to %s more...\n",
					(reason ==
					 SSL_ERROR_WANT_READ) ? "read" :
					"write");
			if (reason == SSL_ERROR_WANT_READ
			    && !s->reading) {
				clear_active(s, WRITE);
				set_active(s, READ);
			} else if (reason == SSL_ERROR_WANT_WRITE
				   && !s->writing) {
				clear_active(s, READ);
				set_active(s, WRITE);
			}
			return;
		}
		fprintf(stderr,
			"%s: failed to connect to SSL server (err=%d, reason=%d)\n",
			prog_name, ssl_err, reason);
		ERR_print_errors_fp(stderr);
		exit(-1);
	}

	s->state = S_CONNECTED;

	if (DBG > 0)
		fprintf(stderr, "core_ssl_connect: SSL is connected!\n");

	if (DBG > 1) {
		const SSL_CIPHER     *ssl_cipher;

		ssl_cipher = SSL_get_current_cipher(s->ssl);
		if (!ssl_cipher)
			fprintf(stderr,
				"core_ssl_connect: server refused all client cipher "
				"suites!\n");
		else
			fprintf(stderr,
				"core_ssl_connect: cipher=%s, id=%lu\n",
				SSL_CIPHER_get_name(ssl_cipher),
				SSL_CIPHER_get_id(ssl_cipher));
	}

	arg.l = 0;
	event_signal(EV_CONN_CONNECTED, (Object *) s, arg);
}

#endif /* HAVE_SSL */

int
core_connect(Conn * s)
{
	int             sd, result, async_errno;
	socklen_t       len;
	struct sockaddr_in *sin;
	struct linger   linger;
	int             myport, optval;
	Any_Type        arg;
	static int      prev_iteration = -1;
	static u_long   burst_len;

	if (iteration == prev_iteration)
		++burst_len;
	else {
		if (burst_len > max_burst_len)
			max_burst_len = burst_len;
		burst_len = 1;
		prev_iteration = iteration;
	}

	SYSCALL(SOCKET, sd = socket(AF_INET, SOCK_STREAM, 0));
	if (sd < 0) {
		if (DBG > 0)
			fprintf(stderr,
				"%s.core_connect.socket: %s (max_sd=%d)\n",
				prog_name, strerror(errno), max_sd);
		goto failure;
	}

	if (fcntl(sd, F_SETFL, O_NONBLOCK) < 0) {
		fprintf(stderr, "%s.core_connect.fcntl: %s\n",
			prog_name, strerror(errno));
		goto failure;
	}

	if (param.close_with_reset) {
		linger.l_onoff = 1;
		linger.l_linger = 0;
		if (setsockopt
		    (sd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger)) < 0) {
			fprintf(stderr,
				"%s.core_connect.setsockopt(SO_LINGER): %s\n",
				prog_name, strerror(errno));
			goto failure;
		}
	}

	/*
	 * Disable Nagle algorithm so we don't delay needlessly when
	 * pipelining requests.  
	 */
	optval = 1;
	if (setsockopt(sd, SOL_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0) {
		fprintf(stderr, "%s.core_connect.setsockopt(SO_SNDBUF): %s\n",
			prog_name, strerror(errno));
		goto failure;
	}

	optval = param.send_buffer_size;
	if (setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval)) < 0) {
		fprintf(stderr, "%s.core_connect.setsockopt(SO_SNDBUF): %s\n",
			prog_name, strerror(errno));
		goto failure;
	}

	optval = param.recv_buffer_size;
	if (setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval)) < 0) {
		fprintf(stderr, "%s.core_connect.setsockopt(SO_SNDBUF): %s\n",
			prog_name, strerror(errno));
		goto failure;
	}

	s->sd = sd;
#if !defined(HAVE_KEVENT) && !defined(HAVE_EPOLL)
	if (sd >= alloced_sd_to_conn) {
		size_t          size, old_size;

		old_size = alloced_sd_to_conn * sizeof(sd_to_conn[0]);
		alloced_sd_to_conn += 2048;
		size = alloced_sd_to_conn * sizeof(sd_to_conn[0]);
		if (sd_to_conn)
			sd_to_conn = realloc(sd_to_conn, size);
		else
			sd_to_conn = malloc(size);
		if (!sd_to_conn) {
			if (DBG > 0)
				fprintf(stderr,
					"%s.core_connect.realloc: %s\n",
					prog_name, strerror(errno));
			goto failure;
		}
		memset((char *) sd_to_conn + old_size, 0, size - old_size);
	}
	assert(!sd_to_conn[sd]);
	sd_to_conn[sd] = s;
#endif

	sin = hash_lookup(s->hostname, s->hostname_len, s->port);
	if (!sin) {
		if (DBG > 0)
			fprintf(stderr,
				"%s.core_connect: unknown server/port %s:%d\n",
				prog_name, s->hostname, s->port);
		goto failure;
	}

	arg.l = 0;
	event_signal(EV_CONN_CONNECTING, (Object *) s, arg);
	if (s->state >= S_CLOSING)
		goto failure;

	s->myaddr = core_get_next_myaddr();
	myaddr.sin_addr = s->myaddr->ip;
	if (param.hog) {
		while (1) {
			myport = port_get(s->myaddr);
			if (myport < 0)
				goto failure;

			myaddr.sin_port = htons(myport);
			SYSCALL(BIND,
				result = bind(sd, (struct sockaddr *) &myaddr,
					      sizeof(myaddr)));
			if (result == 0)
				break;

			if (errno != EADDRINUSE && errno == EADDRNOTAVAIL) {
				if (DBG > 0)
					fprintf(stderr,
						"%s.core_connect.bind: %s\n",
						prog_name, strerror(errno));
				goto failure;
			}
		}
		s->myport = myport;
	} else if (myaddr.sin_addr.s_addr != htonl(INADDR_ANY)) {
		SYSCALL(BIND,
		    result = bind(sd, (struct sockaddr *) &myaddr,
			sizeof(myaddr)));
		if (result != 0)
			goto failure;
	}

	SYSCALL(CONNECT,
		result = connect(sd, (struct sockaddr *) sin, sizeof(*sin)));
	if (result == 0) {
#ifdef HAVE_SSL
		if (param.use_ssl)
			core_ssl_connect(s);
		else
#endif
		{
			s->state = S_CONNECTED;
			arg.l = 0;
			event_signal(EV_CONN_CONNECTED, (Object *) s, arg);
		}
	} else if (errno == EINPROGRESS) {
		/*
		 * The socket becomes writable only after the connection has
		 * been established.  Hence we wait for writability to detect
		 * connection establishment.  
		 */
		s->state = S_CONNECTING;
		set_active(s, WRITE);
		if (param.timeout > 0.0) {
			arg.vp = s;
			assert(!s->watchdog);
			s->watchdog =
			    timer_schedule(conn_timeout, arg, param.timeout);
		}
	} else {
		len = sizeof(async_errno);
		if (getsockopt(sd, SOL_SOCKET, SO_ERROR, &async_errno, &len) ==
		    0 && async_errno != 0)
			errno = async_errno;

		if (DBG > 0)
			fprintf(stderr,
				"%s.core_connect.connect: %s (max_sd=%d)\n",
				prog_name, strerror(errno), max_sd);
		if (s->myport > 0)
			port_put(s->myaddr, s->myport);
		goto failure;
	}
	return 0;

      failure:
	conn_failure(s, errno);
	return -1;
}

int
core_send(Conn * conn, Call * call)
{
	Any_Type        arg;

	arg.l = 0;
	event_signal(EV_CALL_ISSUE, (Object *) call, arg);

	call->conn = conn;	/* NO refcounting here (see call.h).  */

	if (param.no_host_hdr) {
		call->req.iov[IE_HOST].iov_base = (caddr_t) "";
		call->req.iov[IE_HOST].iov_len = 0;
	} else if (!call->req.iov[IE_HOST].iov_base) {
		/*
		 * Default call's hostname to connection's hostname: 
		 */
		call->req.iov[IE_HOST].iov_base = (caddr_t) conn->fqdname;
		call->req.iov[IE_HOST].iov_len = conn->fqdname_len;
	}

	/*
	 * NOTE: the protocol version indicates what the _client_ can
	 * understand.  If we send HTTP/1.1, it doesn't mean that the server
	 * has to speak HTTP/1.1.  In other words, sending an HTTP/1.1 header
	 * leaves it up to the server whether it wants to reply with a 1.0 or
	 * 1.1 reply.  
	 */
	switch (call->req.version) {
	case 0x10000:
		if (param.no_host_hdr) {
			call->req.iov[IE_PROTL].iov_base =
			    (caddr_t) http10req_nohost;
			call->req.iov[IE_PROTL].iov_len =
			    sizeof(http10req_nohost) - 1;
		} else {
			call->req.iov[IE_PROTL].iov_base = (caddr_t) http10req;
			call->req.iov[IE_PROTL].iov_len =
			    sizeof(http10req) - 1;
		}
		break;

	case 0x10001:
		if (param.no_host_hdr) {
			call->req.iov[IE_PROTL].iov_base = http11req_nohost;
			call->req.iov[IE_PROTL].iov_len =
			    sizeof(http11req_nohost) - 1;
		} else {
			call->req.iov[IE_PROTL].iov_base = http11req;
			call->req.iov[IE_PROTL].iov_len =
			    sizeof(http11req) - 1;
		}
		break;

	default:
		fprintf(stderr, "%s: unexpected version code %x\n",
			prog_name, call->req.version);
		exit(1);
	}
	call->req.iov_index = 0;
	call->req.iov_saved = call->req.iov[0];

	/*
	 * insert call into connection's send queue: 
	 */
	call_inc_ref(call);
	call->sendq_next = 0;
	if (!conn->sendq) {
		conn->sendq = conn->sendq_tail = call;
		arg.l = 0;
		event_signal(EV_CALL_SEND_START, (Object *) call, arg);
		if (conn->state >= S_CLOSING)
			return -1;
		call->timeout =
		    param.timeout ? timer_now() + param.timeout : 0.0;
		set_active(conn, WRITE);
	} else {
		conn->sendq_tail->sendq_next = call;
		conn->sendq_tail = call;
	}
	return 0;
}

void
core_close(Conn * conn)
{
	Call           *call, *call_next;
	Any_Type        arg;
	int             sd;

	if (conn->state >= S_CLOSING)
		return;		/* guard against recursive calls */
	conn->state = S_CLOSING;

	if (DBG >= 10)
		fprintf(stderr, "%s.core_close(conn=%p)\n", prog_name, conn);

	if (conn->watchdog) {
		timer_cancel(conn->watchdog);
		conn->watchdog = 0;
	}

	/*
	 * first, get rid of all pending calls: 
	 */
	for (call = conn->sendq; call; call = call_next) {
		call_next = call->sendq_next;
		call_dec_ref(call);
	}
	conn->sendq = 0;

	for (call = conn->recvq; call; call = call_next) {
		call_next = call->recvq_next;
		call_dec_ref(call);
	}
	conn->recvq = 0;

	sd = conn->sd;
	conn->sd = -1;

	arg.l = 0;
	event_signal(EV_CONN_CLOSE, (Object *) conn, arg);
	assert(conn->state == S_CLOSING);

#ifdef HAVE_SSL
	if (param.use_ssl)
		SSL_shutdown(conn->ssl);
#endif

	if (sd >= 0) {
#ifdef HAVE_EPOLL
		struct epoll_event ev = { 0, { 0 } };
		int error;

		error = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sd, &ev);
		if (error < 0) {
			if (conn->epoll_added == 1 && error != ENOENT) {
				error = errno;
				printf("EPOLL_CTL_DEL: %d %d %d\n", epoll_fd, sd, error);
				assert(error == 0);
			}
		}
#endif
		close(sd);
#if !defined(HAVE_KEVENT) && !defined(HAVE_EPOLL)
		sd_to_conn[sd] = 0;
		FD_CLR(sd, &wrfds);
		FD_CLR(sd, &rdfds);
#endif
		conn->reading = 0;
		conn->writing = 0;
	}
	if (conn->myport > 0)
		port_put(conn->myaddr, conn->myport);

	/*
	 * A connection that has been closed is not useful anymore, so we give 
	 * up the reference obtained when creating the session.  This normally 
	 * initiates destruction of the connection.  
	 */
	conn_dec_ref(conn);
}

#ifdef HAVE_KEVENT
void
core_loop(void)
{
	struct kevent ev;
	int n;
	Any_Type   arg;
	Conn      *conn;

	while (running) {
		++iteration;

		n = kevent(kq, NULL, 0, &ev, 1, NULL);
		if (n < 0 && errno != EINTR) {
			fprintf(stderr, "failed to fetch event: %s",
			    strerror(errno));
			exit(1);
		}

		switch (ev.filter) {
		case EVFILT_TIMER:
			timer_tick();
			break;
		case EVFILT_READ:
		case EVFILT_WRITE:
			conn = ev.udata;
	                conn_inc_ref(conn);

	                if (conn->watchdog) {
	                    timer_cancel(conn->watchdog);
	                    conn->watchdog = 0;
	                }
	                if (conn->state == S_CONNECTING) {
#ifdef HAVE_SSL
	                    if (param.use_ssl)
	                        core_ssl_connect(conn);
	                    else
#endif
	                    if (ev.filter == EVFILT_WRITE) {
				clear_active(conn, WRITE);
	                        conn->state = S_CONNECTED;
	                        arg.l = 0;
	                        event_signal(EV_CONN_CONNECTED, (Object*)conn, arg);
	                    }
	                } else {
			    if (ev.filter == EVFILT_WRITE && conn->sendq)
	                        do_send(conn);
	                    if (ev.filter == EVFILT_READ && conn->recvq)
	                        do_recv(conn);
	                }
	                    
	                conn_dec_ref(conn);
			break;
		}
	}
}
#else
#ifdef HAVE_EPOLL
void
core_loop(void)
{
	struct epoll_event *ep;
	int i, n;
	Any_Type   arg;
	Conn      *conn;

	while (running) {
		++iteration;

		timer_tick();
		n = epoll_wait(epoll_fd, epoll_events, EPOLL_N_MAX, epoll_timeout);
		if (n < 0 && errno == EINTR) {
			fprintf(stderr, "failed to fetch event: %s",
			    strerror(errno));
			continue;
		}
		ep = epoll_events;
		for (i = 0; i < n; i++, ep++) {
			conn = ep->data.ptr;
			conn_inc_ref(conn);

			if (conn->watchdog) {
				timer_cancel(conn->watchdog);
				conn->watchdog = 0;
			}
			if (conn->state == S_CONNECTING) {
#ifdef HAVE_SSL
				if (param.use_ssl)
					core_ssl_connect(conn);
				else
#endif
				if (ep->events & EPOLLOUT) {
					clear_active(conn, WRITE);
					conn->state = S_CONNECTED;
					arg.l = 0;
					event_signal(EV_CONN_CONNECTED, (Object*)conn, arg);
				}
			} else {
				if (ep->events & (EPOLLIN | EPOLLHUP) && conn->recvq)
					do_recv(conn);
				if (ep->events & EPOLLOUT && conn->sendq)
					do_send(conn);
			}
			conn_dec_ref(conn);
		}
	}
	close(epoll_fd);
}
#else
void
core_loop(void)
{
	int        is_readable, is_writable, n, sd, bit, min_i, max_i, i = 0;
	fd_set     readable, writable;
	fd_mask    mask;
	Any_Type   arg;
	Conn      *conn;
 
	while (running) {
	    struct timeval  tv = select_timeout;

	    timer_tick();

	    readable = rdfds;
	    writable = wrfds;
	    min_i = min_sd / NFDBITS;
	    max_i = max_sd / NFDBITS;

	    SYSCALL(SELECT,	n = select(max_sd + 1, &readable, &writable, 0, &tv));

	    ++iteration;

	    if (n <= 0) {
	        if (n < 0) {
	            fprintf(stderr, "%s.core_loop: select failed: %s\n", prog_name, strerror(errno));
	            exit(1);
	        }
	        continue;
	    }

	    while (n > 0) {
	        /*
	         * find the index of the fdmask that has something
	         * going on: 
	         */
	        do {
	            ++i;
	            if (i > max_i)
	                i = min_i;

	            assert(i <= max_i);
	            mask = readable.fds_bits[i] | writable.fds_bits[i];
	        } while (!mask);
	        bit = 0;
	        sd = i * NFDBITS + bit;
	        do {
	            if (mask & 1) {
	                --n;
	                is_readable = (FD_ISSET(sd, &readable) && FD_ISSET(sd, &rdfds));
	                is_writable = (FD_ISSET(sd, &writable) && FD_ISSET(sd, &wrfds));
	                
	                if (is_readable || is_writable) {
	                    /*
	                     * only handle sockets that
	                     * haven't timed out yet
	                     */
	                    conn = sd_to_conn[sd];
	                    conn_inc_ref(conn);

	                    if (conn->watchdog) {
	                        timer_cancel(conn->watchdog);
	                        conn->watchdog = 0;
	                    }
	                    if (conn->state == S_CONNECTING) {
#ifdef HAVE_SSL
	                        if (param.use_ssl)
	                             core_ssl_connect(conn);
	                        else
#endif
	                        if (is_writable) {
				    clear_active(conn, WRITE);
	                            conn->state = S_CONNECTED;
	                            arg.l = 0;
	                            event_signal(EV_CONN_CONNECTED, (Object*)conn, arg);
	                        }
	                    } else {
	                        if (is_writable && conn->sendq)
	                            do_send(conn);
	                        if (is_readable && conn->recvq)
	                            do_recv(conn);
	                    }
	                    
	                    conn_dec_ref(conn);
	                    
	                    if (n > 0)
	                         timer_tick();
	                }
	            }
	            mask = ((u_long) mask) >> 1;
	            ++sd;
	        } while (mask);
	    }
	}
}
#endif
#endif

void
core_exit(void)
{
	running = 0;
	param.num_conns = 0;

	printf("Maximum connect burst length: %lu\n", max_burst_len);

#ifdef TIME_SYSCALLS
	{
		u_int           count;
		Time            time;
		int             i;

		printf("Average syscall execution times:\n");
		for (i = 0; i < NELEMS(syscall_name); ++i) {
			count = syscall_count[i];
			time = syscall_time[i];
			printf("\t%s:\t%.3f ms/call (%.3fs total, %u calls)\n",
			       syscall_name[i],
			       count > 0 ? 1e3 * time / count : 0, time,
			       count);

		}
		putchar('\n');
	}
#endif
}
