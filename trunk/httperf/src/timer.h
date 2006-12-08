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

#ifndef timer_h
#define timer_h

#include <sys/types.h>

#include <httperf.h>

#define TIMER_INTERVAL	(1.0/1000)	/* timer granularity in seconds */

struct Timer;
typedef void (*Timer_Callback) (struct Timer *t, Any_Type arg);

typedef struct Timer_Queue
  {
    struct Timer *next;
    struct Timer *prev;
  }
Timer_Queue;

typedef struct Timer
  {
    Timer_Queue q;		/* must be first member! */
    u_long delta;
    Timer_Callback func;
    Any_Type arg;
  }
Timer;

extern Time timer_now_forced (void);
extern Time timer_now (void);

extern void timer_init (void);
/* Needs to be called at least once every TIMER_INTERVAL:  */
extern void timer_tick (void);

extern Timer *timer_schedule (Timer_Callback timeout, Any_Type arg,
			      Time delay);
extern void timer_cancel (Timer *t);

#endif /* timer_h */
