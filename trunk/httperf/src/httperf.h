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

#ifndef httperf_h
#define httperf_h

#include "config.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>

typedef double Time;

#define NELEMS(a)	((sizeof (a)) / sizeof ((a)[0]))
#define TV_TO_SEC(tv)	((tv).tv_sec + 1e-6*(tv).tv_usec)

typedef union
  {
    char c;
    int i;
    long l;
    u_char uc;
    u_int ui;
    u_long ul;
    float f;
    double d;
    void *vp;
    const void *cvp;
  }
Any_Type;

typedef enum Dist_Type
  {
    DETERMINISTIC,	/* also called fixed-rate */
    UNIFORM,		/* over interval [min_iat,max_iat) */
    EXPONENTIAL		/* with mean mean_iat */
  }
Dist_Type;

typedef struct Load_Generator
  {
    const char *name;
    void (*init) (void);
    void (*start) (void);
    void (*stop) (void);
  }
Load_Generator;

typedef struct Stat_Collector
  {
    const char *name;
    /* START and STOP are timing sensitive, so they should be as short
       as possible.  More expensive stuff can be done during INIT and
       DUMP.  */
    void (*init) (void);
    void (*start) (void);
    void (*stop) (void);
    void (*dump) (void);
  }
Stat_Collector;

typedef struct Rate_Info
  {
    Dist_Type dist;		/* interarrival distribution */
    double rate_param;		/* 0 if mean_iat==0, else 1/mean_iat */
    Time mean_iat;		/* mean interarrival time */
    Time min_iat;		/* min interarrival time (for UNIFORM) */
    Time max_iat;	        /* max interarrival time (for UNIFORM) */
  }
Rate_Info;

#define PRINT_HEADER	(1 << 0)
#define PRINT_BODY	(1 << 1)

typedef struct Cmdline_Params
  {
    int http_version;	/* (default) HTTP protocol version */
    const char *server;	/* (default) hostname */
    const char *server_name; /* fully qualified server name */
    int port;		/* (default) server port */
    const char *uri;	/* (default) uri */
    Rate_Info rate;
    Time timeout;	/* watchdog timeout */
    Time think_timeout;	/* timeout for server think time */
    int num_conns;	/* # of connections to generate */
    int num_calls;	/* # of calls to generate per connection */
    int burst_len;	/* # of calls to burst back-to-back */
    int max_piped;	/* max # of piped calls per connection */
    int max_conns;	/* max # of connections per session */
    int hog;		/* client may hog as much resources as possible */
    int send_buffer_size;
    int recv_buffer_size;
    int failure_status;	/* status code that should be considered failure */
    int retry_on_failure; /* when a call fails, should we retry? */
    int close_with_reset; /* close connections with TCP RESET? */
    int print_request;	/* bit 0: print req headers, bit 1: print req body */
    int print_reply;	/* bit 0: print repl headers, bit 1: print repl body */
    int session_cookies; /* handle set-cookies? (at the session level) */
    int no_host_hdr;	/* don't send Host: header in request */
#ifdef HAVE_SSL
    int use_ssl;	/* connect via SSL */
    int ssl_reuse;	/* reuse SSL Session ID */
    const char *ssl_cipher_list; /* client's list of SSL cipher suites */
#endif
    const char *additional_header;	/* additional request header(s) */
    const char *method;	/* default call method */
    struct
      {
	u_int id;
	u_int num_clients;
      }
    client;
    struct
      {
	char *file;	/* name of the file where entries are */
	char do_loop;	/* boolean indicating if we want to loop on entries */
      }
    wlog;
    struct
      {
	u_int num_sessions;	/* # of sessions */
	u_int num_calls;	/* # of calls per session */
	Time think_time;	/* user think time between calls */
      }
    wsess;
    struct
      {
	u_int num_sessions;	/* # of sessions */
	u_int num_reqs;		/* # of user requests per session */
	Time think_time;	/* user think time between requests */
      }
    wsesspage;
    struct
      {
	u_int num_sessions;	/* # of user-sessions */
	Time think_time;	/* user think time between calls */
	char *file;		/* name of the file where session defs are */
      }
    wsesslog;
    struct
      {
	u_int num_files;
	double target_miss_rate;
      }
    wset;
  }
Cmdline_Params;

extern const char *prog_name;
extern int verbose;
extern Cmdline_Params param;
extern Time test_time_start;
extern Time test_time_stop;
extern struct rusage test_rusage_start;
extern struct rusage test_rusage_stop;

#ifdef HAVE_SSL
# include <openssl/ssl.h>
  extern SSL_CTX *ssl_ctx;
#endif

#ifdef DEBUG
  extern int debug_level;
# define DBG debug_level
#else
# define DBG 0
#endif

extern void panic (const char *msg, ...);
extern void no_op (void);

#endif /* httperf_h */
