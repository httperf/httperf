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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>

#include <httperf.h>
#include <timer.h>

#define WHEEL_SIZE	4096

#if 1
static Time now;
#endif
static Time next_tick;
static Timer *timer_free_list = 0;
static Timer *t_curr = 0;

/* What a wheel is made of, no?  */
static Timer_Queue wheel[WHEEL_SIZE], *curr = 0;

static void
done (Timer *t)
{
  t->q.next = timer_free_list;
  t->q.prev = 0;
  timer_free_list = t;
}

Time
timer_now_forced (void)
{
  struct timeval tv;

  gettimeofday (&tv, 0);
  return tv.tv_sec + tv.tv_usec*1e-6;
}

Time
timer_now (void)
{
  return now;
}

void
timer_init (void)
{
  now = timer_now_forced ();
  memset (wheel, 0, sizeof (wheel));
  next_tick = timer_now () + TIMER_INTERVAL;
  curr = wheel;
}

void
timer_tick (void)
{
  Timer *t, *t_next;

  assert (!t_curr);

  now = timer_now_forced ();

  while (timer_now () >= next_tick)
    {
      for (t = curr->next; t && t->delta == 0; t = t_next)
	{
	  t_curr = t;
	  (*t->func) (t, t->arg);
	  t_next = t->q.next;
	  done (t);
	}
      t_curr = 0;
      curr->next = t;
      if (t)
	{
	  t->q.prev = (Timer *) curr;
	  --t->delta;
	}
      next_tick += TIMER_INTERVAL;
      if (++curr >= wheel + WHEEL_SIZE)
	curr = wheel;
    }
}

Timer*
timer_schedule (Timer_Callback timeout, Any_Type arg, Time delay)
{
  Timer_Queue *spoke;
  Timer *t, *p;
  u_long ticks;
  u_long delta;
  Time behind;

  if (timer_free_list)
    {
      t = timer_free_list;
      timer_free_list = t->q.next;
    }
  else
    {
      t = malloc (sizeof (*t));
      if (!t)
	{
	  fprintf (stderr, "%s.timer_schedule: %s\n",
		   prog_name, strerror (errno));
	  return 0;
	}
    }
  memset (t, 0, sizeof (*t));
  t->func = timeout;
  t->arg = arg;

  behind = (timer_now () - next_tick);
  if (behind > 0.0)
    delay += behind;

  if (delay < 0.0)
    ticks = 1;
  else
    {
      ticks = (delay + TIMER_INTERVAL / 2.0) * (1.0 / TIMER_INTERVAL);
      if (!ticks)
	ticks = 1;			/* minimum delay is a tick */
    }

  spoke = curr + (ticks % WHEEL_SIZE);
  if (spoke >= wheel + WHEEL_SIZE)
    spoke -= WHEEL_SIZE;

  delta = ticks / WHEEL_SIZE;
  p = (Timer *) spoke;
  while (p->q.next && delta > p->q.next->delta)
    {
      delta -= p->q.next->delta;
      p = p->q.next;
    }
  t->q.next = p->q.next;
  t->q.prev = p;
  p->q.next = t;
  t->delta = delta;
  if (t->q.next)
    {
      t->q.next->q.prev = t;
      t->q.next->delta -= delta;
    }

  if (DBG > 2)
    fprintf (stderr, "timer_schedule: t=%p, delay=%gs, arg=%lx\n",
	     t, delay, arg.l);

  return t;
}

void
timer_cancel (Timer *t)
{
  if (DBG > 2)
    fprintf (stderr, "timer_cancel: t=%p\n", t);

  assert (t->q.prev);

  /* A module MUST NOT call timer_cancel() for a timer that is
     currently being processed (whose timeout has expired).  */
  if (t_curr == t)
    {
      fprintf (stderr, "timer_cancel() called on currently active timer!\n");
      return;
    }

  if (t->q.next)
    {
      t->q.next->delta += t->delta;
      t->q.next->q.prev = t->q.prev;
    }
  t->q.prev->q.next = t->q.next;
  done (t);
}
