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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <httperf.h>
#include <call.h>
#include <conn.h>

static u_long next_id = 0;

void
call_init (Call *c)
{
# define DEFAULT_METHOD	"GET"

  c->id = next_id++;
  call_set_method (c, DEFAULT_METHOD, sizeof (DEFAULT_METHOD) - 1);
  c->req.version = param.http_version;
  c->req.iov[IE_BLANK].iov_base = (caddr_t) " ";
  c->req.iov[IE_BLANK].iov_len = 1;
  c->req.iov[IE_NEWLINE1].iov_base = (caddr_t) "\r\n";
  c->req.iov[IE_NEWLINE1].iov_len = 2;
  c->req.iov[IE_NEWLINE2].iov_base = (caddr_t) "\r\n";
  c->req.iov[IE_NEWLINE2].iov_len = 2;
}

void
call_deinit (Call *call)
{
}

int
call_append_request_header (Call *c, const char *hdr, size_t len)
{
  u_int num_hdrs = c->req.num_extra_hdrs;

  if (num_hdrs >= MAX_EXTRA_HEADERS)
    {
      fprintf (stderr, "%s.call_append_request_header: max headers "
	       "(%d) exceeded.\n", prog_name, MAX_EXTRA_HEADERS);
      return -1;
    }
  c->req.iov[IE_FIRST_HEADER + num_hdrs].iov_base = (caddr_t) hdr;
  c->req.iov[IE_FIRST_HEADER + num_hdrs].iov_len = len;
  c->req.num_extra_hdrs = num_hdrs + 1;
  return 0;
}
