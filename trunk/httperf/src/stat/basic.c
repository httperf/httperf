/*
    httperf -- a tool for measuring web server performance
    Copyright 2000-2007 Hewlett-Packard Company and Contributors listed in
    AUTHORS file. Originally contributed by David Mosberger-Tang

    This file is part of httperf, a web server performance measurment
    tool.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.
    
    In addition, as a special exception, the copyright holders give
    permission to link the code of this work with the OpenSSL project's
    "OpenSSL" library (or with modified versions of it that use the same
    license as the "OpenSSL" library), and distribute linked combinations
    including the two.  You must obey the GNU General Public License in
    all respects for all of the code used other than "OpenSSL".  If you
    modify this file, you may extend this exception to your version of the
    file, but you are not obligated to do so.  If you do not wish to do
    so, delete this exception statement from your version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
    02110-1301, USA
*/

/* Basic statistics collector.  */

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <stdio.h>

#include <httperf.h>
#include <call.h>
#include <event.h>
#include <stats.h>

/* Increase this if it does not cover at least 50% of all response
   times.  */
#define MAX_LIFETIME	100.0		/* max. conn. lifetime in seconds */
#define BIN_WIDTH	1e-3
#define NUM_BINS	((u_int) (MAX_LIFETIME / BIN_WIDTH))

static struct
  {
    u_int num_conns_issued;	/* total # of connections issued */
    u_int num_replies[6];	/* completion count per status class */
    u_int num_client_timeouts;	/* # of client timeouts */
    u_int num_sock_fdunavail;	/* # of times out of filedescriptors */
    u_int num_sock_ftabfull;	/* # of times file table was full */
    u_int num_sock_refused;	/* # of ECONNREFUSED */
    u_int num_sock_reset;	/* # of ECONNRESET */
    u_int num_sock_timeouts;	/* # of ETIMEDOUT */
    u_int num_sock_addrunavail;/* # of EADDRNOTAVAIL */
    u_int num_other_errors;	/* # of other errors */
    u_int max_conns;		/* max # of concurrent connections */

    u_int num_lifetimes;
    Time conn_lifetime_sum;	/* sum of connection lifetimes */
    Time conn_lifetime_sum2;	/* sum of connection lifetimes squared */
    Time conn_lifetime_min;	/* minimum connection lifetime */
    Time conn_lifetime_max;	/* maximum connection lifetime */

    u_int num_reply_rates;
    Time reply_rate_sum;
    Time reply_rate_sum2;
    Time reply_rate_min;
    Time reply_rate_max;

    u_int num_connects;		/* # of completed connect()s */
    Time conn_connect_sum;	/* sum of connect times */

    u_int num_responses;
    Time call_response_sum;	/* sum of response times */

    Time call_xfer_sum;		/* sum of response times */

    u_int num_sent;		/* # of requests sent */
    size_t req_bytes_sent;

    u_int num_received;		/* # of replies received */
    u_wide hdr_bytes_received;	/* sum of all header bytes */
    u_wide reply_bytes_received;	/* sum of all data bytes */
    u_wide footer_bytes_received;	/* sum of all footer bytes */

    u_int conn_lifetime_hist[NUM_BINS];	/* histogram of connection lifetimes */
  }
basic;

static u_int num_active_conns;
static u_int num_replies;	/* # of replies received in this interval */

static void
perf_sample (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type call_arg)
{
  Time weight = call_arg.d;
  double rate;

  assert (et == EV_PERF_SAMPLE);

  rate = weight*num_replies;

  if (verbose)
    printf ("reply-rate = %-8.1f\n", rate);

  basic.reply_rate_sum += rate;
  basic.reply_rate_sum2 += SQUARE (rate);
  if (rate < basic.reply_rate_min)
    basic.reply_rate_min = rate;
  if (rate > basic.reply_rate_max)
    basic.reply_rate_max = rate;
  ++basic.num_reply_rates;

  /* prepare for next sample interval: */
  num_replies = 0;
}

static void
conn_timeout (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type call_arg)
{
  assert (et == EV_CONN_TIMEOUT);

  ++basic.num_client_timeouts;
}

