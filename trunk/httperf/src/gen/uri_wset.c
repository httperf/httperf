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

/* Causes accesses to a fixed set of files (working set) in such a way
   that is likely to cause disk I/O with a certain probability.  */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <httperf.h>
#include <call.h>
#include <event.h>

#define MAX_URI_LEN		128
#define CALL_PRIVATE_DATA(c) \
 ((void *) ((char *)(c) + call_private_data_offset))

static double miss_prob;
static unsigned file_num;
static size_t call_private_data_offset;
static size_t uri_prefix_len;

static void
set_uri (Event_Type et, Call *c)
{
  char *cp, *buf_end;
  unsigned j, n;

  assert (et == EV_CALL_NEW && object_is_call (c));

  miss_prob += param.wset.target_miss_rate;
  if (miss_prob >= 1.0)
    {
      /* generate (what we hope to be) a miss */
      miss_prob -= 1.0;
      file_num += param.client.num_clients;
      if (file_num >= param.wset.num_files)
	file_num -= param.wset.num_files;
    }

  /* fill in extension: */
  buf_end = (char *) CALL_PRIVATE_DATA (c) + MAX_URI_LEN;
  cp = buf_end - 6;
  memcpy (cp, ".html", 6);

  /* fill in file & pathname: */
  n = file_num;
  for (j = 1; j < param.wset.num_files; j *= 10, n /= 10)
    {
      cp -= 2;
      cp[0] = '/'; cp[1] = '0' + (n % 10);
    }

  /* fill in the uri prefix specified by param.uri: */
  cp -= uri_prefix_len;
  if (cp < (char *) CALL_PRIVATE_DATA (c))
    {
      fprintf (stderr, "%s.uri_wset: URI buffer overflow!\n", prog_name);
      exit (1);
    }
  memcpy (cp, param.uri, uri_prefix_len);

  call_set_uri (c, cp, (buf_end - cp) - 1);

  if (verbose)
    printf ("%s: accessing URI `%s'\n", prog_name, cp);
}

static void
init (void)
{
  Any_Type arg;

  miss_prob = drand48 ();
  file_num = param.client.id;

  call_private_data_offset = object_expand (OBJ_CALL, MAX_URI_LEN);

  uri_prefix_len = strlen (param.uri);
  if (param.uri[uri_prefix_len - 1] == '/')
    {
      ++param.uri;
      --uri_prefix_len;
    }

  arg.l = 0;
  event_register_handler (EV_CALL_NEW, (Event_Handler) set_uri, arg);
}

Load_Generator uri_wset =
  {
    "Generates URIs accessing a working-set at a given rate",
    init,
    no_op,
    no_op
  };
