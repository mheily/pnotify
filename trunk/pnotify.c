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

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include "pnotify.h"
#include "pnotify-internal.h"

/** @file
 *
 *  All externally visible functions in the pnotify API.
 *
*/

/* All worker threads */
pthread_t *WORKER;
size_t WORKER_COUNT = 0;

/** A global list of events that are ready to be delivered */
STAILQ_HEAD(, event) EVENT;
pthread_mutex_t EVENT_MUTEX;
pthread_cond_t EVENT_COND;

/* Define the system-specific vtable.  */
#if defined(BSD)
const struct pnotify_vtable * const sys = &BSD_VTABLE;
#elif defined(__linux__)
const struct pnotify_vtable * const sys = &LINUX_VTABLE;
#endif

/** A list of watched files and/or directories */
LIST_HEAD(pnwatchhead, watch) WATCH;
pthread_mutex_t WATCH_MUTEX = PTHREAD_MUTEX_INITIALIZER;

/* Defined in timer.c */
extern LIST_HEAD(, pn_timer) TIMER;

static int
get_cpu_count(void)
{
#if defined(BSD)
	FILE *f;
	char buf[3];

	if ((f = popen( "/sbin/sysctl -n hw.ncpu", "r")) == NULL)
		err(1, "popen(3)");
	if ((fgets((char *) &buf, sizeof(buf), f)) == NULL)
		err(1, "fgets(3)");
	(void) fclose(f);

	/* FIXME - error checking */
	return ((int) strtol((char *) &buf, NULL, 10));
#endif
	
	return 1;
}

static void
pnotify_init_once(void)
{
	pthread_t tid;
	int i;

	/* Block all signals */
	pn_mask_signals();

	/* Create a dedicated signal handling thread */
	if (pthread_create( &tid, NULL, pn_signal_loop, NULL ) != 0)
		errx(1, "pthread_create(3) failed");

	/* Create a dedicated timer thread */
	if (pthread_create( &tid, NULL, pn_timer_loop, NULL ) != 0)
		errx(1, "pthread_create(3) failed");

	/* Create a pool of worker threads */
	WORKER_COUNT = get_cpu_count();
	WORKER = calloc(WORKER_COUNT, sizeof(pthread_t));
	for (i = 0; i < WORKER_COUNT; i++) {
		if (pthread_create(&WORKER[i], NULL, (void *(*)(void *)) event_dispatch, NULL) != 0)
			errx(1, "pthread_create(3) failed");
	}

	/* Initialize global data structures */
	LIST_INIT(&WATCH);
	LIST_INIT(&TIMER);
	STAILQ_INIT(&EVENT);

	/* Initialize synchronization primitives */
	if (pthread_mutex_init(&EVENT_MUTEX, NULL) != 0) {
		warn("pthread_mutex_init(3) failed");
		goto err1;
	}
	if (pthread_cond_init(&EVENT_COND, NULL) != 0) {
		warn("pthread_cond_init(3) failed");
		goto err2;
	}

	/* Perform system-specific initialization */
	sys->init_once();

	return;

err2:
	(void) pthread_cond_destroy(&EVENT_COND);

err1:
	(void) pthread_mutex_destroy(&EVENT_MUTEX);
}


void
pnotify_init(void)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	/* Perform one-time initialization */
	pthread_once(&once, pnotify_init_once);
}


int
pnotify_add_watch(struct watch *watch)
{
	/* Register the watch with the kernel */
	if (sys->add_watch(watch) < 0) {
		warn("adding watch failed");
		return -1;
	}

	/* Set a timer (this is not a system-dependent function) */
	if (watch->type == WATCH_TIMER && pn_add_timer(watch) != 0) {
		warnx("unable to add timer");
		return -1;
	}

	else if (watch->type == WATCH_SIGNAL) {
		pthread_mutex_lock(&WATCH_MUTEX);
		SIG_WATCH[watch->ident] = watch;
		pthread_mutex_unlock(&WATCH_MUTEX);
	}


	/* Add the watch to the watchlist */
	pthread_mutex_lock(&WATCH_MUTEX);
	LIST_INSERT_HEAD(&WATCH, watch, entries);
	pthread_mutex_unlock(&WATCH_MUTEX);

	return 0;
}


int 
watch_cancel(struct watch *watch)
{
	/* Unregister the kernel event */
	/* TODO: error handling in this switch statement */
	switch (watch->type) {
		case WATCH_TIMER: 
			(void) pn_rm_timer(watch);
			break;

		default: 
			(void) sys->rm_watch(watch);
			break;
	}

	/* Remove from the global watchlist */
	MUTEX_LOCK(WATCH_MUTEX);
	LIST_REMOVE(watch, entries);
	MUTEX_UNLOCK(WATCH_MUTEX);

	return 0;
}


struct event * 
event_wait(void)
{ 
	struct event *evp;

	/* Wait for an event to be added to the queue */
	MUTEX_LOCK(EVENT_MUTEX);
retry:
	if (pthread_cond_wait(&EVENT_COND, &EVENT_MUTEX) != 0) {
		warn("pthread_cond_wait(3) failed");
		return NULL;
	}

	/* Shift the first element off of the pending event queue */
	if ((evp = STAILQ_FIRST(&EVENT)) != NULL) {
		STAILQ_REMOVE_HEAD(&EVENT, entries);
		MUTEX_UNLOCK(EVENT_MUTEX);
		return evp;
	}

	/* Handle a spurious wakeup */
	goto retry;
}


static struct watch *
_watch_add(enum pn_watch_type wtype, int fd, const char *path, void (*cb)(), void *arg)
{
	struct watch *w;

	assert(cb);

	/* Generate the watch */
	if ((w = calloc(1, sizeof(*w))) == NULL) 
		return NULL;
	w->type = wtype;
	w->cb = cb;
	w->arg = arg;
	w->ident = fd;

	/* Add the watch */
	if (pnotify_add_watch(w) != 0) {
		free(w);
		return NULL;
	}

	return (w);

}


struct watch *
watch_fd(int fd, void (*cb)(int, int, void *), void *arg)
{
	return _watch_add(WATCH_FD, fd, NULL, cb, arg);
}


struct watch *
watch_timer(int interval, void (*cb)(void *), void *arg)
{
	return _watch_add(WATCH_TIMER, interval, NULL, cb, arg);
}

struct watch *
watch_signal(int signum, void (*cb)(int, void *), void *arg)
{
	return _watch_add(WATCH_SIGNAL, signum, NULL, cb, arg);
}

void
event_dispatch(void)
{
	struct event *evt;

	for (;;) {
		/* Wait for an event */
		if ((evt = event_wait()) == NULL)
			abort();

		if (evt->watch->type == WATCH_TIMER) {
			evt->watch->cb(evt->mask, evt->watch->arg);
		} else {
			evt->watch->cb(evt->watch->ident, evt->mask, evt->watch->arg);
		}
	}

}


void
pn_event_add(struct watch *watch, int mask)
{
	struct event *evt;

	dprintf("adding an event to the eventlist..\n");

	/* Create a new event structure */
	if ((evt = calloc(1, sizeof(*evt))) == NULL)
		err(1, "calloc(3)");
	evt->watch = watch;
	evt->mask = mask;

	/* Assign the event */
	MUTEX_LOCK(EVENT_MUTEX);
	STAILQ_INSERT_TAIL(&EVENT, evt, entries);
	MUTEX_UNLOCK(EVENT_MUTEX);

	/* Signal a worker thread to process the event */
	(void) pthread_cond_signal(&EVENT_COND);
}
