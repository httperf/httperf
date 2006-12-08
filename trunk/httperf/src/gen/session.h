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

#ifndef session_h
#define session_h

#include <sess.h>

extern void session_init (void);	/* initialize session module */

/* Maximum number of calls that can be queued on a session.  */
extern size_t session_max_qlen (Sess *sess);

/* Current number of calls that are queued on the session.  */
extern size_t session_current_qlen (Sess *sess);

/* Issue call CALL on session SESS.  Returns negative number in case
   of failure.  */
extern int session_issue_call (Sess *sess, Call *call);

/* Given a connection object, find the session object that the
   connection belongs to.  */
extern Sess *session_get_sess_from_conn (Conn *conn);

/* Given a call object, find the session object that the call
   belongs to.  */
extern Sess *session_get_sess_from_call (Call *call);

#endif /* session_h */
