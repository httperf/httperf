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

/* A session consists of a number of calls.  Workload generators such
   as wsess and wsesslog determine when and how to issue calls.  This
   module is responsible for the actual mechanics of issuing the
   calls.  This includes creating and managing connections as
   necessary.  Connection management can be controlled by command-line
   options --max-piped-calls=Np and --max-connections=Nc.  This module
   creates up to Nc concurrent connections to dispatch calls.  On each
   connection, up to Np pipelined requests can be issued.  When no
   more calls can be issued, this module waits until some of the
   pending calls complete.

   Note that HTTP/1.1 allows a server to close a connection pretty
   much any time it feels like.  This means that a session may fail
   (be closed) while there pipelined calls are pending.  In such a
   case, this module takes care of creating a new connection and
   re-issuing the calls that were pending on the failed connection.

   A session is considered to fail if:

   (a) any operation exceeds the timeout parameters, or

   (b) a connection closes on us before we received at least one
       reply, or

   (c) param.failure_status is non-zero and the reply status of a call
       matches this failure status.
   */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <httperf.h>
#include <call.h>
#include <conn.h>
#include <core.h>
#include <event.h>
#include <sess.h>
#include <session.h>

#define MAX_CONN		 4	/* max # of connections per session */
#define MAX_PIPED		32	/* max # of calls that can be piped */

#define SESS_PRIVATE_DATA(c)						\
  ((Sess_Private_Data *) ((char *)(c) + sess_private_data_offset))

#define CONN_PRIVATE_DATA(c)						\
  ((Conn_Private_Data *) ((char *)(c) + conn_private_data_offset))

#define CALL_PRIVATE_DATA(c)						\
  ((Call_Private_Data *) ((char *)(c) + call_private_data_offset))

typedef struct Sess_Private_Data
  {
    struct Conn_Info
      {
	Conn *conn;		/* connection or NULL */
	u_int is_connected : 1;	/* is connection ready for use? */
	u_int is_successful : 1; /* got at least one reply on this conn? */

	/* Ring-buffer of pending calls: */
	u_int num_pending;	/* # of calls pending */
	u_int num_sent;		/* # of calls sent so far */
	u_int rd;		/* first pending call */
	u_int wr;		/* where to insert next call */
	Call *call[MAX_PIPED];
      }
    conn_info[MAX_CONN];
  }
Sess_Private_Data;

typedef struct Conn_Private_Data
  {
    Sess *sess;
    struct Conn_Info *ci;	/* pointer to relevant conn-info */
  }
Conn_Private_Data;

typedef struct Call_Private_Data
  {
    Sess *sess;
  }
Call_Private_Data;

static size_t sess_private_data_offset = -1;
static size_t conn_private_data_offset = -1;
static size_t call_private_data_offset = -1;
static size_t max_qlen;

static void
create_conn (Sess *sess, struct Conn_Info *ci)
{
  Conn_Private_Data *cpriv;

  /* No connection yet (or anymore).  Create a new connection.  Note
     that CI->CONN is NOT reference-counted.  This is again to avoid
     introducing recursive dependencies (see also comment regarding
     member CONN in call.h). */
  ci->conn = conn_new ();
  if (!ci->conn)
    {
      sess_failure (sess);
      return;
    }
  cpriv = CONN_PRIVATE_DATA (ci->conn);
  cpriv->sess = sess;
  cpriv->ci = ci;

  ci->is_connected = 0;
  ci->is_successful = 0;
  ci->num_sent = 0;		/* (re-)send all pending calls */

#ifdef HAVE_SSL
  if (param.ssl_reuse && ci->conn->ssl && sess->ssl)
    {
      if (DBG > 0)
	fprintf (stderr, "create_conn: reusing SSL session %p\n",
		 (void *) sess->ssl);
      SSL_copy_session_id (ci->conn->ssl, sess->ssl);
    }
#endif

  if (core_connect (ci->conn) < 0)
    sess_failure (sess);
}

