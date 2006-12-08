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

#include <httperf.h>
#include <event.h>
#include <sess.h>

void
sess_init (Sess *sess)
{
}

void
sess_deinit (Sess *sess)
{
#ifdef HAVE_SSL
  if (sess->ssl)
    SSL_free (sess->ssl);
#endif
}

void
sess_failure (Sess *sess)
{
  Any_Type arg;

  if (sess->failed)
    return;
  sess->failed = 1;

  arg.l = 0;
  event_signal (EV_SESS_FAILED, (Object *) sess, arg);

  sess_dec_ref (sess);
}
