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
static pthread_key_t CTX_KEY;

#define CTX_GET()	((struct pnotify_ctx *) pthread_getspecific(CTX_KEY))
#define CTX_SET(ctx)	(pthread_setspecific(CTX_KEY, ctx))


/* Define the system-specific vtable.  */
#if HAVE_KQUEUE
const struct pnotify_vtable * const sys = &BSD_VTABLE;
#else
const struct pnotify_vtable * const sys = &LINUX_VTABLE;
#endif

/** A list of watched files and/or directories */
LIST_HEAD(pnwatchhead, pn_watch) WATCH;
pthread_mutex_t WATCH_MUTEX = PTHREAD_MUTEX_INITIALIZER;


struct pn_watch *
pn_get_watch_by_id(int wd)
{
	struct pn_watch *watch;
	
	/* Find the matching watch structure */
	pthread_mutex_lock(&WATCH_MUTEX);
	LIST_FOREACH(watch, &WATCH, entries) {
		if (watch->wd == wd) {
			pthread_mutex_unlock(&WATCH_MUTEX);
			return watch;
		}
	}
	pthread_mutex_unlock(&WATCH_MUTEX);

	/* If no matching watch is found, return NULL */
	//warn("get_watch_by_id(): watch # %d not found", wd);
	return NULL;
}


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
	if (pthread_setspecific(CTX_KEY, ctx) != 0) {
		warn("pthread_setspecific(3) failed");
		return NULL;
	}

	/* Push the cleanup routine on the stack */
	//FIXME: macro error: pthread_cleanup_push(pnotify_free, ctx);

	return ctx;
}


int
pnotify_add_watch(struct pnotify_watch *watch)
{
	static int next_wd = 100;
	static pthread_mutex_t next_wd_mutex = PTHREAD_MUTEX_INITIALIZER;
	struct pn_watch *_watch;
	size_t len;

	/* Get the context */
	if (!watch->ctx)
		watch->ctx = CTX_GET();

	/* Allocate a new entry */
	if ((_watch = malloc(sizeof(*_watch))) == NULL) {
		warn("malloc error");
		return -1;
	}

	/* Generate a new watch descriptor */
	if (watch->type == WATCH_SIGNAL) {
		/* Special case: a signal's wd is the same as its signal number */
		_watch->wd = watch->ident.signum;
	} else {
		pthread_mutex_lock(&next_wd_mutex);
		_watch->wd = next_wd++;
		pthread_mutex_unlock(&next_wd_mutex);
		if (_watch->wd < 0) {
			warnx("watch descriptor overflow");
			free(_watch);
			return -1;
		}
	}

	/* Copy the resource identifier */
	switch (watch->type) {

		case WATCH_VNODE:
			len = strlen(watch->ident.path);
			if (len > PATH_MAX)
				return -1;
			_watch->ident.path = malloc(len + 1);
			/* TODO: malloc error handling */
			(void) strncpy(_watch->ident.path, watch->ident.path, len);
			break;

		case WATCH_SIGNAL:
		case WATCH_TIMER:
		case WATCH_FD:
			_watch->ident = watch->ident;
			break;

		default:
			warn("invalid watch type = %d", watch->type);
			return -1;
	}

	/* Copy the other watch fields */
	_watch->type = watch->type;
	_watch->mask = watch->mask;
	_watch->cb = watch->cb;
	_watch->arg = watch->arg;
	_watch->ctx = watch->ctx;

	/* Register the watch with the kernel */
	if (sys->add_watch(_watch) < 0) {
		warn("adding watch failed");
		//TODO: free(watch->ident.path);
		free(_watch);
		return -1;
	}

	/* Add the watch to the watchlist */
	pthread_mutex_lock(&WATCH_MUTEX);
	LIST_INSERT_HEAD(&WATCH, _watch, entries);
	pthread_mutex_unlock(&WATCH_MUTEX);

	dprintf("added watch: wd=%d mask=%d path=%s\n", 
		watch->wd, watch->mask, watch->path);

	return _watch->wd;
}


int 
pnotify_rm_watch(int wd)
{
	struct pn_watch *watchp, *wtmp;
	int found = 0;

	pthread_mutex_lock(&WATCH_MUTEX);

	/* Find the matching watch structure(s) */
	LIST_FOREACH_SAFE(watchp, &WATCH, entries, wtmp) {

		/* Remove the parent watch and it's children */
		if ((watchp->wd == wd) || (watchp->parent_wd == wd)) {
			if (sys->rm_watch(watchp) < 0)
				break;
			switch (watchp->type) {
				case WATCH_TIMER: 
					(void) pn_rm_timer(watchp);
					break;
				default: 
					break;
			}
			LIST_REMOVE(watchp, entries);
			free(watchp);
			found++;
		}
	}

	pthread_mutex_unlock(&WATCH_MUTEX);

	if (found == 0) {
		warn("watch # %d not found", wd);
		return -1;
	} else {
		return 0;
	}
}


