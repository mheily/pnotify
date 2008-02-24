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
#include "queue.h"
#include "thread.h"

/** @file
 *
 *  All externally visible functions in the pnotify API.
 *
*/

/** A global pnotify context variable
 *
 * This variable must always be accessed using the GET and SET methods.
 */
pthread_key_t CTX_KEY;


/* Define the system-specific vtable.  */
#if HAVE_KQUEUE
const struct pnotify_vtable * const sys = &BSD_VTABLE;
#else
const struct pnotify_vtable * const sys = &LINUX_VTABLE;
#endif

/** A list of watched files and/or directories */
LIST_HEAD(pnwatchhead, watch) WATCH;
pthread_mutex_t WATCH_MUTEX = PTHREAD_MUTEX_INITIALIZER;


void
pnotify_init_once(void)
{
	pthread_t tid;

	/* Initialize the TLS key */
	if (pthread_key_create(&CTX_KEY, NULL) != 0) 
		errx(1, "error creating TLS key");

	/* Block all signals */
	pn_mask_signals();

	/* Create a dedicated signal handling thread */
	if (pthread_create( &tid, NULL, pn_signal_loop, NULL ) != 0)
		errx(1, "pthread_create(3) failed");

	/* Create a dedicated timer thread */
	if (pthread_create( &tid, NULL, pn_timer_loop, NULL ) != 0)
		errx(1, "pthread_create(3) failed");

	/* Initialize lists */
	LIST_INIT(&WATCH);
	pn_timer_init();

	/* Perform system-specific initialization */
	sys->init_once();
}

/* 
 * FIXME: 
 * need to call free() and pthread_mutex_destroy() in
 * the error handing paths.
 */
struct pnotify_ctx *
pnotify_init()
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	struct pnotify_ctx *ctx;

	/* Perform one-time initialization */
	pthread_once(&once, pnotify_init_once);

	/* Allocate a new context structure */
	if ((ctx = calloc(1, sizeof(*ctx))) == NULL) {
		warn("calloc(3) failed");
		return NULL;
	}

	/* Initialize the mutex */
	if (pthread_mutex_init(&ctx->mutex, NULL) != 0) {
		warn("pthread_mutex_init(3) failed");
		return NULL;
	}

	/* Initialize the counting semaphore */
	if (sem_init(&ctx->event_count, 0, 0) != 0) {
		warn("sem_init(3) failed");
		return NULL;
	}
		
	TAILQ_INIT(&ctx->event);

	/* Set the global per-thread context variable */
	CTX_SET(ctx);

	/* Push the cleanup routine on the stack */
	//FIXME: macro error: pthread_cleanup_push(pnotify_free, ctx);

	return ctx;
}


int
pnotify_add_watch(struct watch *watch)
{
	/* Get the context */
	if (!watch->ctx)
		watch->ctx = CTX_GET();
	assert(watch->ctx);

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
		SIG_WATCH[watch->ident.signum] = watch;
		pthread_mutex_unlock(&WATCH_MUTEX);
	}


	/* Add the watch to the watchlist */
	pthread_mutex_lock(&WATCH_MUTEX);
	LIST_INSERT_HEAD(&WATCH, watch, entries);
	pthread_mutex_unlock(&WATCH_MUTEX);

	dprintf("added watch: mask=%d\n", watch->mask);

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
	pthread_mutex_lock(&WATCH_MUTEX);
	LIST_REMOVE(watch, entries);
	pthread_mutex_unlock(&WATCH_MUTEX);

	return 0;
}


int
event_wait(struct event * evt)
{ 
	struct pnotify_ctx * ctx;
	struct event *evp;

	assert(evt);

	ctx = CTX_GET();

	/* Wait for an event to be added to the queue */
retry:
	if (sem_wait(&ctx->event_count) != 0) {
		if (errno == EINTR)
			goto retry;
		warn("sem_wait(3) failed");
		return -1;
	}
		

	/* Shift the first element off of the pending event queue */
	mutex_lock(ctx);
	if (!TAILQ_EMPTY(&ctx->event)) {
		evp = TAILQ_FIRST(&ctx->event);
		TAILQ_REMOVE(&ctx->event, evp, entries);
		mutex_unlock(ctx);
		memcpy(evt, evp, sizeof(*evt));
		free(evp);
		return 0;
	} else {
		warnx("spurious wakeup");
		mutex_unlock(ctx);
		return -1;
	}
}


