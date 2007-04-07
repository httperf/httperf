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

/* Issue a sequence of calls on a connection.  */

#include <assert.h>

#include <httperf.h>
#include <call.h>
#include <core.h>
#include <event.h>
#include <conn.h>

#define CONN_PRIVATE_DATA(c) \
  ((Conn_Private_Data *) ((char *)(c) + conn_private_data_offset))

#define MIN(a,b)	((a) < (b) ? (a) : (b))

typedef struct Conn_Private_Data
  {
    int num_calls;
    int num_completed;
    int num_destroyed;
  }
Conn_Private_Data;

static size_t conn_private_data_offset;

static void
issue_calls (Conn *conn)
{
  Conn_Private_Data *priv;
  Call *call;
  int i;

  priv = CONN_PRIVATE_DATA (conn);
  priv->num_completed = 0;
  priv->num_destroyed = 0;

  for (i = 0; i < param.burst_len; ++i)
    if (priv->num_calls++ < param.num_calls)
      {
	call = call_new ();
	if (call)
	  {
	    core_send (conn, call);
	    call_dec_ref (call);
	  }
      }
}

static void
conn_connected (Event_Type et, Conn *conn)
{
  assert (et == EV_CONN_CONNECTED && object_is_conn (conn));

  issue_calls (conn);
}

static void
call_done (Event_Type et, Call *call)
{
  Conn *conn = call->conn;
  Conn_Private_Data *priv;

  assert (et == EV_CALL_RECV_STOP && conn && object_is_conn (conn));

  priv = CONN_PRIVATE_DATA (conn);
  ++priv->num_completed;
}

static void
call_destroyed (Event_Type et, Call *call)
{
  Conn_Private_Data *priv;
  Conn *conn;

  assert (et == EV_CALL_DESTROYED && object_is_call (call));

  conn = call->conn;
  priv = CONN_PRIVATE_DATA (conn);

  if (++priv->num_destroyed >= MIN (param.burst_len, param.num_calls))
    {
      if (priv->num_completed == priv->num_destroyed
	  && priv->num_calls < param.num_calls)
	issue_calls (conn);
      else
	core_close (conn);
    }
}

static void
init (void)
{
  Any_Type arg;

  conn_private_data_offset = object_expand (OBJ_CONN,
					    sizeof (Conn_Private_Data));

  arg.l = 0;
  event_register_handler (EV_CONN_CONNECTED, (Event_Handler) conn_connected,
			  arg);
  event_register_handler (EV_CALL_RECV_STOP, (Event_Handler) call_done, arg);
  event_register_handler (EV_CALL_DESTROYED, (Event_Handler) call_destroyed,
			  arg);
}

Load_Generator call_seq =
  {
    "performs a sequence of calls on a connection",
    init,
    no_op,
    no_op
  };
