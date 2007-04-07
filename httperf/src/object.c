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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <httperf.h>
#include <call.h>
#include <conn.h>
#include <event.h>
#include <object.h>
#include <sess.h>

#define ALIGN(s)	(((s) + sizeof (double) - 1) & ~(sizeof (double) - 1))

static size_t type_size[OBJ_NUM_TYPES] =
  {
    ALIGN (sizeof (Conn)),
    ALIGN (sizeof (Call)),
    ALIGN (sizeof (Sess))
  };

struct free_list_el
  {
    struct free_list_el *next;
  };

static struct free_list_el *free_list[OBJ_NUM_TYPES];

static void
object_destroy (Object *obj)
{
  Object_Type type = obj->type;
  struct free_list_el *el;
  Event_Type event = 0;
  Any_Type arg;

  switch (type)
    {
    case OBJ_CALL:
      call_deinit ((Call *) obj);
      event = EV_CALL_DESTROYED;
      break;

    case OBJ_CONN:
      conn_deinit ((Conn *) obj);
      event = EV_CONN_DESTROYED;
      break;

    case OBJ_SESS:
      sess_deinit ((Sess *) obj);
      event = EV_SESS_DESTROYED;
      break;

    default:
      assert (0);
      break;
    }
  arg.l = 0;
  event_signal (event, obj, arg);

  /* Each object must be at least the size and alignment of "struct
     free_list_el".  Malloc takes care of returning properly aligned
     objects.  */
  el = (struct free_list_el *) obj;
  el->next = free_list[type];
  free_list[type] = el;
}

size_t
object_expand (Object_Type type, size_t size)
{
  size_t offset = type_size[type];
  type_size[type] += ALIGN (size);
  return offset;
}

Object *
object_new (Object_Type type)
{
  struct free_list_el *el;
  Event_Type event = 0;
  size_t obj_size;
  Any_Type arg;
  Object *obj;

  obj_size = type_size[type];

  if (free_list[type])
    {
      el = free_list[type];
      free_list[type] = el->next;
      obj = (Object *) el;
    }
  else
    {
      obj = malloc (obj_size);
      if (!obj)
	{
	  fprintf (stderr, "%s.object_new: %s\n", prog_name, strerror (errno));
	  return 0;
	}
    }
  memset (obj, 0, obj_size);
  obj->ref_count = 1;
  obj->type = type;
  switch (type)
    {
    case OBJ_CALL:
      call_init ((Call *) obj);
      event = EV_CALL_NEW;
      break;

    case OBJ_CONN:
      conn_init ((Conn *) obj);
      event = EV_CONN_NEW;
      break;

    case OBJ_SESS:
      sess_init ((Sess *) obj);
      event = EV_SESS_NEW;
      break;

    default:
      panic ("object_new: bad object type %d\n", type);
      break;
    }
  arg.l = 0;
  event_signal (event, obj, arg);
  return obj;
}

void
object_dec_ref (Object *obj)
{
  assert (obj->ref_count > 0);

  if (--obj->ref_count == 0)
    object_destroy (obj);
}