static void
send_calls (Sess *sess, struct Conn_Info *ci)
{
  u_int rd;
  int i;

  if (!ci->conn)
    {
      create_conn (sess, ci);
      return;
    }

  if (!ci->is_connected)
    /* wait until connection is connected (or has failed)  */
    return;

  rd = (ci->rd + ci->num_sent) % MAX_PIPED;

  for (i = ci->num_sent; i < ci->num_pending; ++i)
    {
      core_send (ci->conn, ci->call[rd]);
      ++ci->num_sent;
      rd = (rd + 1) % MAX_PIPED;
    }
}

static void
sess_destroyed (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Sess_Private_Data *priv;
  struct Conn_Info *ci;
  Sess *sess;
  int i, j, rd;

  assert (et == EV_SESS_DESTROYED && object_is_sess (obj));
  sess = (Sess *) obj;
  priv = SESS_PRIVATE_DATA (sess);

  for (i = 0; i < param.max_conns; ++i)
    {
      ci = priv->conn_info + i;

      if (ci->conn)
	core_close (ci->conn);

      rd = ci->rd;
      for (j = 0; j < ci->num_pending; ++j)
	{
	  call_dec_ref (ci->call[rd]);
	  rd = (rd + 1) % MAX_PIPED;
	}
    }
}

static void
conn_connected (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Conn_Private_Data *cpriv;
  struct Conn_Info *ci;
  Sess *sess;
  Conn *conn;

  assert (et == EV_CONN_CONNECTED && object_is_conn (obj));

  conn = (Conn *) obj;
  cpriv = CONN_PRIVATE_DATA (conn);
  sess = cpriv->sess;
  ci = cpriv->ci;

  ci->is_connected = 1;

#ifdef HAVE_SSL
  if (param.ssl_reuse && !sess->ssl && ci->conn->ssl)
    {
      sess->ssl = SSL_dup (ci->conn->ssl);
      if (DBG > 0)
	fprintf (stderr, "create_conn: cached SSL session %p as %p\n",
		 (void *) ci->conn->ssl, (void *) sess->ssl);
    }
#endif /* HAVE_SSL */

  send_calls (sess, ci);
}

static void
conn_failed (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Conn_Private_Data *cpriv;
  struct Conn_Info *ci;
  Conn *conn;
  Sess *sess;

  assert (et == EV_CONN_FAILED && object_is_conn (obj));
  conn = (Conn *) obj;
  cpriv = CONN_PRIVATE_DATA (conn);
  sess = cpriv->sess;
  ci = cpriv->ci;

  if (ci->is_successful || param.retry_on_failure)
    /* try to create a new connection so we can issue the remaining
       calls. */
    create_conn (sess, ci);
  else
    /* The connection failed before we got even one reply, so declare
       the session as dead... */
    sess_failure (cpriv->sess);
}

static void
conn_timeout (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Conn_Private_Data *cpriv;
  Conn *conn;

  /* doh, this session is dead now... */

  assert (et == EV_CONN_TIMEOUT && object_is_conn (obj));
  conn = (Conn *) obj;
  cpriv = CONN_PRIVATE_DATA (conn);

  sess_failure (cpriv->sess);
}

static void
call_done (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Conn_Private_Data *cpriv;
  struct Conn_Info *ci;
  Sess *sess;
  Conn *conn;
  Call *call;

  assert (et == EV_CALL_RECV_STOP && object_is_call (obj));
  call = (Call *) obj;
  conn = call->conn;
  cpriv = CONN_PRIVATE_DATA (conn);
  sess = cpriv->sess;
  ci = cpriv->ci;

  ci->is_successful = 1;	/* conn has received at least one reply */

  /* remove the call from the conn_info structure */
  assert (ci->call[ci->rd] == call && ci->num_pending > 0 && ci->num_sent > 0);
  ci->call[ci->rd] = 0;
  ci->rd = (ci->rd + 1) % MAX_PIPED;
  --ci->num_pending;
  --ci->num_sent;

  /* if the reply status matches the failure status, the session has
     failed */
  if (param.failure_status && call->reply.status == param.failure_status)
    {
      if (param.retry_on_failure)
	session_issue_call (sess, call);
      else
	sess_failure (sess);
    }

  call_dec_ref (call);

  if (param.http_version < 0x10001)
    {
      /* Rather than waiting for the connection to close on us, we
	 close it pro-actively (this is what a pre-1.1 browser would
	 do.  */
      core_close (ci->conn);
      ci->conn = 0;
    }
}

