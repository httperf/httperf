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

/* Causes calls to make a request to the fixed URI specified by
   PARAM.URI.  */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <httperf.h>
#include <call.h>
#include <event.h>

static size_t uri_len;

static void
set_uri (Event_Type et, Call *call)
{
  assert (et == EV_CALL_NEW && object_is_call (call));
  call_set_uri (call, param.uri, uri_len);
}

static void
init (void)
{
  Any_Type arg;

  uri_len = strlen (param.uri);

  arg.l = 0;
  event_register_handler (EV_CALL_NEW, (Event_Handler) set_uri, arg);
}

Load_Generator uri_fixed =
  {
    "fixed url",
    init,
    no_op,
    no_op
  };
