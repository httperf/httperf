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

/* Implements miscellaneous command-line specified operations.  So
   far, the following options are implemented here:
   
	--add-header	Adds one or more command-line specified header(s)
			to each call request.

	--method	Sets the method to be used when performing a
			call.  */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <httperf.h>
#include <call.h>
#include <event.h>

static const char *extra;
static size_t extra_len;

static size_t method_len;

/* A simple module that collects cookies from the server responses and
   includes them in future calls to the server.  */

static const char *
unescape (const char *str, size_t *len)
{
  char *dp, *dst = strdup (str);
  const char *cp;
  int ch;

  if (!dst)
    panic ("%s: strdup() failed: %s\n", prog_name, strerror (errno));

  for (cp = str, dp = dst; (ch = *cp++); )
    {
      if (ch == '\\')
	{
	  ch = *cp++;
	  switch (ch)
	    {
	    case '\\':	/* \\ -> \ */
	      break;

	    case 'a':	/* \a -> LF */
	      ch = 10;
	      break;

	    case 'r':	/* \r -> CR */
	      ch = 13;
	      break;

	    case 'n':	/* \n -> CR/LF */
	      *dp++ = 13;
	      ch = 10;
	      break;

	    case '0': case '1': case '2': case '3': case '4':
	    case '5': case '6': case '7': case '8': case '9':
	      ch = strtol (cp - 1, (char **) &cp, 8);
	      break;

	    default:
	      fprintf (stderr, "%s: ignoring unknown escape sequence "
		       "`\\%c' in --add-header\n", prog_name, ch);
	      break;
	    }
	}
      *dp++ = ch;
    }
  *len = dp - dst;
  return dst;
}

static void
call_created (Event_Type et, Object *obj, Any_Type reg_arg, Any_Type arg)
{
  Call *c = (Call *) obj;

  assert (et == EV_CALL_NEW && object_is_call (obj));

  if (method_len > 0)
    call_set_method (c, param.method, method_len);

  if (extra_len > 0)
    call_append_request_header (c, extra, extra_len);
}


static void
init (void)
{
  Any_Type arg;

  if (param.additional_header)
    extra = unescape (param.additional_header, &extra_len);

  if (param.method)
    method_len = strlen (param.method);

  arg.l = 0;
  event_register_handler (EV_CALL_NEW, call_created, arg);
}

Load_Generator misc =
  {
    "Miscellaneous command line options",
    init,
    no_op,
    no_op
  };
