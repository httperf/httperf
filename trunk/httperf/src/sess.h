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

#ifndef sess_h
#define sess_h

#include <sys/types.h>

#include <httperf.h>
#include <object.h>

#ifdef HAVE_SSL
# include <openssl/ssl.h>
#endif

/* Sessions are not used by the httperf's core itself, but they are
   provided here for the benefit of workload generators that need such
   a notion (e.g., to represent users).  */
typedef struct Sess
  {
    Object obj;
#ifdef HAVE_SSL
    SSL *ssl;		/* SSL session (or NULL) */
#endif /* HAVE_SSL */
    u_int failed : 1;	/* did session fail? */
  }
Sess;

/* Initialize the new session object S.  */
extern void sess_init (Sess *s);

/* Destroy the session-specific state in session object S.  */
extern void sess_deinit (Sess *s);

/* Session S failed.  This causes EV_SESS_FAILED to be signalled and
   gives up the caller's reference to S.  */
extern void sess_failure (Sess *s);

#define sess_new()	((Sess *) object_new (OBJ_SESS))
#define sess_inc_ref(s)	object_inc_ref ((Object *) (s))
#define sess_dec_ref(s)	object_dec_ref ((Object *) (s))

#endif /* sess_h */
