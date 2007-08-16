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
 *  Linux-specific code.
 *
 */

#include "config.h"
#include "pnotify.h"
#include "pnotify-internal.h"


#if HAVE_INOTIFY

#include <sys/epoll.h>
#include <sys/inotify.h>

static int INOTIFY_FD = -1;
static int EPOLL_FD = -1;

int _get_inotify_event(struct pnotify_event *evt, struct pnotify_ctx *ctx);

void linux_dump_inotify_event(struct inotify_event *iev);


void *
linux_inotify_loop(void * unused)
{
	struct pn_watch *watch;
	struct pnotify_event *evt;
	struct inotify_event *iev, *endp;
	ssize_t         bytes;
	char            buf[4096];

	/* Avoid a compiler warning */
	watch = unused;

	/* Create an inotify descriptor */
	if ((INOTIFY_FD = inotify_init()) < 0)
		err(1, "inotify_init(2)");

	/* Loop forever waiting for events */
	for (;;) {

		/* 
		 * Wait for an event and read it into a buffer.
		 * This may block, so release the mutex.
		 */
		bytes = read(INOTIFY_FD, &buf, sizeof(buf));
		if (bytes <= 0) 
			err(1, "read(2)");

		/* Compute the beginning and end of the event list */
		iev = (struct inotify_event *) & buf;
		endp = iev + bytes;

		/* Process each pending event */
		while (iev < endp) {

			if (iev->wd == 0)
				break;

			/* We don't care about IN_IGNORED events */
			if (iev->mask & IN_IGNORED)
				goto next_event;
				
			/* Find the matching watch structure */
			/* FIXME: This may be a normal occurrance
			 * when a watch is removed when there are
			 * still events pending.. ?
			 */
			if ((watch = pn_get_watch_by_id(iev->wd)) == NULL) {
				warnx("watch # %d not found\n", iev->wd);	
				linux_dump_inotify_event(iev);
				continue;
			}

			/* Construct a pnotify_event structure */
			if ((evt = calloc(1, sizeof(*evt))) == NULL) 
				err(1, "malloc failed");

			evt->watch = watch;
			(void) strncpy(evt->name, iev->name, iev->len);
			if (iev->mask & IN_ATTRIB)
				evt->mask |= PN_ATTRIB;
			if (iev->mask & IN_MODIFY)
				evt->mask |= PN_MODIFY;
			if (iev->mask & IN_CREATE)
				evt->mask |= PN_CREATE;
			if (iev->mask & IN_DELETE)
				evt->mask |= PN_DELETE;
			if (iev->mask & IN_DELETE_SELF) {
				evt->mask |= PN_DELETE;
				(void) strncpy(evt->name, "", 0);
			}

			/* Add the event to the list of pending events */
			pn_event_add(watch->ctx, evt);

next_event:
			/* Go to the next event */
			iev += sizeof(*iev) + iev->len;
		}
	}

	close(INOTIFY_FD);
	return NULL;
}


void *
linux_epoll_loop(void * unused)
{
	static const int maxevents = 100;
	struct pnotify_event *evt;
	struct pn_watch *watch;
	struct epoll_event events[maxevents];
	int i, numevents;

	/* Avoid a compiler warning */
	watch = unused;

	/* Create an epoll descriptor */
	if ((EPOLL_FD = epoll_create(1000)) < 0)
		err(1, "epoll_create(2)");

	/* Loop forever waiting for events */
	for (;;) {

		/* Wait for an event */
		numevents = epoll_wait(EPOLL_FD, 
				(struct epoll_event *) &events, maxevents, -1);
		if (numevents < 0) {
			if (errno == EINTR)
				continue;
			err(1, "epoll_wait(2)");
		}

		/* Convert each epoll event into a pnotify event */
		for (i = 0; i < numevents; i++) {

			watch = (struct pn_watch *) events[i].data.ptr;	

#if DEAD
			/* Get the delivery context, or ignore the signal */
			pthread_mutex_lock(&SIGNAL_CTX_MUTEX);
			ctx = SIGNAL_CTX[signum];
			pthread_mutex_unlock(&SIGNAL_CTX_MUTEX);
			if (!ctx) {
				default_signal_handler(signum);
				continue;
			}
#endif

			/* Create a new event structure */
			if ((evt = calloc(1, sizeof(*evt))) == NULL)
				err(1, "calloc(3)");
			evt->watch = watch;
			evt->mask = 0;
			if (events[i].events & EPOLLIN)
				evt->mask |= PN_READ;
			if (events[i].events & EPOLLOUT)
				evt->mask |= PN_WRITE;
			if (events[i].events & EPOLLHUP)
				evt->mask |= PN_CLOSE;
			if (events[i].events & EPOLLERR)
				evt->mask |= PN_ERROR;

			/* Add the event to an event queue */
			pn_event_add(watch->ctx, evt);
		}
	}

	close(EPOLL_FD);
	return NULL;
}


