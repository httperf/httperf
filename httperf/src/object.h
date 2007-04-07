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
#ifndef object_h
#define object_h

#ifdef DEBUG
# define object_is_conn(o)	(((Object *) (o))->type == OBJ_CONN)
# define object_is_call(o)	(((Object *) (o))->type == OBJ_CALL)
# define object_is_sess(o)	(((Object *) (o))->type == OBJ_SESS)
#else
# define object_is_conn(o)	1
# define object_is_call(o)	1
# define object_is_sess(o)	1
#endif

typedef enum Object_Type
  {
    OBJ_CONN,			/* connection object */
    OBJ_CALL,			/* call object */
    OBJ_SESS,			/* session object */
    OBJ_NUM_TYPES
  }
Object_Type;

typedef struct Object
  {
    Object_Type type;
    u_int ref_count;			/* # of references to this object */
  }
Object;

/* This may be called during httperf initialize to reserve SIZE
   "private" bytes in objects of type TYPE.  The return value is the
   offset of the private area.  */

extern size_t object_expand (Object_Type type, size_t size);

/* Create a new object of type TYPE.  */
extern Object *object_new (Object_Type type);

/* Create a new reference for object OBJ.  */
#define object_inc_ref(o)	(++(o)->ref_count)

/* Decrement the reference for object OBJ.  If the reference count
   reaches zero, the object's destroy function is called.  */
extern void object_dec_ref (Object *obj);

#endif /* object_h */