static void
conn_fail (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type call_arg)
{
  static int first_time = 1;
  int err = call_arg.i;

  assert (et == EV_CONN_FAILED);

  switch (err)
    {
#ifdef __linux__
    case EINVAL:	/* Linux has a strange way of saying "out of fds"... */
#endif
    case EMFILE:	++basic.num_sock_fdunavail; break;
    case ENFILE:	++basic.num_sock_ftabfull; break;
    case ECONNREFUSED:	++basic.num_sock_refused; break;
    case ETIMEDOUT:	++basic.num_sock_timeouts; break;

    case EPIPE:
    case ECONNRESET:
      ++basic.num_sock_reset;
      break;

    default:
      if (first_time)
	{
	  first_time = 0;
	  fprintf (stderr, "%s: connection failed with unexpected error %d\n",
		   prog_name, errno);
	}
      ++basic.num_other_errors;
      break;
    }
}

static void
conn_created (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type c_arg)
{
  ++num_active_conns;
  if (num_active_conns > basic.max_conns)
    basic.max_conns = num_active_conns;
}

static void
conn_connecting (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type c_arg)
{
  Conn *s = (Conn *) obj;

  assert (et == EV_CONN_CONNECTING && object_is_conn (s));

  s->basic.time_connect_start = timer_now ();
  ++basic.num_conns_issued;
}

static void
conn_connected (Event_Type et, Object *obj, Any_Type reg_arg,
		Any_Type call_arg)
{
  Conn *s = (Conn *) obj;

  assert (et == EV_CONN_CONNECTED && object_is_conn (s));
  basic.conn_connect_sum += timer_now () - s->basic.time_connect_start;
  ++basic.num_connects;
}

static void
conn_destroyed (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type c_arg)
{
  Conn *s = (Conn *) obj;
  Time lifetime;
  u_int bin;

  assert (et == EV_CONN_DESTROYED && object_is_conn (s)
	  && num_active_conns > 0);

  if (s->basic.num_calls_completed > 0)
    {
      lifetime = timer_now () - s->basic.time_connect_start;
      basic.conn_lifetime_sum += lifetime;
      basic.conn_lifetime_sum2 += SQUARE (lifetime);
      if (lifetime < basic.conn_lifetime_min)
	basic.conn_lifetime_min = lifetime;
      if (lifetime > basic.conn_lifetime_max)
	basic.conn_lifetime_max = lifetime;
      ++basic.num_lifetimes;

      bin = lifetime*NUM_BINS/MAX_LIFETIME;
      if (bin >= NUM_BINS)
	bin = NUM_BINS;
      ++basic.conn_lifetime_hist[bin];
    }
  --num_active_conns;
}

static void
send_start (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type call_arg)
{
  Call *c = (Call *) obj;

  assert (et == EV_CALL_SEND_START && object_is_call (c));

  c->basic.time_send_start = timer_now ();
}

static void
send_stop (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type call_arg)
{
  Call *c = (Call *) obj;

  assert (et == EV_CALL_SEND_STOP && object_is_call (c));

  basic.req_bytes_sent += c->req.size;
  ++basic.num_sent;
}

static void
recv_start (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type call_arg)
{
  Call *c = (Call *) obj;
  Time now;

  assert (et == EV_CALL_RECV_START && object_is_call (c));

  now = timer_now ();

  basic.call_response_sum += now - c->basic.time_send_start;
  c->basic.time_recv_start = now;
  ++basic.num_responses;
}

static void
recv_stop (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type call_arg)
{
  Call *c = (Call *) obj;
  int index;

  assert (et == EV_CALL_RECV_STOP && object_is_call (c));
  assert (c->basic.time_recv_start > 0);

  basic.call_xfer_sum += timer_now () - c->basic.time_recv_start;

  basic.hdr_bytes_received += c->reply.header_bytes;
  basic.reply_bytes_received += c->reply.content_bytes;
  basic.footer_bytes_received += c->reply.footer_bytes;

  index = (c->reply.status / 100);
  assert ((unsigned) index < NELEMS (basic.num_replies));
  ++basic.num_replies[index];
  ++num_replies;

  ++c->conn->basic.num_calls_completed;
}

