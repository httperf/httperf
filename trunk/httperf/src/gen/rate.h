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

#ifndef rate_h
#define rate_h

#include <httperf.h>
#include <timer.h>

typedef struct Rate_Generator
  {
    u_short xsubi[3];		/* used for random number generation */
    Rate_Info *rate;
    Time start;
    Time next_time;
    Any_Type arg;
    Timer *timer;
    int (*tick) (Any_Type arg);
    int done;
    Time (*next_interarrival_time) (struct Rate_Generator *rg);
  }
Rate_Generator;

extern void rate_generator_start (Rate_Generator *rg,
				  Event_Type completion_event);
extern void rate_generator_stop (Rate_Generator *rg);

#endif /* rate_h */
