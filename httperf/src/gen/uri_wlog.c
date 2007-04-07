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

/* This load generator can be used to recreate a workload based on a
   server log file.
 
   This module can be used in conjunction with two comands to help in
   extracting information from the httpd CLF file (contact
   eranian@hpl.hp.com).
 
   Please note that you don't necessary need any of those tools. You
   can recreate the list of URIs by hand or with any other programs as
   long as you respect the format expected by this module (and also
   provided than you have the corresponding document tree on the
   server side).
 
   The format of the file used by this module is very simple (maybe
   too simple):

	URI1\0URI2\0......URIn\0
  
   It is a simple concatenated list of URI separated by the \0 (end of
   string) marker.
 
   This way, we don't need any parsing of the string when generating
   the URI.

   You can choose to loop on te list of URIs by using the following
   command line option to httperf:

       % httperf .... --wlog y,my_uri_file

   Otherwise httperf will stop once it reaches the end of the list.
 
   Any comment on this module contact eranian@hpl.hp.com or
   davidm@hpl.hp.com.  */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/types.h>

#include <httperf.h>
#include <call.h>
#include <core.h>
#include <event.h>

static char *fbase, *fend, *fcurrent;

static void
set_uri (Event_Type et, Call * c)
{
  int len, did_wrap = 0;
  const char *uri;

  assert (et == EV_CALL_NEW && object_is_call (c));

  do
    {
      if (fcurrent >= fend)
	{
	  if (did_wrap)
	    panic ("%s: %s does not contain any valid URIs\n",
		   prog_name, param.wlog.file);
	  did_wrap = 1;

	  /* We reached the end of the uri list so wrap around to the
	     beginning.  If not looping, also ask for the test to stop
	     as soon as possible (the current request will still go
	     out, but httperf won't wait for its reply to show up).  */
	  fcurrent = fbase;
	  if (!param.wlog.do_loop)
	    core_exit ();
	}
      uri = fcurrent;
      len = strlen (fcurrent);
      call_set_uri (c, uri, len);
      fcurrent += len + 1;
    }
  while (len == 0);

  if (verbose)
    printf ("%s: accessing URI `%s'\n", prog_name, uri);
}

void
init_wlog (void)
{
  struct stat st;
  Any_Type arg;
  int fd;

  fd = open (param.wlog.file, O_RDONLY, 0);
  if (fd == -1)
    panic ("%s: can't open %s\n", prog_name, param.wlog.file);

  fstat (fd, &st);
  if (st.st_size == 0)
    panic ("%s: file %s is empty\n", prog_name, param.wlog.file);

  /* mmap anywhere in address space: */
  fbase = (char *) mmap (0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (fbase == (char *) 0 - 1)
    panic ("%s: can't mmap the file: %s\n", prog_name, strerror (errno));

  close (fd);

  /* set the upper boundary: */
  fend = fbase + st.st_size;
  /* set current entry: */
  fcurrent = fbase;

  arg.l = 0;
  event_register_handler (EV_CALL_NEW, (Event_Handler) set_uri, arg);
}

static void
stop_wlog (void)
{
  munmap (fbase, fend - fbase);
}

Load_Generator uri_wlog =
  {
    "Generates URIs based on a predetermined list",
    init_wlog,
    no_op,
    stop_wlog
  };
