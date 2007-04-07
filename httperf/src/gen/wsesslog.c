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

/* Creates sessions at the fixed rate PARAM.RATE.  The session descriptions
   are read in from a configuration file.

   There is currently no tool that translates from standard log
   formats to the format accepted by this module.

   An example input file follows:

   #
   # This file specifies the potentially-bursty uri sequence for a number of
   # user sessions.  The format rules of this file are as follows:
   #
   # Comment lines start with a '#' as the first character.  # anywhere else
   # is considered part of the uri.
   #
   # Lines with only whitespace delimit session definitions (multiple blank
   # lines do not generate "null" sessions).
   #
   # Lines otherwise specify a uri-sequence (1 uri per line).  If the
   # first character of the line is whitespace (e.g. space or tab), the
   # uri is considered to be part of a burst that is sent out after the
   # previous non-burst uri.
   #

   # session 1 definition (this is a comment)

   /foo.html
	/pict1.gif
	/pict2.gif
   /foo2.html
	/pict3.gif
	/pict4.gif

   #session 2 definition

   /foo3.html
   /foo4.html
	/pict5.gif

   Any comment on this module contact carter@hpl.hp.com.  */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <httperf.h>
#include <conn.h>
#include <core.h>
#include <event.h>
#include <rate.h>
#include <session.h>
#include <timer.h>

/* Maximum number of sessions that can be defined in the configuration
   file.  */
#define MAX_SESSION_TEMPLATES	1000

#ifndef TRUE
#define TRUE  (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

#define SESS_PRIVATE_DATA(c)						\
  ((Sess_Private_Data *) ((char *)(c) + sess_private_data_offset))

typedef struct req REQ;
struct req
  {
    REQ *next;
    int method;
    char *uri;
    int uri_len;
    char *contents;
    int contents_len;
    char extra_hdrs[50];	/* plenty for "Content-length: 1234567890" */
    int extra_hdrs_len;
  };

typedef struct burst BURST;
struct burst
  {
    BURST *next;
    int num_reqs;
    Time user_think_time;
    REQ *req_list;
  };

typedef struct Sess_Private_Data Sess_Private_Data;
struct Sess_Private_Data
  {
    u_int num_calls_in_this_burst; /* # of calls created for this burst */
    u_int num_calls_target;	/* total # of calls desired */
    u_int num_calls_destroyed;	/* # of calls destroyed so far */
    Timer *timer;		/* timer for session think time */

    int total_num_reqs;		/* total number of requests in this session */

    BURST *current_burst;	/* the current burst we're working on */
    REQ *current_req;		/* the current request we're working on */
  };

/* Methods allowed for a request: */
enum
  {
    HM_DELETE, HM_GET, HM_HEAD, HM_OPTIONS, HM_POST, HM_PUT, HM_TRACE,
    HM_LEN
  };

static const char *call_method_name[] =
  {
    "DELETE", "GET", "HEAD", "OPTIONS", "POST", "PUT", "TRACE"
  };

static size_t sess_private_data_offset;
static int num_sessions_generated;
static int num_sessions_destroyed;
static Rate_Generator rg_sess;

/* This is an array rather than a list because we may want different
   httperf clients to start at different places in the sequence of
   sessions. */
static int num_templates;
static int next_session_template;
static Sess_Private_Data session_templates[MAX_SESSION_TEMPLATES] =
  {
    { 0, }
  };

static void
sess_destroyed (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Sess_Private_Data *priv;
  Sess *sess;

  assert (et == EV_SESS_DESTROYED && object_is_sess (obj));
  sess = (Sess *) obj;

  priv = SESS_PRIVATE_DATA (sess);
  if (priv->timer)
    {
      timer_cancel (priv->timer);
      priv->timer = 0;
    }

  if (++num_sessions_destroyed >= param.wsesslog.num_sessions)
    core_exit ();
}

