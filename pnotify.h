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

#ifndef _PNOTIFY_H
#define _PNOTIFY_H

/* The BSD macro is defined in sys/param.h */
#if (defined(__unix__) || defined(unix)) && !defined(USG)
#include <sys/param.h>
#endif

#include <sys/types.h>

/* System-specific headers */
#if defined(__linux__)
# include <sys/epoll.h>
#elif defined(BSD) 
# include <sys/event.h>
#else
# error "This library has not been ported to your operating system"
#endif

/** @file
 *
 *  Public header for the pnotify library.
 *
*/

/* Opaque structures */
struct pnotify_ctx;

/** The type of resource to be watched */
enum pn_watch_type {
	/** An open file descriptor */
	WATCH_FD,

	/** A user-defined timer */
	WATCH_TIMER,

	/** Signals from the operating system */
	WATCH_SIGNAL,
};


/** A unique resource identifier */
union pn_resource_id {
	/** An open file descriptor
	 *
	 * Can receive PN_READ | PN_WRITE | PN_CLOSE events
	 */ 
	int fd;

	/**
	 * A signal number
	 *
	 * When received, creates a PN_SIGNAL event.
	 */
	int signum;

	/**
	 * A timer interval (in seconds)
	 *
	 * When the interval elapses, a PN_TIMER event is created.
	 */
	int interval;
};


/** The bitmask of events to monitor */
enum pn_event_bitmask {

	/** Use the default settings when creating a watch */
	PN_DEFAULT              = 0,

	/** Data is ready to be read from a file descriptor */
	PN_READ                 = 0x1 << 1,

	/** Data is ready to be written to a file descriptor */
	PN_WRITE                = 0x1 << 2,

	/** A socket or pipe descriptor was closed by the remote end */
	PN_CLOSE                = 0x1 << 3,

	/** A timer expired */
	PN_TIMEOUT              = 0x1 << 4,  

	/** A signal was received */
	PN_SIGNAL               = 0x1 << 5,  

	/** Delete the watch after a matching event occurs */
	PN_ONESHOT		= 0x1 << 30,

	/** An error condition in the underlying kernel event queue */
	PN_ERROR		= 0x1 << 31,

};

/**
 * A watch request.
 */
struct watch {

	/** The type of resource to be watched */
	enum pn_watch_type type;

	/** Bitmask containing a union of all events to be monitored */
	enum pn_event_bitmask mask;

	/** The resource ID */
	union pn_resource_id ident;

	/** A callback to be invoked when a matching event occurs */
	void (*cb)();
	void *arg;

	/** The context that receives the event */
	struct pnotify_ctx *ctx;

#if defined(BSD)

	/* The associated kernel event structure */
	struct kevent    kev;

	/* Each watched file must be opened first, this is the fd that is used */
	int wfd;

#elif defined(__linux__)

	/* The associated kernel event structure */
	struct epoll_event epoll_evt;

#endif

	/* (LIST_ENTRY) Pointer to the next/previous watches */
	struct {
		struct watch *le_next;   /* next element */
		struct watch **le_prev;  /* address of previous next element */
	} entries;
};

/** An event */
struct event {

	/** The watch that is interested in this event  */
	struct watch *watch;

	/** One or more bitflags containing the event(s) that occurred */
	int       mask;

	/** (STAILQ_ENTRY) Pointers to the next list element */
	struct { 
		struct event *stqe_next; /* next element */
	} entries;
};



/**
  Initialize a pnotify event queue.

  Before adding watches, the queue must be initialized via
  a call to pnotify_init(). 

  @return pointer to a new pnotify context, or NULL if an error occurred.
*/
struct pnotify_ctx * pnotify_init();


/**
  Remove a watch.

  @param watch watch 
  @return 0 if successful, or non-zero if an error occurred.
*/
int watch_cancel(struct watch *watch);


/**
  Wait for an event to occur.

  @param evt an event structure that will store the result
  @return 0 if successful, or non-zero if an error occurred.
*/
int event_wait(struct event *);

/**
 * Wait for events and dispatch callbacks.
 *
 * @return -1 if an error occurs, otherwise the function does not return
*/
int event_dispatch();

/**
  Free all resources associated with an event queue.

  All internal data structures will be freed. 

  @param ctx a context returned by pnotify_init() or NULL for the current context
  @return 0 if successful, or non-zero if an error occurred.
*/
void pnotify_free(struct pnotify_ctx *ctx);

/** Trap a specific signal and generate an event when it is received.
 *
 * When a signal is trapped, it is no longer delivered to the program
 * and is converted into an event instead.
 *
 * @param signum the signal to be trapped
 * @return a watch descriptor, or -1 if an error occurred
 */ 
struct watch * watch_signal(int signum, void (*cb)(), void *arg);

/** Watch for changes to a file descriptor */
struct watch * watch_fd(int fd, int mask, void (*cb)(), void *arg); 

/** Set a timer to fire after specific number of seconds 
 *
 * If the mask is set to PN_ONESHOT, the timer will be automatically
 * deleted after one occurrance. If the mask is PN_DEFAULT,
 * the timer will repeat forever.
 *
 * @param interval the number of seconds between timer events
 * @param mask either PN_DEFAULT or PN_ONESHOT
 */
struct watch * watch_timer(int interval, int mask, void (*cb)(), void *arg);

#endif /* _PNOTIFY_H */
