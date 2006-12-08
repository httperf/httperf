/*
    httperf -- a tool for measuring web server performance
    Copyright (C) 2000  Hewlett-Packard Company
    Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

    This file is part of httperf, a web server performance measurment
    tool.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
    02111-1307 USA
*/

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <httperf.h>
#include <conn.h>

void
conn_init (Conn *conn)
{
  conn->hostname = param.server;
  conn->hostname_len = strlen (param.server);
  conn->port = param.port;
  conn->sd = -1;
  conn->myport = -1;
  conn->line.iov_base = conn->line_buf;
  if (param.server_name)
    {
      conn->fqdname = param.server_name;
      conn->fqdname_len = strlen (param.server_name);
    }
  else
    {
      conn->fqdname = conn->hostname;
      conn->fqdname_len = conn->hostname_len;
    }

#ifdef HAVE_SSL
  if (param.use_ssl)
    {
      conn->ssl = SSL_new (ssl_ctx);
      if (!conn->ssl)
	{
	  ERR_print_errors_fp (stderr);
	  exit (-1);
	}

      if (param.ssl_cipher_list)
	{
	  /* set order of ciphers  */
	  int ssl_err = SSL_set_cipher_list (conn->ssl, param.ssl_cipher_list);

	  if (DBG > 2)
	    fprintf (stderr, "core_ssl_connect: set_cipher_list returned %d\n",
		     ssl_err);
	}
    }
#endif
}

void
conn_deinit (Conn *conn)
{
  assert (conn->sd < 0 && conn->state != S_FREE);
  assert (!conn->sendq);
  assert (!conn->recvq);
  assert (!conn->watchdog);
  conn->state = S_FREE;

#ifdef HAVE_SSL
  if (param.use_ssl)
    SSL_free (conn->ssl);
#endif
}
