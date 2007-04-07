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

/* This statistics collector simply prints the replies received from
   the server.  */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <httperf.h>
#include <call.h>
#include <conn.h>
#include <event.h>

typedef struct Call_Private_Data
  {
    int done_with_reply_hdr; /* are we done printing reply header? */
    size_t size;	/* number of bytes allocated for "line" buffer */
    size_t len;		/* current length of line buffer */
    char *line;		/* line buffer */
  }
Call_Private_Data;

#define CALL_PRIVATE_DATA(c)						\
  ((Call_Private_Data *) ((char *)(c) + call_private_data_offset))

static size_t call_private_data_offset = -1;

static void
flush_print_buf (Call *call, const char *prefix)
{
  Call_Private_Data *priv = CALL_PRIVATE_DATA (call);

  if (priv->len)
    printf ("%s%ld:%.*s\n", prefix, call->id, (int) priv->len, priv->line);
  priv->len = 0;
}

static void
print_buf (Call *call, const char *prefix, const char *buf, int len)
{
  Call_Private_Data *priv = CALL_PRIVATE_DATA (call);
  const char *eol, *end;
  size_t line_len;

  for (end = buf + len; buf < end; buf += line_len)
    {
      line_len = (end - buf);
      eol = strchr (buf, '\n');
      if (eol)
	{
	  /* got a complete line: print it */
	  line_len = eol - buf;
	  printf ("%s%ld:", prefix, call->id);

	  if (priv->len)
	    printf ("%.*s", (int) priv->len, priv->line);
	  priv->len = 0;

	  if (line_len > 0)
	    printf ("%.*s", (int) line_len, buf);
	  putchar ('\n');

	  ++line_len;	/* skip over newline */
	}
      else
	{
	  /* got a partial line: buffer it */
	  if (priv->len + line_len > priv->size)
	    {
	      priv->size = priv->len + line_len;
	      if (priv->line)
		priv->line = realloc (priv->line, priv->size);
	      else
		priv->line = malloc (priv->size);
	      if (!priv->line)
		{
		  fprintf (stderr, "%s.print_buf: Out of memory\n", prog_name);
		  exit (1);
		}
	    }
	  memcpy (priv->line + priv->len, buf, line_len);
	  priv->len += line_len;
	}
    }
}

static void
print_request (Call *call)
{
  size_t hdr_len, h_len, b_len;
  int i, first, end;
  char *hdr;

  first = IE_CONTENT;
  end = IE_CONTENT;

  if ((param.print_request & PRINT_HEADER) != 0)
    first = IE_METHOD;

  if ((param.print_request & PRINT_BODY) != 0)
    end = IE_LEN;

  for (i = first; i < end; ++i)
    {
      hdr = call->req.iov[i].iov_base;
      hdr_len = call->req.iov[i].iov_len;

      if (hdr_len)
	print_buf (call, (i < IE_CONTENT) ? "SH" : "SB", hdr, hdr_len);
    }

  for (h_len = 0, i = IE_METHOD; i < IE_CONTENT; ++i)
    h_len += call->req.iov[i].iov_len;

  for (b_len = 0, i = IE_CONTENT; i < IE_LEN; ++i)
    b_len += call->req.iov[i].iov_len;

  printf ("SS%ld: header %ld content %ld\n",
	  call->id, (long) h_len, (long) b_len);
}

static void
print_reply_hdr (Call *call, const char *buf, int len)
{
  Call_Private_Data *priv = CALL_PRIVATE_DATA (call);
  const char *eoh;

  if (len <= 0 || priv->done_with_reply_hdr)
    return;

  eoh = strstr (buf, "\r\n\r\n");
  if (eoh)
    {
      priv->done_with_reply_hdr = 1;
      eoh += 4;
    }
  else
    {
      /* no CRLFCRLF: non-conforming server */
      eoh = strstr (buf, "\n\n");
      if (eoh)
	{
	  priv->done_with_reply_hdr = 1;
	  eoh += 2;
	}
      else
	eoh = buf + len;
    }
  print_buf (call, "RH", buf, eoh - buf);
}

static void
call_destroyed (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Call_Private_Data *priv;
  Call *call;

  assert (et == EV_CALL_DESTROYED && object_is_call (obj));
  call = (Call *) obj;
  priv = CALL_PRIVATE_DATA (call);

  if (priv->line)
    free (priv->line);
}

static void
send_raw_data (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Call *call;

  assert (et == EV_CALL_SEND_RAW_DATA && object_is_call (obj));
  call = (Call *) obj;

  print_request (call);
}

static void
recv_raw_data (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  struct iovec *iov;
  Call *call;

  assert (et == EV_CALL_RECV_RAW_DATA && object_is_call (obj));
  call = (Call *) obj;
  iov = callarg.vp;

  print_reply_hdr (call, iov->iov_base, iov->iov_len);
}

static void
recv_data (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  struct iovec *iov;
  Call *call;

  assert (et == EV_CALL_RECV_DATA && object_is_call (obj));
  call = (Call *) obj;
  iov = callarg.vp;

  print_buf (call, "RB", iov->iov_base, iov->iov_len);
}

static void
recv_stop (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Call *call;

  assert (et == EV_CALL_RECV_STOP && object_is_call (obj));
  call = (Call *) obj;

  flush_print_buf (call, "RB");

  printf ("RS%ld: header %ld content %ld footer %ld\n",
	  call->id, (long) call->reply.header_bytes,
	  (long) call->reply.content_bytes, (long) call->reply.footer_bytes);
}

static void
init (void)
{
  Any_Type arg;

  call_private_data_offset = object_expand (OBJ_CALL,
					    sizeof (Call_Private_Data));
  arg.l = 0;
  if ((param.print_request & (PRINT_HEADER | PRINT_BODY)) != 0)
    event_register_handler (EV_CALL_SEND_RAW_DATA, send_raw_data, arg);
  if ((param.print_reply & PRINT_HEADER) != 0)
    event_register_handler (EV_CALL_RECV_RAW_DATA, recv_raw_data, arg);
  if ((param.print_reply & PRINT_BODY) != 0)
    event_register_handler (EV_CALL_RECV_DATA, recv_data, arg);
  if ((param.print_reply & (PRINT_HEADER | PRINT_BODY)) != 0)
    event_register_handler (EV_CALL_RECV_STOP, recv_stop, arg);
  event_register_handler (EV_CALL_DESTROYED, call_destroyed, arg);
}

Stat_Collector stats_print_reply =
  {
    "Reply printer",
    init,
    no_op,
    no_op,
    no_op
  };
