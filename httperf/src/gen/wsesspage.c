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

/* Similar to wsess but instead of generating fixed bursts, each
   fetched html page is parsed and the embedded objects are fetched in
   a burst.

   This is NOT a high performance workload generator!  Use it only for
   non-performance critical tests.  */

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

#define CALL_PRIVATE_DATA(c) \
  ((Call_Private_Data *) ((char *)(c) + call_private_data_offset))
#define SESS_PRIVATE_DATA(c) \
  ((Sess_Private_Data *) ((char *)(c) + sess_private_data_offset))

typedef struct Call_Private_Data
  {
    enum
      {
	P_INITIAL,
	P_HTML,
	P_CMD,		/* we saw a `<' and are scanning for the end of CMD */
	P_DASH_ONE,	/* looking for the first dash of a comment close */
	P_DASH_TWO,	/* looking for the second dash of a comment close */
	P_RANGLE,	/* looking for '>' */
	P_SRC,		/* we're looking for "src" */
	P_DATA,		/* we're looking for "data" */
	P_LQUOTE,	/* we're looking for the left quote of a URI */
	P_NAKED_URI,	/* we're looking for an unquoted URI */
	P_QUOTED_URI	/* we're looking for a quoted URI */
      }
    state;
    int buf_len;
    char buf[1024];
    void *to_free;	/* call queue element to free when done */
  }
Call_Private_Data;

typedef struct Sess_Private_Data
  {
    u_int num_created;		/* # of calls created in this burst */
    u_int num_destroyed;	/* # of calls destroyed in this burst */
    u_int num_reqs_completed;	/* # of user reqs completed */
    Timer *timer;		/* timer for session think time */
    struct uri_list
      {
	struct uri_list *next;
	size_t uri_len;
	char uri[1];		/* really URI_LEN+1 bytes... */
      }
    *uri_list;
  }
Sess_Private_Data;

static size_t sess_private_data_offset;
static size_t call_private_data_offset;

static int num_sessions_generated;
static int num_sessions_destroyed;
static Rate_Generator rg_sess;

static size_t prefix_len;
static char *prefix;

static void
issue_calls (Sess *sess, Sess_Private_Data *priv)
{
  int i, to_create, retval, embedded = 0;
  Call_Private_Data *cpriv;
  struct uri_list *el;
  Call *call;

  /* Mimic browser behavior of fetching html object, then a couple of
     embedded objects: */

  to_create = 1;
  if (priv->num_created > 0)
    {
      to_create = session_max_qlen (sess) - session_current_qlen (sess);
      embedded = 1;
    }

  for (i = 0; i < to_create && (!embedded || priv->uri_list); ++i)
    {
      ++priv->num_created;

      call = call_new ();
      if (!call)
	{
	  sess_failure (sess);
	  return;
	}
      if (embedded)
	{
	  el = priv->uri_list;
	  priv->uri_list = el->next;

	  cpriv = CALL_PRIVATE_DATA (call);
	  cpriv->to_free = el;
	  call_set_uri (call, el->uri, el->uri_len);
	}

      if (verbose > 1)
	printf ("%s: fetching `%s'\n",
		prog_name, (char *)call->req.iov[IE_URI].iov_base);

      retval = session_issue_call (sess, call);
      call_dec_ref (call);
      if (retval < 0)
	return;
    }
}

