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

#define Minimum_Wheel_Size 10

struct Queue {
	u_long          wheel_size;
	u_long          num_elements;
	u_long          first_element;
	u_long          last_element;
	Any_Type        wheel[];	/* c99 Flexible Array Member */
};

static void
empty_queue(struct Queue *q)
{
	q->num_elements = 0;
	q->first_element = 1;
	q->last_element = 0;

	for (u_long i = 0; i < q->wheel_size; i++)
		q->wheel[i].uw = 0;
}

struct Queue   *
create_queue(u_long size)
{
	struct Queue   *q;

	if (size < Minimum_Wheel_Size)
		size = Minimum_Wheel_Size;

	/*
	 * Again, we are using c99 Flexible Array Members so we can do this :) 
	 */
	q = malloc(sizeof(struct Queue) + sizeof(Any_Type) * size);
	if (q == NULL)
		return NULL;
	
	q->wheel_size = size;
	empty_queue(q);

	return q;
}

int
is_queue_empty(struct Queue *q)
{
	return q->num_elements == 0;
}

int
is_queue_full(struct Queue *q)
{
	return q->num_elements == q->wheel_size;
}

void
free_queue(struct Queue *q)
{
	if (q != NULL) {
		empty_queue(q);
		free(q);
	}
}

int
enqueue(Any_Type a, struct Queue *q)
{
	if (is_queue_full(q))
		return 0;
	
	q->num_elements++;

	if (++q->last_element == q->wheel_size)
		q->last_element = 0;

	q->wheel[q->last_element] = a;

	return 1;
}

void
dequeue(struct Queue *q)
{
	q->num_elements--;

	if (++q->first_element == q->wheel_size)
		q->first_element = 0;

}

Any_Type
get_front(struct Queue *q)
{
	return q->wheel[q->first_element];
}

Any_Type
get_front_and_dequeue(struct Queue * q)
{
	Any_Type        a = get_front(q);
	dequeue(q);

	return a;

}
