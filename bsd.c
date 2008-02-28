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
 *  BSD-specific code.
 *
 */

#include "config.h"
#include "pnotify.h"
#include "pnotify-internal.h"


#if defined(BSD)

#include <fcntl.h>
#include <sys/event.h>

/* Forward declarations */
void bsd_dump_kevent(struct kevent *kev);

/** The file descriptor returned by kqueue(2) */
static int KQUEUE_FD;

static void
bsd_handle_fd_event(struct watch *watch, struct kevent *kev)
{
	int mask = 0;

	/* Set the mask */
	if (kev->filter == EVFILT_READ)
		mask |= PN_READ;
	if (kev->filter == EVFILT_WRITE)
		mask |= PN_WRITE;
	if (kev->flags & EV_EOF)
		mask |= PN_CLOSE;
	if (mask == 0)
		errx(1, "invalid event mask");

	/* Add the event to the list of pending events */
	pn_event_add(watch, mask);
}


void *
bsd_kqueue_loop()
{
	struct watch *watch;
	struct kevent kev;
	int rc;

	/* Loop forever waiting for events */
	for (;;) {

		/* Wait for an event */
		dprintf("waiting for kernel event..\n");
		rc = kevent(KQUEUE_FD, NULL, 0, &kev, 1, NULL);
		if (rc < 0) 
			err(1, "kqueue_loop: kevent(2) failed");

		bsd_dump_kevent(&kev);

		/* Find the matching watch structure */
		watch = (struct watch *) kev.udata;

		/* Handle the event */
		switch (watch->type) {
			case WATCH_FD:
				bsd_handle_fd_event(watch, &kev);
				break;
			default:
				errx(1, "invalid watch type %d", watch->type);
		}
	}

	close(KQUEUE_FD);
	return NULL;
}


void
bsd_init_once(void)
{
	pthread_t tid;

	/* Create a kqueue descriptor */
	if ((KQUEUE_FD = kqueue()) < 0)
		err(1, "kqueue(2)");

        /* Create a dedicated kqueue thread */
	if (pthread_create( &tid, NULL, bsd_kqueue_loop, NULL ) != 0)
		errx(1, "pthread_create(3) failed");

	/* TODO: push cleanup function */
}


void
bsd_cleanup(void)
{
	(void) close(KQUEUE_FD);
}


void
bsd_dump_kevent(struct kevent *kev)
{
	static const char *nam[] = {
		"EV_ADD", "EV_ENABLE", "EV_DISABLE", "EV_DELETE", "EV_ONESHOT",
		"EV_CLEAR", "EV_EOF", "EV_ERROR",
		NULL };
	static const char *evfilt_nam[] = {
		"EVFILT_READ", "EVFILT_WRITE", "EVFILT_AIO", "EVFILT_VNODE",
		"EVFILT_PROC", "EVFILT_SIGNAL",
		NULL };
	static const int evfilt_val[] = {
		EVFILT_READ, EVFILT_WRITE, EVFILT_AIO, EVFILT_VNODE,
		EVFILT_PROC, EVFILT_SIGNAL, 
		0 };
	static const int val[] = {
		EV_ADD, EV_ENABLE, EV_DISABLE, EV_DELETE, EV_ONESHOT,
		EV_CLEAR, EV_EOF, EV_ERROR,
		0 };
	int i;

	fprintf(stderr, "kevent: ident=%d filter=", (int) kev->ident);
	for (i = 0; evfilt_val[i] != 0; i++) {
		if (kev->filter == evfilt_val[i]) {
			fprintf(stderr, "%s ", evfilt_nam[i]);
			break;
			}
	}
	fprintf(stderr, "flags=");
	for (i = 0; val[i] != 0; i++) {
		if (kev->flags & val[i])
			fprintf(stderr, "%s ", nam[i]);
	}
	fprintf(stderr, "udata=%p", kev->udata);
	fprintf(stderr, "\n");
}


int
bsd_add_watch(struct watch *watch)
{
	struct kevent *kev = &watch->kev;

	/* Create and populate a kevent structure */
	if (watch->type == WATCH_FD) {
			EV_SET(kev, watch->ident.fd, 
					EVFILT_READ | EVFILT_WRITE, 
					EV_ONESHOT | EV_ADD | EV_CLEAR, 0, 0, watch);
	} else {
			return 0;
	}

	/* Add the kevent to the kernel event queue */
	if (kevent(KQUEUE_FD, kev, 1, NULL, 0, NULL) < 0) {
		perror("kevent(2)");
		return -1;
	}

	return 0;
}


int
bsd_rm_watch(struct watch *watch)
{
	/* Close the file descriptor.
	  The kernel will automatically delete the kevent 
	  and any pending events.
	 */
	if (close(watch->wfd) < 0) {
		perror("unable to close watch fd");
		return -1;
	}

	return 0;
}


const struct pnotify_vtable BSD_VTABLE = {
	.init_once = bsd_init_once,
	.add_watch = bsd_add_watch,
	.rm_watch = bsd_rm_watch,
	.cleanup = bsd_cleanup,
};

#endif