int
pnotify_get_event(struct pnotify_event * evt, struct pnotify_ctx * ctx)
{
	struct pnotify_event *evp;

	assert(evt);

	/* Get the context */
	if (!ctx)
		ctx = CTX_GET();

	/* Wait for an event to be added to the queue */
	if (sem_wait(&ctx->event_count) != 0) {
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
pnotify_print_event(struct pnotify_event * evt)
{
	assert(evt);

	return printf("event: wd=%d mask=(%s%s%s%s%s) name=`%s'\n",
		      evt->watch->wd,
		      evt->mask & PN_ATTRIB ? "attrib," : "",
		      evt->mask & PN_CREATE ? "create," : "",
		      evt->mask & PN_DELETE ? "delete," : "",
		      evt->mask & PN_MODIFY ? "modify," : "",
		      evt->mask & PN_ERROR ? "error," : "",
		      evt->name);
}


void
pnotify_dump(struct pnotify_ctx *ctx)
{
	struct pnotify_event *evt;

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
	struct pnotify_event *evt, *nxt;

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


/* -------- Convenience functions for pnotify_add_watch() ----------------- */

int
pnotify_watch_vnode(const char *path, int mask, void (*cb)(), void *arg)
{
	struct pnotify_watch w;

	w.type = WATCH_VNODE;
	w.ident.path = (char *) path;
	w.mask = mask;
	w.cb = cb;
	w.arg = arg;

	return pnotify_add_watch(&w);
}


int
pnotify_watch_fd(int fd, int mask, void (*cb)(), void *arg)
{
	struct pnotify_watch w;

	w.type = WATCH_FD;
	w.ident.fd = fd;
	w.mask = mask;
	w.cb = cb;
	w.arg = arg;

	return pnotify_add_watch(&w);
}

int
pnotify_set_timer(int interval, int mask, void (*cb)(), void *arg)
{
	struct pnotify_watch w;
	int wd;

	/* Add the watch */
	w.type = WATCH_TIMER;
	w.ident.interval = interval;
	w.mask = mask;
	w.cb = cb;
	w.arg = arg;
	if ((wd = pnotify_add_watch(&w)) < 0) {
		warnx("unable to add watch for timer");
		return -1;
	}

	/* Set a timer */
	if (pn_add_timer(pn_get_watch_by_id(wd))) {
		warnx("unable to add timer");
		return -1;
	}

	return 0;
}

int
pnotify_trap_signal(int signum, void (*cb)(), void *arg)
{
	struct pnotify_watch w;

	/* Register the signal with the signal handling thread */
	if (pn_trap_signal(CTX_GET(), signum) != 0)
		return -1;

	w.type = WATCH_SIGNAL;
	w.ident.signum = signum;
	w.mask = PN_SIGNAL;
	w.cb = cb;
	w.arg = arg;

	return pnotify_add_watch(&w);
}

int pnotify_call_function(int (*func)(), size_t nargs, ...)
{
	abort(); 

	/* FIXME - TODO */
	
}

int
pnotify_dispatch()
{
	struct pnotify_event evt;
	//void (*fn)(union pn_resource_id, int, void *);

	for (;;) {
		/* Wait for an event */
		if (pnotify_get_event(&evt, NULL) != 0)
			return -1;

		/* Ignore events that have no callback defined */
		if (!evt.watch->cb) 
			continue;
			
		if (evt.watch->type == WATCH_VNODE) {
			//FIXME: need to copy path to caller
			//*(evt->watch->cb)(evt->, 
		} else if (evt.watch->type == WATCH_TIMER) {
			 evt.watch->cb(evt.mask, evt.watch->arg);
		} else {
			 evt.watch->cb(evt.watch->ident.fd, evt.mask, evt.watch->arg);
		}

	}
	return 0;
}


void
pn_event_add(struct pnotify_ctx *ctx, struct pnotify_event *evt)
{
	/* Assign the event to a context */
	mutex_lock(ctx);
	TAILQ_INSERT_HEAD(&ctx->event, evt, entries);
	mutex_unlock(ctx);

	/* Increase the event counter, waking the thread */
	if (sem_post(&ctx->event_count) != 0)
		err(1, "sem_post(3)");
}