static void
init (void)
{
  Any_Type arg;

  basic.conn_lifetime_min = DBL_MAX;
  basic.reply_rate_min = DBL_MAX;

  arg.l = 0;
  event_register_handler (EV_PERF_SAMPLE, perf_sample, arg);
  event_register_handler (EV_CONN_FAILED, conn_fail, arg);
  event_register_handler (EV_CONN_TIMEOUT, conn_timeout, arg);
  event_register_handler (EV_CONN_NEW, conn_created, arg);
  event_register_handler (EV_CONN_CONNECTING, conn_connecting, arg);
  event_register_handler (EV_CONN_CONNECTED, conn_connected, arg);
  event_register_handler (EV_CONN_DESTROYED, conn_destroyed, arg);
  event_register_handler (EV_CALL_SEND_START, send_start, arg);
  event_register_handler (EV_CALL_SEND_STOP, send_stop, arg);
  event_register_handler (EV_CALL_RECV_START, recv_start, arg);
  event_register_handler (EV_CALL_RECV_STOP, recv_stop, arg);
}

static void
dump (void)
{
  Time conn_period = 0.0, call_period = 0.0;
  Time conn_time = 0.0, resp_time = 0.0, xfer_time = 0.0;
  Time call_size = 0.0, hdr_size = 0.0, reply_size = 0.0, footer_size = 0.0;
  Time lifetime_avg = 0.0, lifetime_stddev = 0.0, lifetime_median = 0.0;
  double reply_rate_avg = 0.0, reply_rate_stddev = 0.0;
  int i, total_replies = 0;
  Time delta, user, sys;
  u_wide total_size;
  Time time;
  u_int n;

  for (i = 1; i < NELEMS (basic.num_replies); ++i)
    total_replies += basic.num_replies[i];

  delta = test_time_stop - test_time_start;

  if (verbose > 1)
    {
      printf ("\nConnection lifetime histogram (time in ms):\n");
      for (i = 0; i < NUM_BINS; ++i)
	if (basic.conn_lifetime_hist[i])
	  {
	    if (i > 0 && basic.conn_lifetime_hist[i - 1] == 0)
	      printf ("%14c\n", ':');
	    time = (i + 0.5)*BIN_WIDTH;
	    printf ("%16.1f %u\n", 1e3*time, basic.conn_lifetime_hist[i]);
	  }
    }

  printf ("\nTotal: connections %u requests %u replies %u "
	  "test-duration %.3f s\n",
	  basic.num_conns_issued, basic.num_sent, total_replies,
	  delta);

  putchar ('\n');

  if (basic.num_conns_issued)
    conn_period = delta/basic.num_conns_issued;
  printf ("Connection rate: %.1f conn/s (%.1f ms/conn, "
	  "<=%u concurrent connections)\n",
	  basic.num_conns_issued / delta, 1e3*conn_period, basic.max_conns);

  if (basic.num_lifetimes > 0)
    {
      lifetime_avg = (basic.conn_lifetime_sum / basic.num_lifetimes);
      if (basic.num_lifetimes > 1)
	lifetime_stddev = STDDEV (basic.conn_lifetime_sum,
				  basic.conn_lifetime_sum2,
				  basic.num_lifetimes);
      n = 0;
      for (i = 0; i < NUM_BINS; ++i)
	{
	  n += basic.conn_lifetime_hist[i];
	  if (n >= 0.5*basic.num_lifetimes)
	    {
	      lifetime_median = (i + 0.5)*BIN_WIDTH;
	      break;
	    }
	}
    }  
  printf ("Connection time [ms]: min %.1f avg %.1f max %.1f median %.1f "
	  "stddev %.1f\n",
	  basic.num_lifetimes > 0 ? 1e3 * basic.conn_lifetime_min : 0.0,
	  1e3 * lifetime_avg,
	  1e3 * basic.conn_lifetime_max, 1e3 * lifetime_median,
	  1e3 * lifetime_stddev);
  if (basic.num_connects > 0)
    conn_time = basic.conn_connect_sum / basic.num_connects;
  printf ("Connection time [ms]: connect %.1f\n", 1e3*conn_time);
  printf ("Connection length [replies/conn]: %.3f\n",
	  basic.num_lifetimes > 0
	  ? total_replies/ (double) basic.num_lifetimes : 0.0);
  putchar ('\n');

  if (basic.num_sent > 0)
    call_period = delta/basic.num_sent;
  printf ("Request rate: %.1f req/s (%.1f ms/req)\n",
	  basic.num_sent / delta, 1e3*call_period);

  if (basic.num_sent)
    call_size = basic.req_bytes_sent / basic.num_sent;
  printf ("Request size [B]: %.1f\n", call_size);

  putchar ('\n');

  if (basic.num_reply_rates > 0)
    {
      reply_rate_avg = (basic.reply_rate_sum / basic.num_reply_rates);
      if (basic.num_reply_rates > 1)
	reply_rate_stddev = STDDEV (basic.reply_rate_sum,
				    basic.reply_rate_sum2,
				    basic.num_reply_rates);
    }
  printf ("Reply rate [replies/s]: min %.1f avg %.1f max %.1f stddev %.1f "
	  "(%u samples)\n",
	  basic.num_reply_rates > 0 ? basic.reply_rate_min : 0.0,
	  reply_rate_avg, basic.reply_rate_max,
	  reply_rate_stddev, basic.num_reply_rates);

  if (basic.num_responses > 0)
    resp_time = basic.call_response_sum / basic.num_responses;
  if (total_replies > 0)
    xfer_time = basic.call_xfer_sum / total_replies;
  printf ("Reply time [ms]: response %.1f transfer %.1f\n",
	  1e3*resp_time, 1e3*xfer_time);

  if (total_replies)
    {
      hdr_size = basic.hdr_bytes_received / total_replies;
      reply_size = basic.reply_bytes_received / total_replies;
      footer_size = basic.footer_bytes_received / total_replies;
    }
  printf ("Reply size [B]: header %.1f content %.1f footer %.1f "
	  "(total %.1f)\n", hdr_size, reply_size, footer_size,
	  hdr_size + reply_size + footer_size);

  printf ("Reply status: 1xx=%u 2xx=%u 3xx=%u 4xx=%u 5xx=%u\n",
	  basic.num_replies[1], basic.num_replies[2], basic.num_replies[3],
	  basic.num_replies[4], basic.num_replies[5]);

  putchar ('\n');

  user = (TV_TO_SEC (test_rusage_stop.ru_utime)
	  - TV_TO_SEC (test_rusage_start.ru_utime));
  sys = (TV_TO_SEC (test_rusage_stop.ru_stime)
	  - TV_TO_SEC (test_rusage_start.ru_stime));
  printf ("CPU time [s]: user %.2f system %.2f (user %.1f%% system %.1f%% "
	  "total %.1f%%)\n", user, sys, 100.0*user/delta, 100.0*sys/delta,
	  100.0*(user + sys)/delta);

  total_size = (basic.req_bytes_sent
		+ basic.hdr_bytes_received + basic.reply_bytes_received);
  printf ("Net I/O: %.1f KB/s (%.1f*10^6 bps)\n",
	  total_size/delta / 1024.0, 8e-6*total_size/delta);

  putchar ('\n');

  printf ("Errors: total %u client-timo %u socket-timo %u "
	  "connrefused %u connreset %u\n"
	  "Errors: fd-unavail %u addrunavail %u ftab-full %u other %u\n",
	  (basic.num_client_timeouts + basic.num_sock_timeouts
	   + basic.num_sock_fdunavail + basic.num_sock_ftabfull
	   + basic.num_sock_refused + basic.num_sock_reset
	   + basic.num_sock_addrunavail + basic.num_other_errors),
	  basic.num_client_timeouts, basic.num_sock_timeouts,
	  basic.num_sock_refused, basic.num_sock_reset,
	  basic.num_sock_fdunavail, basic.num_sock_addrunavail,
	  basic.num_sock_ftabfull, basic.num_other_errors);
}

Stat_Collector stats_basic =
  {
    "Basic statistics",
    init,
    no_op,
    no_op,
    dump
  };
