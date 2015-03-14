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

#include "config.h"

#include <stdlib.h>

#include <generic_types.h>

#define Minimum_Heap_Size 10

struct Heap {
	u_long          array_size;
	u_long          num_elements;
	bool            (*compare) (Any_Type, Any_Type);
	Any_Type        storage[];	/* c99 Flexible Array Member */
};

struct Heap    *
create_heap(u_long size, bool(*compare_callback) (Any_Type, Any_Type))
{
	struct Heap    *h;

	if (size < Minimum_Heap_Size)
		size = Minimum_Heap_Size;

	/*
	 * We are using c99 Flexible Array Members so we can do this :) 
	 */
	h = malloc(sizeof(struct Heap) + sizeof(Any_Type) * size);
	if (!h)
		return NULL;

	h->array_size = size;
	h->num_elements = 0;
	h->compare = compare_callback;

	return h;
}

bool
is_heap_empty(struct Heap * h)
{
	return h->num_elements == 0;
}

bool
is_heap_full(struct Heap * h)
{
	return h->array_size == h->num_elements;
}

u_long
num_heap_elements(struct Heap * h)
{
	return h->num_elements;
}

void
free_heap(struct Heap *h)
{
	if (h != NULL)
		free(h);
}

#define PARENT(i)	(i/2)
bool
insert(Any_Type a, struct Heap *h)
{
	u_long          i;

	if (is_heap_full(h))
		return false;

	i = ++h->num_elements;

	/*
	 * find the correct place to insert
	 */
	while ((i > 1) && (*h->compare) (h->storage[PARENT(i)], a)) {
		h->storage[i] = h->storage[PARENT(i)];
		i = PARENT(i);
	}
	h->storage[i] = a;
	return true;
}

static void
percolate(struct Heap *h, u_long hole)
{
	int             child;
	Any_Type        dest_val = h->storage[hole];

	for (; hole * 2 <= h->num_elements; hole = child) {
		child = hole * 2;
		if (child != h->num_elements
		    && (*h->compare) (h->storage[child + 1],
				      h->storage[child + 1]))
			child++;
		if ((*h->compare) (h->storage[child + 1], dest_val))
			h->storage[hole] = h->storage[child];
		else
			break;
	}
	h->storage[hole] = dest_val;
}

Any_Type
remove_min(struct Heap *h)
{
	if (is_heap_empty(h)) {
		Any_Type temp = {0};
		return temp;
	}
	else {
		Any_Type        min = h->storage[1];
		h->storage[1] = h->storage[h->num_elements--];
		percolate(h, 1);

		return min;
	}
}

Any_Type
poll_min(struct Heap * h)
{
	if (is_heap_empty(h)) {
		Any_Type temp = {0};
		return temp;
	}
	else
		return h->storage[1];
}

void
heap_for_each(struct Heap *h, void (*action) (Any_Type))
{
	for (u_long i = 1; i <= h->num_elements; i++) {
		(*action) (h->storage[i]);
	}
}