static void
issue_calls (Sess *sess, Sess_Private_Data *priv)
{
  int i, to_create, retval, n;
  const char *method_str;
  Call *call;
  REQ *req;

  /* Mimic browser behavior of fetching html object, then a couple of
     embedded objects: */

  to_create = 1;
  if (priv->num_calls_in_this_burst > 0)
    to_create = priv->current_burst->num_reqs - priv->num_calls_in_this_burst;

  n = session_max_qlen (sess) - session_current_qlen (sess);
  if (n < to_create)
    to_create = n;

  priv->num_calls_in_this_burst += to_create;

  for (i = 0; i < to_create; ++i)
    {
      call = call_new ();
      if (!call)
	{
	  sess_failure (sess);
	  return;
	}

      /* fill in the new call: */
      req = priv->current_req;
      if (req == NULL)
	panic ("%s: internal error, requests ran past end of burst\n",
	       prog_name);

      method_str = call_method_name[req->method];
      call_set_method (call, method_str, strlen (method_str));
      call_set_uri (call, req->uri, req->uri_len);
      if (req->contents_len > 0)
	{
	  /* add "Content-length:" header and contents, if necessary: */
	  call_append_request_header (call, req->extra_hdrs,
				      req->extra_hdrs_len);
	  call_set_contents (call, req->contents, req->contents_len);
	}
      priv->current_req = req->next;

      if (DBG > 0)
	fprintf (stderr, "%s: accessing URI `%s'\n", prog_name, req->uri);

      retval = session_issue_call (sess, call);
      call_dec_ref (call);

      if (retval < 0)
	return;
    }
}

static void
user_think_time_expired (Timer *t, Any_Type arg)
{
  Sess *sess = arg.vp;
  Sess_Private_Data *priv;

  assert (object_is_sess (sess));

  priv = SESS_PRIVATE_DATA (sess);
  priv->timer = 0;
  issue_calls (sess, priv);
}

/* Create a new session and fill in our private information.  */
static int
sess_create (Any_Type arg)
{
  Sess_Private_Data *priv, *template;
  Sess *sess;

  if (num_sessions_generated++ >= param.wsesslog.num_sessions)
    return -1;

  sess = sess_new ();

  template = &session_templates[next_session_template];
  if (++next_session_template >= num_templates)
    next_session_template = 0;

  priv = SESS_PRIVATE_DATA (sess);
  priv->current_burst = template->current_burst;
  priv->current_req = priv->current_burst->req_list;
  priv->total_num_reqs = template->total_num_reqs;
  priv->num_calls_target = priv->current_burst->num_reqs;

  if (DBG > 0)
    fprintf (stderr, "Starting session, first burst_len = %d\n",
	     priv->num_calls_target);

  issue_calls (sess, SESS_PRIVATE_DATA (sess));
  return 0;
}

static void
prepare_for_next_burst (Sess *sess, Sess_Private_Data *priv)
{
  Time think_time;
  Any_Type arg;

  if (priv->current_burst != NULL)
    {
      think_time = priv->current_burst->user_think_time;

      /* advance to next burst: */
      priv->current_burst = priv->current_burst->next;

      if (priv->current_burst != NULL)
	{
	  priv->current_req = priv->current_burst->req_list;
	  priv->num_calls_in_this_burst = 0;
	  priv->num_calls_target += priv->current_burst->num_reqs;

	  assert (!priv->timer);
	  arg.vp = sess;
	  priv->timer = timer_schedule (user_think_time_expired,
					arg, think_time);
	}
    }
}

static void
call_destroyed (Event_Type et, Object *obj, Any_Type regarg, Any_Type callarg)
{
  Sess_Private_Data *priv;
  Sess *sess;
  Call *call;

  assert (et == EV_CALL_DESTROYED && object_is_call (obj));
  call = (Call *) obj;
  sess = session_get_sess_from_call (call);
  priv = SESS_PRIVATE_DATA (sess);

  if (sess->failed)
    return;

  ++priv->num_calls_destroyed;

  if (priv->num_calls_destroyed >= priv->total_num_reqs)
    /* we're done with this session */
    sess_dec_ref (sess);
  else if (priv->num_calls_in_this_burst < priv->current_burst->num_reqs)
    issue_calls (sess, priv);
  else if (priv->num_calls_destroyed >= priv->num_calls_target)
    prepare_for_next_burst (sess, priv);
}

/* Allocates memory for a REQ and assigns values to data members.
   This is used during configuration file parsing only.  */
static REQ*
new_request (char *uristr)
{
  REQ *retptr;

  retptr = (REQ *) malloc (sizeof (*retptr));
  if (retptr == NULL || uristr == NULL)
    panic ("%s: ran out of memory while parsing %s\n",
	   prog_name, param.wsesslog.file);  

  memset (retptr, 0, sizeof (*retptr));
  retptr->uri = uristr;
  retptr->uri_len = strlen (uristr);
  retptr->method = HM_GET;
  return retptr;
}

