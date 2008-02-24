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

#ifndef _PNOTIFY_INTERNAL_H
#define _PNOTIFY_INTERNAL_H

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <dirent.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pnotify.h"
#include "queue.h"
#include "thread.h"
#include "buffer.h"

#define CTX_GET()      ((struct pnotify_ctx *) pthread_getspecific(CTX_KEY))
#define CTX_SET(ctx)   (pthread_setspecific(CTX_KEY, ctx))
extern pthread_key_t CTX_KEY;

/* Defined in signal.c */
extern struct watch *SIG_WATCH[NSIG + 1];

/** An event */
struct event {

	/** The watch that is interested in this event  */
	struct watch *watch;

	/** One or more bitflags containing the event(s) that occurred */
	int       mask;

	/** Pointers to the next and previous list elements */
	TAILQ_ENTRY(event) entries;
};

/** pnotify context */
struct pnotify_ctx {

	/** A list of events that are ready to be delivered */
	TAILQ_HEAD(, event) event;

	/** A mutex used to synchronize access between threads */
	pthread_mutex_t mutex;

	/** The number of unprocessed events.
	 *
	 * This sempaphore is incremented when an event is added
	 * to the `event' queue, and decremented when the event
	 * is delivered to the caller via pnotify_get_event().
	 * When the counter is zero, the caller will block
	 * inside of pnotify_get_event().
	 */
	sem_t event_count;
};

/* TODO - not used yet */
/** An entry within a pnotify_buffer chain */
struct pn_buffer {
	char data[4096];

	/** The position, in bytes, within the buffer.
	 *
	 * It is possible to do a partial read/write operation on a buffer.
	 * The position is stored so that the next operation can continue
	 * where the previous operation finished.
	 */
	size_t pos;
};

/* TODO - not used yet */
/** A linked list of buffers used to store the incoming and outgoing data 
 *  for a file descriptor.
 **/
struct pnotify_buffer {

	/** Input buffer */
	STAILQ_HEAD(, pn_buffer) in;

	/** Output buffer */
	STAILQ_HEAD(, pn_buffer) out;

	/** The position, in bytes, within the head of the `out' queue.
	 *
	 * When multiple lines are contained in a single buffer entry,
	 * the reader's position is stored in the in_pos variable.
	 *
	 * An entry is freed when all its data has been written to the 
	 * file descriptor.
	 */
	size_t out_pos;
};

/* The 'dprintf' macro is used for printf() debugging */
#if PNOTIFY_DEBUG
# define dprintf printf
# define dprint_event(e) (void) pnotify_print_event(e)
#else
# define dprintf(...)    do { ; } while (0)
# define dprint_event(e) do { ; } while (0)
#endif

/* Forward declarations for private functions */

void * pn_signal_loop(void *);
void * pn_timer_loop(void *);
void pn_event_add(struct watch *watch, int mask);
void pn_mask_signals();
void pn_timer_init(void);
int pn_add_timer(struct watch *watch);
int pn_rm_timer(struct watch *watch);

/* vtable for system-specific functions */
struct pnotify_vtable {
	void (*init_once)(void);
	int (*add_watch)(struct watch *);
	int (*rm_watch)(struct watch *);
	void (*cleanup)();
};
extern const struct pnotify_vtable * const sys;
extern const struct pnotify_vtable LINUX_VTABLE;
extern const struct pnotify_vtable BSD_VTABLE;

#endif /* _PNOTIFY_INTERNAL_H */
