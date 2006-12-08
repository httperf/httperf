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

#ifndef event_h
#define event_h

#include <httperf.h>
#include <object.h>

typedef enum Event_Type
  {
    EV_PERF_SAMPLE,

    EV_HOSTNAME_LOOKUP_START,
    EV_HOSTNAME_LOOKUP_STOP,

    EV_SESS_NEW,
    EV_SESS_FAILED,
    EV_SESS_DESTROYED,

    EV_CONN_NEW,
    EV_CONN_CONNECTING,
    EV_CONN_CONNECTED,
    EV_CONN_CLOSE,		/* connection closed */
    EV_CONN_DESTROYED,
    EV_CONN_FAILED,		/* failed for reasons other than timeout */
    EV_CONN_TIMEOUT,

    EV_CALL_NEW,
    EV_CALL_ISSUE,
    EV_CALL_SEND_START,
    EV_CALL_SEND_RAW_DATA,
    EV_CALL_SEND_STOP,
    EV_CALL_RECV_START,
    EV_CALL_RECV_HDR,
    EV_CALL_RECV_RAW_DATA,
    EV_CALL_RECV_DATA,
    EV_CALL_RECV_FOOTER,
    EV_CALL_RECV_STOP,
    EV_CALL_DESTROYED,

    EV_NUM_EVENT_TYPES
  }
Event_Type;

typedef struct Event
  {
    Event_Type type;
    Object *obj;
    Any_Type arg;
  }
Event;

typedef void (*Event_Handler) (Event_Type type, Object *obj,
			       Any_Type registration_time_arg,
			       Any_Type signal_time_arg);

extern void event_register_handler (Event_Type et, Event_Handler handler,
				    Any_Type arg);
extern void event_signal (Event_Type type, Object *obj, Any_Type arg);

#endif /* event_h */
