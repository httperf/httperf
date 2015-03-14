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

struct Node {
	Any_Type        data;
	struct Node    *next;
};

struct List {
	struct Node    *dummy_head;
};

bool
is_list_empty(struct List *l)
{

	return l->dummy_head->next == NULL;
}

struct List    *
list_create()
{
	struct List    *l;

	if ((l = malloc(sizeof(struct List))) == NULL)
		goto create_error;

	if ((l->dummy_head = malloc(sizeof(struct Node))) == NULL)
		goto create_error;

	l->dummy_head->next = NULL;

	return l;

      create_error:
	if (l != NULL)
		free(l);

	return NULL;
}

void
list_free(struct List *l)
{
	free(l->dummy_head);
	l->dummy_head = NULL;
	free(l);
}

bool
list_push(struct List *l, Any_Type data)
{
	struct Node    *n;

	/*
	 * TODO: Implement caching so that we don't have to call
	 * malloc every time we push a new node onto the list
	 */
	if ((n = malloc(sizeof(struct Node))) == NULL) {
		return false;
	}

	n->data = data;
	n->next = l->dummy_head->next;
	l->dummy_head->next = n;

	return true;
}

Any_Type
list_top(struct List * l)
{
	return l->dummy_head->next->data;
}

Any_Type
list_pop(struct List * l)
{
	Any_Type        data;
	struct Node    *n;

	n = l->dummy_head->next;
	data = l->dummy_head->next->data;
	l->dummy_head->next = l->dummy_head->next->next;

	/*
	 * TODO: As per above, implement caching here so that this memory
	 * does not have to be freed
	 */
	free(n);

	return data;
}

void
list_remove_if_true(struct List *l, bool (*action) (Any_Type))
{
	struct Node    *n = l->dummy_head;

	while (n->next != NULL) {
		if ((*action) (n->next->data)) {
			struct Node    *oldnext = n->next;
			n->next = n->next->next;
			free(oldnext);
		} else
			n = n->next;
	}

}

void
list_for_each(struct List *l, int (*action) (Any_Type))
{
	struct Node    *n = l->dummy_head->next;

	while (n != NULL) {
		(*action) (n->data);
		n = n->next;
	}
}