int
pnotify_print_event(struct event * evt)
{
	assert(evt);

	return printf("event: fd=%d mask=(%s%s%s%s%s)\n",
		      evt->watch->ident.fd,
		      evt->mask & PN_ATTRIB ? "attrib," : "",
		      evt->mask & PN_CREATE ? "create," : "",
		      evt->mask & PN_DELETE ? "delete," : "",
		      evt->mask & PN_MODIFY ? "modify," : "",
		      evt->mask & PN_ERROR ? "error," : "");
}


void
pnotify_dump(struct pnotify_ctx *ctx)
{
	struct event *evt;

	mutex_lock(ctx);

	printf("\npending events:\n");
	TAILQ_FOREACH(evt, &ctx->event, entries) {
		printf("\t");
		(void) pnotify_print_event(evt);
	}
	printf("/* end: pending events */\n");

	mutex_unlock(ctx);
}


void
pnotify_free(struct pnotify_ctx *ctx)
{
	struct event *evt, *nxt;

	assert(ctx != NULL);

	mutex_lock(ctx);

#if FIXME
	// need to scan the global watchlist
	
	/* Delete all watches */
	//struct pn_watch *watch;
	while (!LIST_EMPTY(&ctx->watch)) {
		watch = LIST_FIRST(&ctx->watch);
		if (pnotify_rm_watch(ctx, watch->wd) < 0) 
			errx(1,"error removing watch");
	}
#endif

	/* Delete all pending events */
  	evt = TAILQ_FIRST(&ctx->event);
        while (evt != NULL) {
		nxt = TAILQ_NEXT(evt, entries);
		free(evt);
		evt = nxt;
    	}

	/* Destroy the semaphore and mutex */
	if (sem_destroy(&ctx->event_count) != 0) 
		err(1, "sem_init(3) failed");
	if (pthread_mutex_destroy(&ctx->mutex) != 0) 
		err(1, "pthread_mutex_destroy(3) failed");

	/* Perform system-specific cleanup */
	sys->cleanup();

	mutex_unlock(ctx);
	free(ctx);
}


static struct watch *
_watch_add(enum pn_watch_type wtype, int fd, const char *path, int mask, void (*cb)(), void *arg)
{
	struct watch *w;

	/* Generate the watch */
	if ((w = calloc(1, sizeof(*w))) == NULL) 
		return NULL;
	w->type = wtype;
	w->mask = mask;
	w->cb = cb;
	w->arg = arg;
	if (wtype == WATCH_VNODE) {
		if ((w->ident.path = strdup(path)) == NULL) {
			free(w);
			return NULL;
		}
	} else {
		w->ident.fd = fd;
	}

	/* Add the watch */
	if (pnotify_add_watch(w) != 0) {
		if (wtype == WATCH_VNODE)
			free(w->ident.path);
		free(w);
		return NULL;
	}

	return (w);

}

struct watch *
watch_vnode(const char *path, int mask, void (*cb)(), void *arg)
{
	return _watch_add(WATCH_VNODE, 0, path, mask, cb, arg);
}


struct watch *
watch_fd(int fd, int mask, void (*cb)(), void *arg)
{
	return _watch_add(WATCH_FD, fd, NULL, mask, cb, arg);
}


struct watch *
watch_timer(int interval, int mask, void (*cb)(), void *arg)
{
	return _watch_add(WATCH_TIMER, interval, NULL, mask, cb, arg);
}

struct watch *
watch_signal(int signum, void (*cb)(), void *arg)
{
	return _watch_add(WATCH_SIGNAL, signum, NULL, PN_SIGNAL, cb, arg);
}

int
event_dispatch()
{
	struct event evt;

	for (;;) {
		/* Wait for an event */
		if (event_wait(&evt) != 0)
			return -1;

		/* Ignore events that have no callback defined */
		/* FIXME - this sounds like a bad idea... maybe crash instead */
		if (evt.watch->cb == NULL) {
			dprintf("ERROR: Cannot dispatch an event without a callback\n");
			continue;
		}
			
		if (evt.watch->type == WATCH_VNODE) {
			//FIXME: need to copy path to caller
			//*(evt->watch->cb)(evt->, 
			err(1, "XXX-FIXME");
		} else if (evt.watch->type == WATCH_TIMER) {
			 evt.watch->cb(evt.mask, evt.watch->arg);
		} else {
			 evt.watch->cb(evt.watch->ident.fd, evt.mask, evt.watch->arg);
		}
	}
	return 0;
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

	/* Assign the event to a context */
	mutex_lock(watch->ctx);
	TAILQ_INSERT_HEAD(&watch->ctx->event, evt, entries);
	mutex_unlock(watch->ctx);

	/* Increase the event counter, waking the thread */
	if (sem_post(&watch->ctx->event_count) != 0)
		err(1, "sem_post(3)");
}
