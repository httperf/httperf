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
#ifndef conn_h
#define conn_h

#include "config.h"

#include <sys/uio.h>

#include <httperf.h>
#include <object.h>
#include <timer.h>

#ifdef HAVE_SSL
# include <openssl/ssl.h>
# include <openssl/err.h>
#endif

/* Maximum header line length that we can process properly.  Longer
   lines will be treated as if they were only this long (i.e., they
   will be truncated).  */
#define MAX_HDR_LINE_LEN	1024

struct Call;

typedef enum Conn_State
  {
    S_INITIAL,
    S_CONNECTING,
    S_CONNECTED,
    S_REPLY_STATUS,
    S_REPLY_HEADER,
    S_REPLY_CONTINUE,
    S_REPLY_DATA,
    S_REPLY_CHUNKED,
    S_REPLY_FOOTER,
    S_REPLY_DONE,
    S_CLOSING,
    S_FREE
  }
Conn_State;

typedef struct Conn
  {
    Object obj;

    Conn_State state;
    struct Conn *next;
    struct Call *sendq;		/* calls whose request needs to be sent */
    struct Call *sendq_tail;
    struct Call *recvq;		/* calls waiting for a reply */
    struct Call *recvq_tail;
    Timer *watchdog;

    struct
      {
	Time time_connect_start;	/* time connect() got called */
	u_int num_calls_completed;	/* # of calls that completed */
      }
    basic;			/* maintained by stat/stats_basic.c */

    size_t hostname_len;
    const char *hostname;	/* server's hostname (or 0 for default) */
    size_t fqdname_len;
    const char *fqdname;	/* fully qualified server name (or 0) */
    int port;			/* server's port (or -1 for default) */
    int	sd;			/* socket descriptor */
    int myport;			/* local port number or -1 */
    /* Since replies are read off the socket sequentially, much of the
       reply-processing related state can be kept here instead of in
       the reply structure: */
    struct iovec line;		/* buffer used to parse reply headers */
    size_t content_length;	/* content length (or INF if unknown) */
    u_int has_body : 1;		/* does reply have a body? */
    u_int is_chunked : 1;	/* is the reply chunked? */
    char line_buf[MAX_HDR_LINE_LEN];	/* default line buffer */

#ifdef HAVE_SSL
    SSL *ssl;			/* SSL connection info */
#endif
  }
Conn;

extern int max_num_conn;
extern Conn *conn;

/* Initialize the new connection object C.  */
extern void conn_init (Conn *c);

/* Destroy the connection-specific state in connection object C.  */
extern void conn_deinit (Conn *c);

#define conn_new()	((Conn *) object_new (OBJ_CONN))
#define conn_inc_ref(c)	object_inc_ref ((Object *) (c))
#define conn_dec_ref(c)	object_dec_ref ((Object *) (c))

#endif /* conn_h */
