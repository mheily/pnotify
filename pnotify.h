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


/** The bitmask of events to monitor */
enum pn_event_bitmask {
	PN_READ,	/** Data is ready to be read from a file descriptor */
	PN_WRITE, 	/** Data is ready to be written to a file descriptor */
	PN_CLOSE,	/** A socket or pipe descriptor was closed by the remote end */
	PN_TIMEOUT,	/** A timer expired */
	PN_ERROR	/** An error condition in the underlying kernel event queue */
};

/**
 * A watch request.
 */
struct watch {

	/** The type of resource to be watched */
	enum pn_watch_type type;

	/** The resource ID */
	int ident;

	/** A callback to be invoked when a matching event occurs */
	void (*cb)();
	void *arg;

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



/**
  Initialize a pnotify event queue.

  Before adding watches, the queue must be initialized via
  a call to pnotify_init(). 
*/
void pnotify_init(void);


/**
  Remove a watch.

  @param watch watch 
  @return 0 if successful, or non-zero if an error occurred.
*/
int watch_cancel(struct watch *watch);


/* Undocumented API function - Used for unit testing */
struct event * event_wait(void);


/**
 * Wait for events and dispatch callbacks.
 *
 * @return -1 if an error occurs, otherwise the function does not return
*/
void event_dispatch(void);


/** 
 * Trap a specific signal and generate an event when it is received.
 *
 * When a signal is trapped, it is no longer delivered to the program
 * and is converted into an event instead.
 *
 * @param signum the signal to be trapped
 * @return a watch descriptor, or -1 if an error occurred
 */ 
struct watch * watch_signal(int signum, void (*cb)(int, void *), void *arg);

/** Watch for changes to a file descriptor */
struct watch * watch_fd(int fd, void (*cb)(int, int, void *), void *arg); 

/** Set a timer to fire after specific number of seconds 
 *
 * @param interval the minimum number of seconds that are to elapse
 */
struct watch * watch_timer(int interval, void (*cb)(void *), void *arg);

#endif /* _PNOTIFY_H */
