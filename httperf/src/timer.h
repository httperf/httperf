/*
 * Copyright (C) 2007 Ted Bullock <tbullock@comlore.com>
 * Copyright (C) 2000 Hewlett-Packard Company
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

#ifndef timer_h
#define timer_h

struct Timer;
typedef void    (*Timer_Callback) (struct Timer * t, Any_Type arg);

Time     timer_now_forced(void);
Time     timer_now(void);

bool      timer_init(void);
void     timer_reset_all(void);
void     timer_free_all(void);
/*
 * Needs to be called at least once every TIMER_INTERVAL: 
 */
void     timer_tick(void);

struct Timer   *timer_schedule(Timer_Callback timeout, Any_Type arg,
			       Time delay);
void     timer_cancel(struct Timer * t);

#endif /* timer_h */