void
linux_init_once(void)
{
	pthread_t tid;

        /* Create a dedicated epoll thread */
	if (pthread_create( &tid, NULL, linux_epoll_loop, NULL ) != 0)
		errx(1, "pthread_create(3) failed");

        /* Create a dedicated inotify thread */
	if (pthread_create( &tid, NULL, linux_inotify_loop, NULL ) != 0)
		errx(1, "pthread_create(3) failed");

	/* TODO: push cleanup function */
}


void
linux_cleanup(void)
{
	(void) close(INOTIFY_FD);
}


int
linux_add_watch(struct pn_watch *watch)
{
	struct epoll_event *ev = &watch->epoll_evt;
	int mask = watch->mask;
	uint32_t        imask = 0;

	switch (watch->type) {

		case WATCH_FD:
			/* Generate the epoll_event structure */
			ev->events = EPOLLET;
			if (mask & PN_READ)
				ev->events |= EPOLLIN;
			if (mask & PN_WRITE)
				ev->events |= EPOLLOUT;
			ev->data.ptr = watch;

			/* Add the epoll_event structure to the kernel queue */
			if (epoll_ctl(EPOLL_FD, EPOLL_CTL_ADD, watch->ident.fd, ev) < 0) {
				warn("epoll_ctl(2) failed");
				return -1;
			}
			break;

		case WATCH_VNODE:
			/* Generate the mask */
			if (mask & PN_ATTRIB)
				imask |= IN_ATTRIB;
			if (mask & PN_CREATE)
				imask |= IN_CREATE;
			if (mask & PN_DELETE)
				imask |= IN_DELETE | IN_DELETE_SELF;
			if (mask & PN_MODIFY)
				imask |= IN_MODIFY;
			if (mask & PN_ONESHOT)
				imask |= IN_ONESHOT;

			/* Add the event to the kernel event queue */
			/* XXX-FIXME this overwrites watch->wd as assigned earlier! */
			watch->wd = inotify_add_watch(INOTIFY_FD, watch->ident.path, imask);
			if (watch->wd < 0) {
				perror("inotify_add_watch(2) failed");
				return -1;
			}
			break;

		default:
			/* The default action is to do nothing. */
			break;
	}

	return 0;
}

int
linux_rm_watch(struct pn_watch *watch)
{
	if (inotify_rm_watch(INOTIFY_FD, watch->wd) < 0) {
		perror("inotify_rm_watch(2)");
		return -1;
	}

	return 0;
}

int
_get_inotify_event(struct pnotify_event *evt, struct pnotify_ctx *ctx)
{


	return 0;
}

int
linux_trap_signal(struct pnotify_ctx *ctx, int signum)
{
	/* Linux does not have a kernel mechanism for
	 * converting signals to events but if it did,
	 * this would be the place to activate it.
	 */
	assert(ctx && signum);
	return 0;
}

void
linux_dump_inotify_event(struct inotify_event *iev)
{
	static const char *nam[] = {
		"IN_ACCESS", "IN_MODIFY", "IN_ATTRIB", "IN_CLOSE_WRITE",
		"IN_CLOSE_NOWRITE", "IN_OPEN", "IN_MOVED_FROM",
		"IN_MOVED_TO", "IN_CREATE", "IN_DELETE", "IN_DELETE_SELF",
		"IN_MOVE_SELF", "IN_UNMOUNT", "IN_Q_OVERFLOW", "IN_IGNORED",
		"IN_ONLYDIR", "IN_DONT_FOLLOW", "IN_MASK_ADD", "IN_ISDIR",
		"IN_ONESHOT", NULL };
	static const int val[] = {
		IN_ACCESS, IN_MODIFY, IN_ATTRIB, IN_CLOSE_WRITE,
		IN_CLOSE_NOWRITE, IN_OPEN, IN_MOVED_FROM,
		IN_MOVED_TO, IN_CREATE, IN_DELETE, IN_DELETE_SELF,
		IN_MOVE_SELF, IN_UNMOUNT, IN_Q_OVERFLOW, IN_IGNORED,
		IN_ONLYDIR, IN_DONT_FOLLOW, IN_MASK_ADD, IN_ISDIR,
		IN_ONESHOT, 0 };
	int i;

	fprintf(stderr, "inotify event: wd=%d mask=", iev->wd);
	for (i = 0; val[i] != 0; i++) {
		if (iev->mask & val[i])
			fprintf(stderr, "%s ", nam[i]);
	}
	fprintf(stderr, "\n");
}

const struct pnotify_vtable LINUX_VTABLE = {
	.init_once = linux_init_once,
	.add_watch = linux_add_watch,
	.rm_watch = linux_rm_watch,
	.cleanup = linux_cleanup,
};

#endif

