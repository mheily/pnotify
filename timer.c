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

#include "pnotify.h"
#include "pnotify-internal.h"
#include "queue.h"
#include "thread.h"


struct pn_timer {

	/** The current amount of time left until the timer expires (in seconds) */
	int remaining;

	/** The watch associated with the timer event */
	struct pn_watch *watch;

	/** Pointers to the next and previous list entries */
	LIST_ENTRY(pn_timer) entries;
};


/** A list of all active timers */
LIST_HEAD(, pn_timer) TIMER;

/** The smallest interval (in seconds) of all timers in the TIMER list. */
size_t TIMER_INTERVAL = 1;

/** A mutex to protect all global TIMER variables */
pthread_mutex_t TIMER_MUTEX = PTHREAD_MUTEX_INITIALIZER;

/** Cause a SIGALRM signal to be generated every TIMER_INTERVAL */
static void
timer_enable()
{
	struct itimerval itv;

	itv.it_value.tv_sec = TIMER_INTERVAL;
	itv.it_value.tv_usec = 0;
	itv.it_interval.tv_sec = TIMER_INTERVAL;
	itv.it_interval.tv_usec = 0;

	if (setitimer(ITIMER_REAL, &itv, NULL) != 0) 
		err(1, "setitimer(2)");
}


/** Disable the periodic alarm timer */
static void
timer_disable()
{
	struct itimerval itv;

	memset(&itv, 0, sizeof(itv));

	if (setitimer(ITIMER_REAL, &itv, NULL) != 0) 
		err(1, "setitimer(2)");
}


int
pn_add_timer(struct pn_watch *watch)
{
	struct pn_timer *timer;

	/* Allocate a new timer struct */
	if ((timer = calloc(1, sizeof(*timer))) == NULL) {
		warn("malloc(3)");
		return -1;
	}
	timer->remaining = watch->ident.interval;
	timer->watch = watch;

	pthread_mutex_lock(&TIMER_MUTEX);

	/* Enable the periodic timer if this is the first entry */
	if (LIST_EMPTY(&TIMER))
		timer_enable();

	/* Add the timer to the list */
	LIST_INSERT_HEAD(&TIMER, timer, entries);

	pthread_mutex_unlock(&TIMER_MUTEX);

	return 0;
}


int
pn_rm_timer(struct pn_watch *watch)
{
	struct pn_timer *timer, *tmp;

	pthread_mutex_lock(&TIMER_MUTEX);

	/* Disable the periodic timer if there are no more timers */
	if (LIST_EMPTY(&TIMER)) 
		timer_disable();

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


void
pn_timer_init(void)
{
	LIST_INIT(&TIMER);
}

void *
pn_timer_loop(void * unused)
{
	struct pnotify_event *evt;
	struct pn_timer *timer;
	sigset_t signal_set;
	int signum;

	/* Avoid a compiler warning */
	evt = unused;

	/* Loop forever waiting for an alarm signal */
	for (;;) {

		/* Wait for SIGALRM */
		sigemptyset(&signal_set);
		sigaddset(&signal_set, SIGALRM);
		sigwait(&signal_set, &signum);
		
		/* Reduce the time remaining on all timers */
		pthread_mutex_lock(&TIMER_MUTEX);
		LIST_FOREACH(timer, &TIMER, entries) {

			/* If the timer has expired, generate an event ... */
			if (TIMER_INTERVAL > timer->remaining) {

				/* Create a new event structure */
				if ((evt = calloc(1, sizeof(*evt))) == NULL)
					err(1, "calloc(3)");
				evt->watch = timer->watch;
				evt->mask = PN_TIMEOUT;

				/* Add the event to an event queue */
				pn_event_add(timer->watch->ctx, evt);

				/* Reset the timer to it's initial value */
				timer->remaining = timer->watch->ident.interval;
			}

			/* Otherwise, decrease the timer value */
			else {
				timer->remaining -= TIMER_INTERVAL;
			}
		}
		pthread_mutex_unlock(&TIMER_MUTEX);
	}

	return NULL;
}
