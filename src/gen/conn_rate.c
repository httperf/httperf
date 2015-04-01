/*
    httperf -- a tool for measuring web server performance
    Copyright 2000-2007 Hewlett-Packard Company

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

/* Creates connections at the fixed rate PARAM.RATE or sequentially if
   PARAM.RATE is zero.  */

#include "config.h"

#include <assert.h>
#include <stdio.h>

#include <generic_types.h>

#include <object.h>
#include <timer.h>
#include <httperf.h>
#include <localevent.h>
#include <rate.h>
#include <conn.h>
#include <call.h>
#include <core.h>

static int num_conns_generated;
static int num_conns_destroyed;
static int num_conns_open;
static Rate_Generator rg;
static bool paused;

static int
make_conn (Any_Type arg)
{
  Conn *s;

  if (paused)
    return 0;

  if (num_conns_generated++ >= param.num_conns)
    return -1;

  if (param.max_conns != 0 && num_conns_open >= param.max_conns)
    return 0;

  s = conn_new ();
  if (!s)
    return -1;

  num_conns_open++;
  if (core_connect (s) == -1) {
    num_conns_generated--;
    num_conns_destroyed--;
    paused = true;
  }
  return 0;
}

static void
destroyed (void)
{
  Any_Type arg;

  if (++num_conns_destroyed >= param.num_conns)
    core_exit ();
  num_conns_open--;
  paused = false;
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
