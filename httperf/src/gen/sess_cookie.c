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

/* This module intercepts `Set-Cookie:' headers on a per-session basis
   and includes set cookies in future calls of the session.

   Missing features:
	- intercepted cookies are always sent, independent of any constraints
	   that may be present in the set-cookie header
*/

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
#include <session.h>

#define MAX_COOKIE_LEN	256

#define SESS_PRIVATE_DATA(c) \
  ((Sess_Private_Data *) ((char *)(c) + sess_private_data_offset))

#define CALL_PRIVATE_DATA(c) \
  ((Call_Private_Data *) ((char *)(c) + call_private_data_offset))

typedef struct Sess_Private_Data
  {
    /* For now, we support just one cookie per session.  If we get
       more than one cookie, we'll print a warning message when
       --debug is turned on.  */
    size_t cookie_len;
    /* We can't malloc the cookie string because we may get a
       ``Set-Cookie:'' while there are calls pending that refer to an
       existing cookie.  So if we were to malloc & free cookies, we
       would have to use reference counting to avoid the risk of
       dangling pointers.  */
    char cookie[MAX_COOKIE_LEN];
  }
Sess_Private_Data;

/* We need the call private data to ensure that the cookie gets set
   only once.  EV_CALL_ISSUE gets signalled each time a call is sent
   on a connection.  Since a connection may fail, the same call may be
   issued multiple times, hence we need to make sure that the cookie
   gets set only once per call.  */
typedef struct Call_Private_Data
  {
    u_int cookie_present;	/* non-zero if cookie has been set already */
    char cookie[MAX_COOKIE_LEN];
  }
Call_Private_Data;

static size_t sess_private_data_offset = -1;
static size_t call_private_data_offset = -1;

/* A simple module that collects cookies from the server responses and
   includes them in future calls to the server.  */

static void
call_issue (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Call_Private_Data *cpriv;
  Sess_Private_Data *priv;
  Sess *sess;
  Call *call;

  assert (et == EV_CALL_ISSUE && object_is_call (obj));
  call = (Call *) obj;
  cpriv = CALL_PRIVATE_DATA (call);

  if (cpriv->cookie_present)
    /* don't do anything if cookie has been set already */
    return;

  sess = session_get_sess_from_call (call);
  priv = SESS_PRIVATE_DATA (sess);

  if (priv->cookie_len > 0)
    {
      if (DBG > 1)
	fprintf (stderr, "call_issue.%ld: inserting `%s'\n",
		 call->id, priv->cookie);
      cpriv->cookie_present = 1;
      memcpy (cpriv->cookie, priv->cookie, priv->cookie_len + 1);
      call_append_request_header (call, cpriv->cookie, priv->cookie_len);
    }
}

static void
call_recv_hdr (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  char *hdr, *start, *end;
  Sess_Private_Data *priv;
  size_t len;
  struct iovec *line;
  Sess *sess;
  Call *call;

  assert (et == EV_CALL_RECV_HDR && object_is_call (obj));
  call = (Call *) obj;
  sess = session_get_sess_from_call (call);
  priv = SESS_PRIVATE_DATA (sess);

  line = callarg.vp;
  hdr = line->iov_base;
  if (tolower (hdr[0]) == 's' && line->iov_len > 12
      && strncasecmp (hdr + 1, "et-cookie: ", 11) == 0)
    {
      /* munch time! */
      start = hdr + 12;
      end = strchr (start, ';');
      if (!end)
	end = hdr + line->iov_len;
      len = end - start;
      if (DBG > 0 && priv->cookie_len > 0)
	fprintf (stderr, "%s: can't handle more than one "
		 "cookie at a time, replacing existing one\n", prog_name);
      if (len + 10 >= MAX_COOKIE_LEN)
	{
	  fprintf (stderr, "%s.sess_cookie: truncating cookie to %d bytes\n",
		   prog_name, MAX_COOKIE_LEN - 11);
	  len = MAX_COOKIE_LEN - 11;
	}
      memcpy (priv->cookie, "Cookie: ", 8);
      memcpy (priv->cookie + 8, start, len);
      memcpy (priv->cookie + 8 + len, "\r\n", 2);
      priv->cookie[10 + len] = '\0';
      priv->cookie_len = len + 10;

      if (DBG > 0)
	fprintf (stderr, "%s: got cookie `%s'\n", prog_name, start);
    }
}

static void
init (void)
{
  Any_Type arg;

  sess_private_data_offset = object_expand (OBJ_SESS,
					    sizeof (Sess_Private_Data));
  call_private_data_offset = object_expand (OBJ_CALL,
					    sizeof (Call_Private_Data));
  arg.l = 0;
  event_register_handler (EV_CALL_ISSUE, call_issue, arg);
  event_register_handler (EV_CALL_RECV_HDR, call_recv_hdr, arg);
}

Load_Generator sess_cookie =
  {
    "per-session cookie manager",
    init,
    no_op,
    no_op
  };
