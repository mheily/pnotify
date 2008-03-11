/*		$Id: $		*/

/*
 * Copyright (c) 2007 Mark Heily <devel@heily.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/** @file
 *
 * Timer functions to support the PN_TIMEOUT event class
 */

#include <sys/time.h>

#include "pnotify.h"
#include "pnotify-internal.h"


struct pn_timer {
	time_t expires;		 /** The time after which the timer expires */
	struct watch *watch;	 /** The watch associated with the timer event */
	LIST_ENTRY(pn_timer) entries; /** Pointers to the next and previous list entries */
};


/** A list of all active timers */
LIST_HEAD(, pn_timer) TIMER;

/** The smallest interval (in seconds) of all timers in the TIMER list. */
size_t TIMER_INTERVAL = 1;

/** A mutex to protect all global TIMER variables */
pthread_mutex_t TIMER_MUTEX = PTHREAD_MUTEX_INITIALIZER;

int
pn_add_timer(struct watch *watch)
{
	struct pn_timer *timer;

	/* Allocate a new timer struct */
	if ((timer = calloc(1, sizeof(*timer))) == NULL) {
		warn("malloc(3)");
		return -1;
	}
	timer->expires = time(NULL) + watch->ident;
	timer->watch = watch;

	pthread_mutex_lock(&TIMER_MUTEX);

	/* Enable the periodic timer if this is the first entry */
	//if (LIST_EMPTY(&TIMER))
	//	timer_enable();

	/* Add the timer to the list */
	LIST_INSERT_HEAD(&TIMER, timer, entries);

	pthread_mutex_unlock(&TIMER_MUTEX);

	return 0;
}


int
pn_rm_timer(struct watch *watch)
{
	struct pn_timer *timer, *tmp;

	pthread_mutex_lock(&TIMER_MUTEX);

	/* Disable the periodic timer if there are no more timers */
//	if (LIST_EMPTY(&TIMER)) 
//		timer_disable();

	/* Remove the timer struct */
	LIST_FOREACH_SAFE(timer, &TIMER, entries, tmp) {
		if (timer->watch == watch) {
			LIST_REMOVE(timer, entries);
			break;
		}
	}

	pthread_mutex_unlock(&TIMER_MUTEX);

	return 0;
}


void *
pn_timer_loop(void *unused)
{
	struct pn_timer *timer, *tmp;
	time_t now;

	/* Loop forever marking time */
	for (;;) {

		/* Wait for SIGALRM */
		sleep(TIMER_INTERVAL);
		now = time(NULL);
		dprintf("checking timer..\n");
		
		/* Check the time remaining on all timers */
		pthread_mutex_lock(&TIMER_MUTEX);
		LIST_FOREACH_SAFE(timer, &TIMER, entries, tmp) {

			/* If the timer has expired, generate an event ... */
			if (now > timer->expires) {

				/* Add the event to an event queue */
				pn_event_add(timer->watch, PN_TIMEOUT);

				/* Remove the timer */

				/* Delete the watch*/
				watch_cancel(timer->watch);

				/* Delete the timer entry */
				LIST_REMOVE(timer, entries);
				free(timer);

			}
		}

		/* Disable the periodic timer if there are no more timers */
		//if (LIST_EMPTY(&TIMER)) 
		//	timer_disable();

		pthread_mutex_unlock(&TIMER_MUTEX);
	}

	return NULL;
}
