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
