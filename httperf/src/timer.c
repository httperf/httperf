/*
 * Copyright (C) 2007 Ted Bullock <tbullock@canada.com>
 * Copyright (C) 2000-2007 Hewlett-Packard Company
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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <generic_types.h>
#include <list.h>
#include <httperf.h>

#define NUM_TIMERS 0

static Time     now;
static Time     next_tick;

struct Timer {
	Time            time_started;
	Time            timeout_delay;
	bool            has_expired;

	/*
	 * Callback function called when timer expires (timeout) 
	 */
	void            (*timeout_callback) (struct Timer * t, Any_Type arg);

	/*
	 * Typically used as a void pointer to the data object being timed 
	 */
	Any_Type        timer_subject;
};

/*
 * inactive timers 
 */
static struct List *passive_timers = NULL;
/*
 * active timers 
 */
static struct List *active_timers = NULL;
/*
 * active timeoutless timers 
 */
static struct List *persistent_timers = NULL;

/*
 * Returns the time and calls the syscall gettimeofday.  This is an expensive
 * function since it requires a context switch.  Use of the cache is
 * preferable with the timer_now function
 */
Time
timer_now_forced(void)
{
	struct timeval  tv;

	gettimeofday(&tv, 0);
	return tv.tv_sec + tv.tv_usec * 1e-6;
}

/*
 * Returns the current time. If timer caching is enabled then uses the cache.
 */
Time
timer_now(void)
{
	if (param.use_timer_cache)
		return now;
	else
		return timer_now_forced();
}

/*
 * Initializes a large timer pool cache
 * This is a very expensive function.  Call before beginning measurements.
 * Returns 0 upon a memory allocation error
 */
_Bool
timer_init(void)
{
	passive_timers = list_create();
	if (passive_timers == NULL)
		goto init_failure;

	active_timers = list_create();
	if (active_timers == NULL)
		goto init_failure;

	persistent_timers = list_create();
	if (persistent_timers == NULL)
		goto init_failure;

	for (int i = 0; i < NUM_TIMERS; i++) {
		Any_Type        a;
		a.vp = malloc(sizeof(struct Timer));

		if (a.vp == NULL)
			goto init_failure;

		list_push(passive_timers, a);
	}

	now = timer_now_forced();

	return true;

      init_failure:
	fprintf(stderr, "%s.%s: %s\n", __FILE__, __func__, strerror(errno));
	return false;

}

/*
 * Frees all allocated timers, and timer queues
 */
void
timer_free_all(void)
{
	int             count = 0;

	while (!is_list_empty(passive_timers)) {
		Any_Type        a = list_pop(passive_timers);
		fprintf(stderr,
			"passive_timers Freeing counter at address %p\n",
			a.vp);
		free(a.vp);
		count++;
	}
	list_free(passive_timers);
	passive_timers = NULL;

	while (!is_list_empty(active_timers)) {
		Any_Type        a = list_pop(active_timers);
		fprintf(stderr,
			"active_timers Freeing counter at address %p\n", a.vp);
		free(a.vp);
		count++;
	}
	list_free(active_timers);
	active_timers = NULL;

	while (!is_list_empty(persistent_timers)) {
		Any_Type        a = list_pop(persistent_timers);
		fprintf(stderr,
			"persistent_timers Freeing counter at address %p\n",
			a.vp);
		free(a.vp);
		count++;
	}
	list_free(persistent_timers);
	persistent_timers = NULL;

	if (DBG > 2)
		fprintf(stderr, "Freed a total of %d counters\n", count);
}

static int
timer_has_expired(Any_Type a)
{
	struct Timer   *t = a.vp;

	/*
	 * Only expire currently processing timers 
	 */
	if (t->has_expired == false) {
		if (t->time_started + t->timeout_delay < timer_now()) {
			t->has_expired = true;
			(*t->timeout_callback) (t, t->timer_subject);
			return 1;
		}
	} else
		return 0;
}

static int
timer_deactivate(Any_Type a)
{
	struct Timer   *t = a.vp;

	if (t->has_expired == true)
		list_push(passive_timers, a);

	return t->has_expired;
}

/*
 * Checks for timers which have had their timeout value pass and executes their
 * callback function.  The timer is then removed from the active timer list
 * and then enqueued back into the passive timer queue
 */
void
timer_tick(void)
{
	now = timer_now_forced();
	list_for_each(active_timers, &timer_has_expired);
	list_remove_if_true(active_timers, &timer_deactivate);
}

struct Timer   *
timer_schedule(void (*timeout) (struct Timer * t, Any_Type arg),
	       Any_Type subject, Time delay)
{
	struct Timer   *t;

	if (!is_list_empty(passive_timers)) {
		Any_Type        a = list_pop(passive_timers);
		t = (struct Timer *) a.vp;
	} else if ((t = malloc(sizeof(struct Timer))) == NULL)
		return NULL;

	memset(t, 0, sizeof(struct Timer));
	t->timeout_callback = timeout;
	t->has_expired = false;
	t->timer_subject = subject;
	t->time_started = timer_now();
	t->timeout_delay = delay;

	if (delay > 0)
		list_push(active_timers, (Any_Type) (void *) t);
	else
		list_push(persistent_timers, (Any_Type) (void *) t);

	if (DBG > 2)
		fprintf(stderr,
			"timer_schedule: t=%p, delay=%gs, subject=%lx\n", t,
			delay, subject.l);

	return t;
}

void
timer_cancel(struct Timer *t)
{
	if (DBG > 2)
		fprintf(stderr, "timer_cancel: t=%p\n", t);

	/*
	 * A module MUST NOT call timer_cancel() for a timer that is currently 
	 * being processed (whose timeout has expired).  
	 */

	t->has_expired = true;
}