void
session_init (void)
{
  Any_Type arg;

  if (!param.max_conns)
    param.max_conns = MAX_CONN;

  if (!param.max_piped)
    {
      if (param.http_version >= 0x10001)
	param.max_piped = MAX_PIPED;
      else
	/* no pipelining before HTTP/1.1... */
	param.max_piped = 1;
    }

  if (param.max_conns > MAX_CONN)
    {
      fprintf (stderr, "%s.session_init: --max-conns must be <= %u\n",
	       prog_name, MAX_CONN);
      exit (1);
    }
  if (param.max_piped > MAX_PIPED)
    {
      fprintf (stderr, "%s.session_init: --max-piped-calls must be <= %u\n",
	       prog_name, MAX_PIPED);
      exit (1);
    }

  max_qlen = param.max_conns * param.max_piped;

  sess_private_data_offset = object_expand (OBJ_SESS,
					    sizeof (Sess_Private_Data));
  conn_private_data_offset = object_expand (OBJ_CONN,
					    sizeof (Conn_Private_Data));
  call_private_data_offset = object_expand (OBJ_CALL,
					    sizeof (Call_Private_Data));

  arg.l = 0;
  event_register_handler (EV_SESS_DESTROYED, sess_destroyed, arg);

  event_register_handler (EV_CONN_CONNECTED, conn_connected, arg);
  event_register_handler (EV_CONN_FAILED, conn_failed, arg);
  event_register_handler (EV_CONN_TIMEOUT, conn_timeout, arg);

  event_register_handler (EV_CALL_RECV_STOP, call_done, arg);
}

size_t
session_max_qlen (Sess *sess)
{
  return max_qlen;
}

size_t
session_current_qlen (Sess *sess)
{
  Sess_Private_Data *priv;
  size_t num_pending = 0;
  int i;

  priv = SESS_PRIVATE_DATA (sess);

  for (i = 0; i < param.max_conns; ++i)
    num_pending += priv->conn_info[i].num_pending;

  return num_pending;
}

int
session_issue_call (Sess *sess, Call *call)
{
  Call_Private_Data *cpriv;
  Sess_Private_Data *priv;
  struct Conn_Info *ci;
  int i;

  priv = SESS_PRIVATE_DATA (sess);

  cpriv = CALL_PRIVATE_DATA (call);
  cpriv->sess = sess;

  for (i = 0; i < param.max_conns; ++i)
    {
      ci = priv->conn_info + i;
      if (ci->num_pending < param.max_piped)
	{
	  ++ci->num_pending;
	  ci->call[ci->wr] = call;
	  call_inc_ref (call);
	  ci->wr = (ci->wr + 1) % MAX_PIPED;
	  send_calls (sess, ci);
	  return 0;
	}
    }
  fprintf (stderr, "%s.session_issue_call: too many calls pending!\n"
	   "\tIncrease --max-connections and/or --max-piped-calls.\n",
	   prog_name);
  exit (1);
}

Sess *
session_get_sess_from_conn (Conn *conn)
{
  assert (object_is_conn (conn));
  return CONN_PRIVATE_DATA (conn)->sess;
}

Sess *
session_get_sess_from_call (Call *call)
{
  assert (object_is_call (call));
  return CALL_PRIVATE_DATA (call)->sess;
}
