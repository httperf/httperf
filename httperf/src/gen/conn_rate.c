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

/* Creates connections at the fixed rate PARAM.RATE or sequentially if
   PARAM.RATE is zero.  */

#include <assert.h>
#include <stdio.h>

#include <httperf.h>
#include <core.h>
#include <event.h>
#include <rate.h>
#include <conn.h>
#include <timer.h>

static int num_conns_generated;
static int num_conns_destroyed;
static Rate_Generator rg;

static int
make_conn (Any_Type arg)
{
  Conn *s;

  if (num_conns_generated++ >= param.num_conns)
    return -1;

  s = conn_new ();
  if (!s)
    return -1;

  core_connect (s);
  return 0;
}

static void
destroyed (void)
{
  if (++num_conns_destroyed >= param.num_conns)
    core_exit ();
}

static void
init (void)
{
  Any_Type arg;

  rg.arg.l = 0;
  rg.tick = make_conn;

  arg.l = 0;
  event_register_handler (EV_CONN_DESTROYED, (Event_Handler) destroyed, arg);
}

static void
start (void)
{
  rg.rate = &param.rate;
  rate_generator_start (&rg, EV_CONN_DESTROYED);
}

Load_Generator conn_rate =
  {
    "creates connections at a fixed rate",
    init,
    start,
    no_op
  };
