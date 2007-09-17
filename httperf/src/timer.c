/*
 * Copyright (C) 2000-2007 Hewlett-Packard Company
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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <generic_types.h>
#include <heap.h>
#include <queue.h>
#include <httperf.h>

#define HEAP_SIZE	4096
#define WHEEL_SIZE	4096

#define TIMER_INTERVAL	(1.0/1000)	/* timer granularity in seconds */
typedef void    (*Timer_Callback) (struct Timer * t, Any_Type arg);

static Time     now;
static Time     next_tick;

struct Timer {
	u_long          delta;

	/*
	 * Callback function called when timer expires (timeout) 
	 */
	Timer_Callback  timeout_callback;

	/*
	 * Typically used as a void pointer to the data object being timed 
	 */
	Any_Type        timer_subject;
};

/*
 * FIFO Queue of inactive timers 
 */
static struct Queue *passive_timers = NULL;
/*
 * Min heap of active timers 
 */
static struct Heap *active_timers = NULL;

/*
 * Executed once a timer has expired, enqueues the timer back into
 * the passive_timers queue for later use
 */
static void
done(struct Timer *t)
{
	/*
	 * Double cast.  Irritating but does the trick 
	 */
	enqueue((Any_Type) (void *) t, passive_timers);
}

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
 * Comparison callback function used by the heap data structure to correctly
 * insert the timer in the proper order (min order as in this case).
 */
_Bool
comparator(Any_Type a, Any_Type b)
{
	struct Timer   *timer_a, *timer_b;

	timer_a = (struct Timer *) a.vp;
	timer_b = (struct Timer *) b.vp;

	return timer_a->delta < timer_b->delta;
}

/*
 * Initializes a large timer pool cache
 * This is a very expensive function.  Call before beginning measurements.
 * Returns 0 upon a memory allocation error
 */
_Bool
timer_init(void)
{
	passive_timers = create_queue(WHEEL_SIZE);
	if (passive_timers == NULL)
		goto init_failure;

	active_timers = create_heap(HEAP_SIZE, &comparator);
	if (active_timers == NULL)
		goto init_failure;

	while (!is_queue_full(passive_timers)) {
		Any_Type a;
		a.vp = malloc(sizeof(struct Timer));
		
		if (a.vp == NULL)
			goto init_failure;

		enqueue(a, passive_timers);
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
	while (!is_queue_empty(passive_timers)) {
		Any_Type        a = get_front_and_dequeue(passive_timers);
		free(a.vp);
	}
	free_queue(passive_timers);

	while (!is_heap_empty(active_timers)) {
		Any_Type        a = remove_min(active_timers);
		free(a.vp);
	}
	free_heap(active_timers);
}

/*
 * Checks for timers which have had their timeout value pass and executes their
 * callback function.  The timer is then removed from the active timer list
 * and then enqueued back into the passive timer queue
 */
static void
expire_complete_timers(Any_Type a)
{
	struct Timer   *t = (struct Timer *) a.vp;
	
	if (t->delta == 0) {
		(*t->timeout_callback) (t, t->timer_subject);

		Any_Type        verify = remove_min(active_timers);

		if (verify.vp != t)
			fprintf(stderr,
				"Active timer heap is out of min order!\n\t%s.%s(%d): %s\n",
				__FILE__, __func__, __LINE__, strerror(errno));

		/*
		 * Double cast.  Irritating but does the trick 
		 */
		enqueue((Any_Type) (void *) t, passive_timers);
	}
}

/*
 * To be used to decrement a single timer delta value with the heap_for_each
 * function via a function pointer
 */
static void
decrement_timers(Any_Type a)
{
	struct Timer   *t = (struct Timer *) a.vp;

	if (t != 0)
		t->delta--;
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

	while (timer_now() >= next_tick) {
		/*
		 * Check for timers that have timed out and expire them 
		 */
		heap_for_each(active_timers, &expire_complete_timers);

		/*
		 * Decrement remaining timers
		 */
		heap_for_each(active_timers, &decrement_timers);

		next_tick += TIMER_INTERVAL;
	}
}

struct Timer   *
timer_schedule(Timer_Callback timeout, Any_Type subject, Time delay)
{
	struct Timer   *t;
	u_long          ticks;
	u_long          delta;
	Time            behind;

	if (!is_queue_empty(passive_timers)) {
		Any_Type        a = get_front_and_dequeue(passive_timers);
		t = (struct Timer *) a.vp;
	} else
		return NULL;

	memset(t, 0, sizeof(struct Timer));
	t->timeout_callback = timeout;
	t->timer_subject = subject;

	behind = (timer_now() - next_tick);
	if (behind > 0.0)
		delay += behind;

	if (delay < 0.0)
		ticks = 1;
	else {
		ticks =
		    (delay + TIMER_INTERVAL / 2.0) * (1.0 / TIMER_INTERVAL);
		if (!ticks)
			ticks = 1;	/* minimum delay is a tick */
	}

	delta = ticks / WHEEL_SIZE;
	t->delta = delta;

	insert((Any_Type) (void *) t, active_timers);

	if (DBG > 2)
		fprintf(stderr, "timer_schedule: t=%p, delay=%gs, subject=%lx\n",
			t, delay, subject.l);

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

	done(t);
}
