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

#include "config.h"
#include "pnotify.h"
#include "pnotify-internal.h"

/** @file
 *
 *  Linux-specific functions.
 *
*/

#if defined(__linux__)

#include <sys/epoll.h>

static int EPOLL_FD = -1;

void *
linux_epoll_loop(void * unused)
{
	static const int maxevents = 100;
	struct watch *watch;
	struct epoll_event events[maxevents];
	int i, mask, numevents;

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

			watch = (struct watch *) events[i].data.ptr;	
			mask = 0;
			if (events[i].events & EPOLLIN)
				mask |= PN_READ;
			if (events[i].events & EPOLLOUT)
				mask |= PN_WRITE;
			if (events[i].events & EPOLLHUP)
				mask |= PN_CLOSE;
			if (events[i].events & EPOLLERR)
				mask |= PN_ERROR;

			/* Add the event to an event queue */
			pn_event_add(watch, mask);
		}
	}

	close(EPOLL_FD);
	return NULL;
}


void
linux_init_once(void)
{
	pthread_t tid;

	/* Create an epoll descriptor */
	if ((EPOLL_FD = epoll_create(1000)) < 0)
		err(1, "epoll_create(2)");

        /* Create a dedicated epoll thread */
	if (pthread_create( &tid, NULL, linux_epoll_loop, NULL ) != 0)
		errx(1, "pthread_create(3) failed");

	/* TODO: push cleanup function */
}


void
linux_cleanup(void)
{
}


int
linux_add_watch(struct watch *watch)
{
	struct epoll_event *ev = &watch->epoll_evt;

	switch (watch->type) {

		case WATCH_FD:
			/* Generate the epoll_event structure */
			ev->events = EPOLLET | EPOLLIN | EPOLLOUT;
			ev->data.ptr = watch;

			/* Add the epoll_event structure to the kernel queue */
			if (epoll_ctl(EPOLL_FD, EPOLL_CTL_ADD, watch->ident.fd, ev) < 0) {
				warn("epoll_ctl(2) failed");
				return -1;
				}
			dprintf("added epoll watch for fd #%d", watch->ident.fd);
			break;

		default:
			/* The default action is to do nothing. */
			break;
	}

	return 0;
}

int
linux_rm_watch(struct watch *watch)
{
	/* XXX-FIXME remove from epoll set */
	return 0;
}


const struct pnotify_vtable LINUX_VTABLE = {
	.init_once = linux_init_once,
	.add_watch = linux_add_watch,
	.rm_watch = linux_rm_watch,
	.cleanup = linux_cleanup,
};

#endif

