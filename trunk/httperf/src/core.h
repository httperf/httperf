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
#ifndef core_h
#define core_h

#include <netinet/in.h>

#include <call.h>
#include <conn.h>

extern void core_init (void);
extern struct sockaddr_in *core_intern_addr (const char *hostname,
					     size_t hostname_len, int port);
extern int core_connect (Conn *conn);
extern int core_send (Conn *conn, Call *call);
extern void core_close (Conn *conn);

extern void core_loop (void);
extern void core_exit (void);

#endif /* core_h */
