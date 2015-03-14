/*
 * Copyright (C) 2007 Ted Bullock <tbullock@canada.com>
 * 
 * This file is part of httperf, a web server performance measurment tool.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * In addition, as a special exception, the copyright holders give permission
 * to link the code of this work with the OpenSSL project's "OpenSSL" library
 * (or with modified versions of it that use the same license as the "OpenSSL" 
 * library), and distribute linked combinations including the two.  You must
 * obey the GNU General Public License in all respects for all of the code
 * used other than "OpenSSL".  If you modify this file, you may extend this
 * exception to your version of the file, but you are not obligated to do so.
 * If you do not wish to do so, delete this exception statement from your
 * version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT 
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA 
 */

#ifndef heap_h
#define heap_h

struct Heap;

typedef         bool(*heap_compare) (Any_Type, Any_Type);
typedef void    (*heap_for_each_action) (Any_Type);

struct Heap    *create_heap(u_long, heap_compare);
bool            is_heap_empty(struct Heap *);
bool            is_heap_full(struct Heap *);
u_long          num_heap_elements(struct Heap *);
void            free_heap(struct Heap *);

bool            insert(Any_Type, struct Heap *);
Any_Type        remove_min(struct Heap *);
Any_Type        poll_min(struct Heap *);

void            heap_for_each(struct Heap *, heap_for_each_action);

#endif /* Heap_h */
