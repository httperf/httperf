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
#ifndef call_h
#define call_h

#include <sys/types.h>
#include <sys/uio.h>

#include <httperf.h>
#include <conn.h>
#include <object.h>

/* Max. # of additional request header lines we allow: */
#define MAX_EXTRA_HEADERS	4

typedef enum IOV_Element
  {
    IE_METHOD,
    IE_BLANK,		/* space separating method from location */
    IE_LOC,		/* for proxy requests only */
    IE_URI,
    IE_PROTL,
    IE_HOST,		/* for the "Host:" header */
    IE_NEWLINE1,
    IE_FIRST_HEADER,
    IE_LAST_HEADER = IE_FIRST_HEADER + MAX_EXTRA_HEADERS - 1,
    IE_NEWLINE2,
    IE_CONTENT,
    IE_LEN		/* must be last */
  }
IOV_Element;

/* I call this a "call" because "transaction" is too long and because
   it's basically a remote procedure call consisting of a request that
   is answered by a reply.  */
typedef struct Call
  {
    Object obj;

    u_long id;			/* unique id */

    /* Connection this call is being sent on.  This pointer is NOT
       reference counted as otherwise we would get a recursive
       dependency between connections and calls....  */
    struct Conn *conn;
    struct Call *sendq_next;
    struct Call *recvq_next;
    Time timeout;		/* used for watchdog management */

    struct
      {
	Time time_send_start;
	Time time_recv_start;
      }
    basic;

    /* the request: */
    struct
      {
	int version;		/* 0x10000*major + minor */
	u_int num_extra_hdrs;	/* number of additional headers in use */
	int iov_index;		/* first iov element that has data */
	size_t size;		/* # of bytes sent */
	struct iovec iov_saved;	/* saved copy of iov[iov_index] */
	struct iovec iov[IE_LEN];
      }
    req;

    /* the reply: */
    struct
      {
	int status;
	int version;		/* 0x10000*major + minor */
	size_t header_bytes;	/* # of header bytes received so far */
	size_t content_bytes;	/* # of reply data bytes received so far */
	size_t footer_bytes;	/* # of footer bytes received so far */
      }
    reply;
  }
Call;

/* Initialize the new call object C.  */
extern void call_init (Call *c);

/* Destroy the call-specific state in call object C.  */
extern void call_deinit (Call *c);

#define call_new()	((Call *) object_new (OBJ_CALL))
#define call_inc_ref(c)	object_inc_ref ((Object *) (c))
#define call_dec_ref(c)	object_dec_ref ((Object *) (c))

/* Append the additional request header line(s) HDR to the request
   headers.  The total length of the additional headers is LEN bytes.
   The headers must be end with a carriage-return, line-feed sequence
   ("\r\n").  */
extern int call_append_request_header (Call *c, const char *hdr, size_t len);

#define call_set_method(c, method, method_len)			\
  do								\
    {								\
      c->req.iov[IE_METHOD].iov_base = (caddr_t) method;	\
      c->req.iov[IE_METHOD].iov_len = method_len;		\
    }								\
  while (0)

#define call_set_location(c, loc, loc_len)		\
  do							\
    {							\
      c->req.iov[IE_LOC].iov_base = (caddr_t) loc;	\
      c->req.iov[IE_LOC].iov_len = loc_len;		\
    }							\
  while (0)

#define call_set_uri(c, uri, uri_len)			\
  do							\
    {							\
      c->req.iov[IE_URI].iov_base = (caddr_t) uri;	\
      c->req.iov[IE_URI].iov_len = uri_len;		\
    }							\
  while (0)

#define call_set_contents(c, content, content_len)		\
  do								\
    {								\
      c->req.iov[IE_CONTENT].iov_base = (caddr_t) content;	\
      c->req.iov[IE_CONTENT].iov_len = content_len;		\
    }								\
  while (0)

#endif /* call_h */
