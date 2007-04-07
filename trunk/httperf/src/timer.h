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