/* Like new_request except this is for burst descriptors.  */
static BURST*
new_burst (REQ *r)
{
  BURST *retptr;
    
  retptr = (BURST *) malloc (sizeof (*retptr));
  if (retptr == NULL)
    panic ("%s: ran out of memory while parsing %s\n",
	   prog_name, param.wsesslog.file);  
  memset (retptr, 0, sizeof (*retptr));
  retptr->user_think_time = param.wsesslog.think_time;
  retptr->req_list = r;
  return retptr;
}

/* Read in session-defining configuration file and create in-memory
   data structures from which to assign uri_s to calls. */
static void
parse_config (void)
{
  FILE *fp;
  int lineno, i, reqnum;
  Sess_Private_Data *sptr;
  char line[10000];	/* some uri's get pretty long */
  char uri[10000];	/* some uri's get pretty long */
  char method_str[1000];
  char this_arg[10000];
  char contents[10000];
  double think_time;
  int bytes_read;
  REQ *reqptr;
  BURST *bptr, *current_burst = 0;
  char *from, *to, *parsed_so_far;
  int ch;
  int single_quoted, double_quoted, escaped, done;

  fp = fopen (param.wsesslog.file, "r");
  if (fp == NULL)
    panic ("%s: can't open %s\n", prog_name, param.wsesslog.file);  

  num_templates = 0;
  sptr = &session_templates[0];

  for (lineno = 1; fgets (line, sizeof (line), fp); lineno++)
    {
      if (line[0] == '#')
	continue;		/* skip over comment lines */

      if (sscanf (line,"%s%n", uri, &bytes_read) != 1)
	{
	  /* must be a session-delimiting blank line */
	  if (sptr->current_req != NULL)
	    sptr++;		/* advance to next session */
	  continue;
	}
      /* looks like a request-specifying line */
      reqptr = new_request (strdup (uri));

      if (sptr->current_req == NULL)
	{
	  num_templates++;
	  if (num_templates > MAX_SESSION_TEMPLATES)
	    panic ("%s: too many sessions (%d) specified in %s\n",
		   prog_name, num_templates, param.wsesslog.file);  
	  current_burst = sptr->current_burst = new_burst (reqptr);
	}
      else
	{
	  if (!isspace (line[0]))
	    /* this uri starts a new burst */
	    current_burst = (current_burst->next = new_burst (reqptr));
	  else
	    sptr->current_req->next = reqptr;
	}
      /* do some common steps for all new requests */
      current_burst->num_reqs++;
      sptr->total_num_reqs++;
      sptr->current_req = reqptr;

      /* parse rest of line to specify additional parameters of this
	 request and burst */
      parsed_so_far = line + bytes_read;
      while (sscanf (parsed_so_far, " %s%n", this_arg, &bytes_read) == 1)
	{
	  if (sscanf (this_arg, "method=%s", method_str) == 1)
	    {
	      for (i = 0; i < HM_LEN; i++)
		{
		  if (!strncmp (method_str,call_method_name[i],
				strlen (call_method_name[i])))
		    {
		      sptr->current_req->method = i;
		      break;
		    }
		}
	      if (i == HM_LEN)
		panic ("%s: did not recognize method '%s' in %s\n",
		       prog_name, method_str, param.wsesslog.file);  
	    }
	  else if (sscanf (this_arg, "think=%lf", &think_time) == 1)
	    current_burst->user_think_time = think_time;
	  else if (sscanf (this_arg, "contents=%s", contents) == 1)
	    {
	      /* this is tricky since contents might be a quoted
		 string with embedded spaces or escaped quotes.  We
		 should parse this carefully from parsed_so_far */
	      from = strchr (parsed_so_far, '=') + 1;
	      to = contents;
	      single_quoted = FALSE;
	      double_quoted = FALSE;
	      escaped = FALSE;
	      done = FALSE;
	      while ((ch = *from++) != '\0' && !done)
		{
		  if (escaped == TRUE)
		    {
		      switch (ch)
			{
			case 'n':
			  *to++ = '\n';
			  break;
			case 'r':
			  *to++ = '\r';
			  break;
			case 't':
			  *to++ = '\t';
			  break;
			case '\n':
			  *to++ = '\n';
			  /* this allows an escaped newline to
			     continue the parsing to the next line. */
			  if (fgets(line,sizeof(line),fp) == NULL)
			    {
			      lineno++;
			      panic ("%s: premature EOF seen in '%s'\n",
				     prog_name, param.wsesslog.file);  
			    }
			  parsed_so_far = from = line;
			  break;
			default:
			  *to++ = ch;
			  break;
			}
		      escaped = FALSE;
		    }
		  else if (ch == '"' && double_quoted)
		    {
		      double_quoted = FALSE;
		    }
		  else if (ch == '\'' && single_quoted)
		    {
		      single_quoted = FALSE;
		    }
		  else
		    {
		      switch (ch)
			{
			case '\t':
			case '\n':
			case ' ':
			  if (single_quoted == FALSE &&
			      double_quoted == FALSE)
			    done = TRUE;	/* we are done */
			  else
			    *to++ = ch;
			  break;
			case '\\':		/* backslash */
			  escaped = TRUE;
			  break;
			case '"':		/* double quote */
			  if (single_quoted)
			    *to++ = ch;
			  else
			    double_quoted = TRUE;
			  break;
			case '\'':		/* single quote */
			  if (double_quoted)
			    *to++ = ch;
			  else
			    single_quoted = TRUE;
			  break;
			default:
			  *to++ = ch;
			  break;
			}
		    }
		}
	      *to = '\0';
	      from--;		/* back up 'from' to '\0' or white-space */
	      bytes_read = from - parsed_so_far;
	      if ((sptr->current_req->contents_len = strlen (contents)) != 0)
		{
		  sptr->current_req->contents = strdup (contents);
		  snprintf (sptr->current_req->extra_hdrs,
			    sizeof(sptr->current_req->extra_hdrs),
			    "Content-length: %d\r\n",
			   sptr->current_req->contents_len);
		  sptr->current_req->extra_hdrs_len =
		    strlen (sptr->current_req->extra_hdrs);
		}
	    }
	  else
	    {
	      /* do not recognize this arg */
	      panic ("%s: did not recognize arg '%s' in %s\n",
		     prog_name, this_arg, param.wsesslog.file);  
	    }
	  parsed_so_far += bytes_read;
	}
    }
  fclose (fp);

  if (DBG > 3)
    {
      fprintf (stderr,"%s: session list follows:\n\n", prog_name);

      for (i = 0; i < num_templates; i++)
	{
	  sptr = &session_templates[i];
	  fprintf (stderr, "#session %d (total_reqs=%d):\n",
		   i, sptr->total_num_reqs);
	    
	  for (bptr = sptr->current_burst; bptr; bptr = bptr->next)
	    {
	      for (reqptr = bptr->req_list, reqnum = 0;
		   reqptr;
		   reqptr = reqptr->next, reqnum++)
		{
		  if (reqnum >= bptr->num_reqs)
		    panic ("%s: internal error detected in parsing %s\n",
			   prog_name, param.wsesslog.file);  
		  if (reqnum > 0)
		    fprintf (stderr, "\t");
		  fprintf (stderr, "%s", reqptr->uri);
		  if (reqnum == 0
		      && bptr->user_think_time != param.wsesslog.think_time)
		    fprintf (stderr, " think=%0.2f",
			     (double) bptr->user_think_time);
		  if (reqptr->method != HM_GET)
		    fprintf (stderr," method=%s",
			     call_method_name[reqptr->method]);
		  if (reqptr->contents != NULL)
		    fprintf (stderr, " contents='%s'", reqptr->contents);
		  fprintf (stderr, "\n");
		}
	    }
	  fprintf (stderr, "\n");
	}
    }
}

static void
init (void)
{
  Any_Type arg;

  parse_config ();

  sess_private_data_offset = object_expand (OBJ_SESS,
					    sizeof (Sess_Private_Data));
  rg_sess.rate = &param.rate;
  rg_sess.tick = sess_create;
  rg_sess.arg.l = 0;

  arg.l = 0;
  event_register_handler (EV_SESS_DESTROYED, sess_destroyed, arg);
  event_register_handler (EV_CALL_DESTROYED, call_destroyed, arg);

  /* This must come last so the session event handlers are executed
     before this module's handlers.  */
  session_init ();
}

static void
start (void)
{
  rate_generator_start (&rg_sess, EV_SESS_DESTROYED);
}

Load_Generator wsesslog =
  {
    "creates log-based session workload",
    init,
    start,
    no_op
  };
