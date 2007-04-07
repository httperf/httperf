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

/* Creates sessions at the fixed rate PARAM.RATE.  */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <httperf.h>
#include <call.h>
#include <conn.h>
#include <core.h>
#include <event.h>
#include <rate.h>
#include <session.h>
#include <timer.h>

#define SESS_PRIVATE_DATA(c) \
  ((Sess_Private_Data *) ((char *)(c) + sess_private_data_offset))

typedef struct Sess_Private_Data
  {
    u_int num_calls_in_this_burst; /* # of calls created for this burst */
    u_int num_calls_target;	/* total # of calls desired */
    u_int num_calls_destroyed;	/* # of calls destroyed so far */
    Timer *timer;		/* timer for session think time */
  }
Sess_Private_Data;

static size_t sess_private_data_offset;

static int num_sessions_generated;
static int num_sessions_destroyed;
static Rate_Generator rg_sess;


static void
issue_calls (Sess *sess, Sess_Private_Data *priv)
{
  int i, to_create, retval;
  Call *call;

  /* Mimic browser behavior of fetching html object, then a couple of
     embedded objects: */

  to_create = 1;
  if (priv->num_calls_in_this_burst > 0)
    to_create = param.burst_len - priv->num_calls_in_this_burst;

  priv->num_calls_in_this_burst += to_create;

  for (i = 0; i < to_create; ++i)
    {
      call = call_new ();
      if (!call)
	{
	  sess_failure (sess);
	  return;
	}

      retval = session_issue_call (sess, call);
      call_dec_ref (call);

      if (retval < 0)
	return;
    }
}

static void
user_think_time_expired (Timer *t, Any_Type arg)
{
  Sess *sess = arg.vp;
  Sess_Private_Data *priv;

  assert (object_is_sess (sess));

  priv = SESS_PRIVATE_DATA (sess);
  priv->timer = 0;

  issue_calls (sess, priv);
}

static void
call_destroyed (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Any_Type arg;
  Sess *sess;
  Call *call;
  Sess_Private_Data *priv;

  assert (et == EV_CALL_DESTROYED && object_is_call (obj));

  call = (Call *) obj;
  sess = session_get_sess_from_call (call);
  priv = SESS_PRIVATE_DATA (sess);

  ++priv->num_calls_destroyed;

  if (priv->num_calls_destroyed >= param.wsess.num_calls)
    {
      /* we're done with this session */
      if (!sess->failed)
	sess_dec_ref (sess);
    }
  else if (priv->num_calls_in_this_burst < param.burst_len)
    /* now that we received the reply to the first call in this burst,
       create the remaining calls */
    issue_calls (sess, priv);
  else if (priv->num_calls_destroyed >= priv->num_calls_target)
    {
      /* we're done with this burst---schedule the user-think-time timer */
      priv->num_calls_in_this_burst = 0;
      priv->num_calls_target += param.burst_len;
      assert (!priv->timer);
      arg.vp = sess;
      priv->timer = timer_schedule (user_think_time_expired, arg,
				    param.wsess.think_time);
    }
}

/* Create a new session.  */
static int
sess_create (Any_Type arg)
{
  Sess_Private_Data *priv;
  Sess *sess;

  if (num_sessions_generated++ >= param.wsess.num_sessions)
    return -1;

  sess = sess_new ();
  if (!sess)
    return 1;

  priv = SESS_PRIVATE_DATA (sess);

  priv->num_calls_target = param.burst_len;
  issue_calls (sess, SESS_PRIVATE_DATA (sess));
  return 0;
}

static void
sess_destroyed (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Sess_Private_Data *priv;
  Sess *sess;

  assert (et == EV_SESS_DESTROYED && object_is_sess (obj));
  sess = (Sess *) obj;

  priv = SESS_PRIVATE_DATA (sess);
  if (priv->timer)
    {
      timer_cancel (priv->timer);
      priv->timer = 0;
    }

  if (++num_sessions_destroyed >= param.wsess.num_sessions)
    core_exit ();
}

static void
init (void)
{
  Any_Type arg;

  session_init ();

  sess_private_data_offset = object_expand (OBJ_SESS,
					    sizeof (Sess_Private_Data));
  rg_sess.rate = &param.rate;
  rg_sess.tick = sess_create;
  rg_sess.arg.l = 0;

  arg.l = 0;
  event_register_handler (EV_SESS_DESTROYED, sess_destroyed, arg);
  event_register_handler (EV_CALL_DESTROYED, call_destroyed, arg);
}

static void
start (void)
{
  rate_generator_start (&rg_sess, EV_SESS_DESTROYED);
}

Load_Generator wsess =
  {
    "creates session workload",
    init,
    start,
    no_op
  };