static void
fetch_uri (Sess *sess, Sess_Private_Data *priv, Call_Private_Data *cpriv,
	   const char *uri, size_t uri_len)
{
  struct uri_list *el;
  int is_relative;
  size_t len;
  char *dst;

  if (strchr (uri, ':'))
    {
      len = strlen (param.server);
      if (strncmp (uri, "http://", 7) == 0
	  && strncmp (uri + 7, param.server, len) == 0
	  && uri[7 + len] == '/')
	{
	  uri += 7 + len;
	  uri_len -= 7 + len;
	}
      else
	{
	  /* Eventually, we may want to create new sessions on the fly,
	     but for now, we simply punt on non-absolute URIs */
	  if (verbose > 1)
	    fprintf (stderr, "%s: ignoring absolute URI `%s'\n",
		     prog_name, uri);
	  return;
	}
    }

  is_relative = (uri[0] != '/');

  /* enqueue the new uri: */
  len = uri_len;
  if (is_relative)
    len += prefix_len;
  el = malloc (sizeof (*el) + len);
  if (!el)
    panic ("%s.fetch_uri: out of memory!\n", prog_name);

  el->uri_len = len;
  dst = el->uri;
  if (is_relative)
    {
      memcpy (el->uri, prefix, prefix_len);
      dst += prefix_len;
    }
  memcpy (dst, uri, uri_len + 1);

  el->next = priv->uri_list;
  priv->uri_list = el;

  issue_calls (sess, priv);
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
call_recv_hdr (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Call_Private_Data *cpriv;
  Sess_Private_Data *priv;
  struct iovec *line;
  Call *call;
  Sess *sess;
  char *hdr;

  assert (et == EV_CALL_RECV_HDR && object_is_call (obj));
  call = (Call *) obj;
  cpriv = CALL_PRIVATE_DATA (call);
  sess = session_get_sess_from_call (call);
  priv = SESS_PRIVATE_DATA (sess);

  line = callarg.vp;
  hdr = line->iov_base;

  switch (tolower (hdr[0]))
    {
    case 'c':
      if (line->iov_len >= 23
	  && strncasecmp (hdr + 1, "ontent-type: text/html", 22) == 0)
	cpriv->state = P_HTML;
      break;

    case 'l':
      if (line->iov_len > 10
	  && strncasecmp (hdr + 1, "ocation: ", 9) == 0)
	fetch_uri (sess, priv, cpriv, hdr + 10, line->iov_len - 10);
      break;
    }

}

static void
call_recv_data (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Call_Private_Data *cpriv;
  Sess_Private_Data *priv;
  const char *cp, *end;
  struct iovec *line;
  Sess *sess;
  Call *call;
  int ch;

  assert (et == EV_CALL_RECV_DATA && object_is_call (obj));
  call = (Call *) obj;
  cpriv = CALL_PRIVATE_DATA (call);
  sess = session_get_sess_from_call (call);
  priv = SESS_PRIVATE_DATA (sess);

  if (cpriv->state == P_INITIAL)
    return;	/* not an html object */

  line = callarg.vp;
  cp = line->iov_base;
  end = cp + line->iov_len;
  while (cp < end)
    {
      ch = *cp++;

      switch (cpriv->state)
	{
	case P_INITIAL:
	  break;

	case P_HTML:
	  cpriv->buf_len = 0;
	  if (ch == '<')
	    cpriv->state = P_CMD;
	  break;

	case P_CMD:
	  if (isspace (ch) || ch == '=')
	    {
	      if (cpriv->buf_len > 0)
		{
		  if (DBG > 3)
		    fprintf (stderr, "found command `%.*s'\n",
			     cpriv->buf_len, cpriv->buf);

		  if (cpriv->buf_len == 3
		      && strcmp (cpriv->buf, "!--") == 0)
		    cpriv->state = P_DASH_ONE;
		  else if (cpriv->buf_len == 5
			   && strncasecmp (cpriv->buf, "frame", 5) == 0)
		    cpriv->state = P_SRC;
		  else if (cpriv->buf_len == 6
			   && strncasecmp (cpriv->buf, "iframe", 6) == 0)
		    cpriv->state = P_SRC;
		  else if (cpriv->buf_len == 6
			   && strncasecmp (cpriv->buf, "data", 6) == 0)
		    cpriv->state = P_DATA;
		  else if (cpriv->buf_len == 3
			   && strncasecmp (cpriv->buf, "img", 3) == 0)
		    cpriv->state = P_SRC;
		  cpriv->buf_len = 0;
		}
	      else
		cpriv->state = P_HTML;
	    }
	  else if (ch == '>')
	    cpriv->state = P_HTML;
	  else if (cpriv->buf_len < sizeof (cpriv->buf))
	    cpriv->buf[cpriv->buf_len++] = ch;
	  break;

	case P_DASH_ONE:
	  if (ch == '-')
	    cpriv->state = P_DASH_TWO;
	  break;

	case P_DASH_TWO:
	  cpriv->state = (ch == '-') ? P_RANGLE : P_DASH_ONE;
	  break;

	case P_RANGLE:
	  if (ch == '>')
	    cpriv->state = P_HTML;
	  break;

	case P_SRC:
	  if (ch == '>')
	    cpriv->state = P_HTML;
	  else
	    {
	      cpriv->buf[cpriv->buf_len++] = ch;
	      if (cpriv->buf_len == 4)
		{
		  if (strncasecmp (cpriv->buf, "src=", 4) == 0)
		    {
		      cpriv->state = P_LQUOTE;
		      cpriv->buf_len = 0;
		    }
		  else
		    {
		      memcpy (cpriv->buf, cpriv->buf + 1, 3);
		      cpriv->buf_len = 3;
		    }
		}
	    }
	  break;

	case P_DATA:
	  if (ch == '>')
	    cpriv->state = P_HTML;
	  else
	    {
	      cpriv->buf[cpriv->buf_len++] = ch;
	      if (cpriv->buf_len == 5)
		{
		  if (strncasecmp (cpriv->buf, "data=", 5) == 0)
		    {
		      cpriv->state = P_LQUOTE;
		      cpriv->buf_len = 0;
		    }
		  else
		    {
		      memcpy (cpriv->buf, cpriv->buf + 1, 4);
		      cpriv->buf_len = 4;
		    }
		}
	    }
	  break;

	case P_LQUOTE:
	  if (ch == '"')
	    cpriv->state = P_QUOTED_URI;
	  else if (!isspace (ch))
	    {
	      cpriv->state = P_NAKED_URI;
	      cpriv->buf[cpriv->buf_len++] = ch;
	    }
	  break;

	case P_NAKED_URI:
	case P_QUOTED_URI:
	  if ((cpriv->state == P_QUOTED_URI && ch == '"')
	      || (cpriv->state == P_NAKED_URI && isspace (ch)))
	    {
	      cpriv->buf[cpriv->buf_len] = '\0';
	      fetch_uri (sess, priv, cpriv, cpriv->buf, cpriv->buf_len);
	      cpriv->state = P_HTML;
	      cpriv->buf_len = 0;
	    }
	  else if (cpriv->buf_len < sizeof (cpriv->buf) - 1)
	    cpriv->buf[cpriv->buf_len++] = ch;
	  break;
	}
    }
}

static void
call_destroyed (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Call_Private_Data *cpriv;
  Sess_Private_Data *priv;
  Any_Type arg;
  Sess *sess;
  Call *call;

  assert (et == EV_CALL_DESTROYED && object_is_call (obj));

  call = (Call *) obj;
  cpriv = CALL_PRIVATE_DATA (call);
  sess = session_get_sess_from_call (call);
  priv = SESS_PRIVATE_DATA (sess);

  if (cpriv->to_free)
    {
      free (cpriv->to_free);
      cpriv->to_free = 0;
    }
  ++priv->num_destroyed;

  if (sess->failed)
    return;

  if (priv->uri_list)
    /* there are some queued URI's which we may be able to issue now */
    issue_calls (sess, priv);
  else if (priv->num_destroyed >= priv->num_created)
    {
      /* we're done with this burst */
      if (++priv->num_reqs_completed >= param.wsesspage.num_reqs)
	/* we're done with this session */
	sess_dec_ref (sess);
      else 
	{
	  /* schedule the user-think-time timer */
	  priv->num_created = 0;
	  assert (!priv->timer);
	  arg.vp = sess;
	  priv->timer = timer_schedule (user_think_time_expired, arg,
					param.wsesspage.think_time);
	}
    }
}

/* Create a new session.  */
static int
sess_create (Any_Type arg)
{
  Sess_Private_Data *priv;
  Sess *sess;

  if (num_sessions_generated++ >= param.wsesspage.num_sessions)
    return -1;

  sess = sess_new ();
  if (!sess)
    return 1;

  priv = SESS_PRIVATE_DATA (sess);

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

  if (++num_sessions_destroyed >= param.wsesspage.num_sessions)
    core_exit ();
}

static void
init (void)
{
  const char *slash;
  Any_Type arg;

  slash = strrchr (param.uri, '/');
  if (slash)
    prefix_len = (slash + 1) - param.uri;
  else
    panic ("%s: URI specified with --uri must be absolute", prog_name);

  prefix = strdup (param.uri);
  prefix[prefix_len] = '\0';

  session_init ();

  call_private_data_offset = object_expand (OBJ_CALL,
					    sizeof (Call_Private_Data));
  sess_private_data_offset = object_expand (OBJ_SESS,
					    sizeof (Sess_Private_Data));
  rg_sess.rate = &param.rate;
  rg_sess.tick = sess_create;
  rg_sess.arg.l = 0;

  arg.l = 0;
  event_register_handler (EV_CALL_RECV_HDR, call_recv_hdr, arg);
  event_register_handler (EV_CALL_RECV_DATA, call_recv_data, arg);
  event_register_handler (EV_SESS_DESTROYED, sess_destroyed, arg);
  event_register_handler (EV_CALL_DESTROYED, call_destroyed, arg);
}

static void
start (void)
{
  rate_generator_start (&rg_sess, EV_SESS_DESTROYED);
}

Load_Generator wsesspage =
  {
    "creates sessions that fetch html pages and embedded objects",
    init,
    start,
    no_op
  };
