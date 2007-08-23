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

/** @file
 *
 *  Public header for the pnotify library.
 *
*/

#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <sys/time.h>
#include <stdarg.h>

/* kqueue(4) in MacOS/X does not support NOTE_TRUNCATE */
#ifndef NOTE_TRUNCATE
# define NOTE_TRUNCATE 0
#endif

/* Opaque structures */
struct pnotify_event;
struct pnotify_ctx;


/** The type of resource to be watched */
enum pn_watch_type {
	/** A vnode in the filesystem */
	WATCH_VNODE = 0,

	/** An open file descriptor */
	WATCH_FD,

	/** A user-defined timer */
	WATCH_TIMER,

	/** Signals from the operating system */
	WATCH_SIGNAL,
	
	/** User-defined asynchronous function call */
	WATCH_FUNCTION
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

	/**
	 * A file or directory
	 *
	 * When changes are made to the file/directory,
	 * various events are generated.
	 */
	char *path;
};


/** The bitmask of events to monitor */
enum pn_event_bitmask {

	/** Use the default settings when creating a watch */
	PN_DEFAULT              = 0,

	/** The attributes of a file have been modified */
	PN_ATTRIB 		= 0x1 << 0,

	/** A file was created in a watched directory */
	PN_CREATE		= 0x1 << 1,

	/** A file was deleted from a watched directory */
	PN_DELETE		= 0x1 << 2,

	/** The contents of a file have changed */
	PN_MODIFY		= 0x1 << 3,

	/** Data is ready to be read from a file descriptor */
	PN_READ                 = 0x1 << 4,

	/** Data is ready to be written to a file descriptor */
	PN_WRITE                = 0x1 << 5,

	/** A socket or pipe descriptor was closed by the remote end */
	PN_CLOSE                = 0x1 << 6,

	/** A timer expired */
	PN_TIMEOUT              = 0x1 << 7,  

	/** A signal was received */
	PN_SIGNAL               = 0x1 << 8,  

	/** Delete the watch after a matching event occurs */
	PN_ONESHOT		= 0x1 << 30,

	/** An error condition in the underlying kernel event queue */
	PN_ERROR		= 0x1 << 31,

};


/**
 * A watch request.
 *
 * @see pn_watch, used internally to track each watch
 */
struct pnotify_watch {

	/** The type of resource to be watched */
	enum pn_watch_type type;

	/** Bitmask containing a union of all events to be monitored */
	enum pn_event_bitmask mask;

	/** The resource ID */
	union pn_resource_id ident;

	/** A callback to be invoked when a matching event occurs
	 *
	 * Parameters passed to the function are:
	 *     - the resource identifier from the watch structure
	 *     - the mask of events which occurred
	 *     - an opaque pointer to `arg' 
	 */
	void (*cb)();
	void *arg;

	/** The context that receives the event */
	struct pnotify_ctx *ctx;
};


/**
  Initialize a pnotify event queue.

  Before adding watches, the queue must be initialized via
  a call to pnotify_init(). 

  @return pointer to a new pnotify context, or NULL if an error occurred.
*/
struct pnotify_ctx * pnotify_init();

/**
  Add a watch.

  @param watch a watch structure
  @return a unique watch descriptor if successful, or -1 if an error occurred.
*/
int pnotify_add_watch(struct pnotify_watch *watch);

/**
  Remove a watch.

  @param wd watch descriptor
  @return 0 if successful, or non-zero if an error occurred.
*/
int pnotify_rm_watch(int wd);


/**
  Wait for an event to occur.

  @param evt an event structure that will store the result
  @param ctx a context returned by pnotify_init() or NULL for the current context
  @return 0 if successful, or non-zero if an error occurred.
*/
int pnotify_get_event(struct pnotify_event *, struct pnotify_ctx *);

/**
 * Wait for events and dispatch callbacks.
 *
 * @return -1 if an error occurs, otherwise the function does not return
*/
int pnotify_dispatch();

/**
 Print debugging information about an event to standard output.
 @param evt an event to be printed
*/
int pnotify_print_event(struct pnotify_event *);


/**
 Print the context to standard output.
*/
void pnotify_dump(struct pnotify_ctx *);

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
int pnotify_trap_signal(int signum, void (*cb)(), void *arg);

/** Watch for changes to a vnode 
 *
 * @param path the path to a file or directory to be monitored
 * @param mask a bitmask of events to monitor
 * @return a watch descriptor, or -1 if an error occurred
 */ 
int pnotify_watch_vnode(const char *path, int mask, void (*cb)(), void *arg);

/** Watch for changes to a file descriptor */
int pnotify_watch_fd(int fd, int mask, void (*cb)(), void *arg); 

/** Set a timer to fire after specific number of seconds 
 *
 * If the mask is set to PN_ONESHOT, the timer will be automatically
 * deleted after one occurrance. If the mask is PN_DEFAULT,
 * the timer will repeat forever.
 *
 * @param interval the number of seconds between timer events
 * @param mask either PN_DEFAULT or PN_ONESHOT
 */
int pnotify_set_timer(int interval, int mask, void (*cb)(), void *arg);

#if TODO
	// experimental
	
/** The maximum number of arguments that can be passed to an async function */
#define PNOTIFY_ARG_MAX 12

struct pnotify_func {

	/** The address of the function entry point */
	void (*symbol)();

	/** The number of arguments to pass to the function */
	size_t argc;

	/** An argument vector */
	void *argv[PNOTIFY_ARG_MAX + 1];

	/** The callback function to invoke when the original function completes */
	void (*cb)();
	/** FIXME - argument to the callback (this is not sufficient) */
	void *cbarg;	

	/* -- filled in by the function when it returns -- */

	/** The return value of the function */
	union {
		int rv_int;
		void *rv_ptr;
	} retval;

	/** The value of the global 'errno' immediately after the function call */
	int ret_errno;
};

#define pnotify_set_function(pf, sym, 
/** Invoke a function asynchronously and generate an event when it returns.
 *
 * @param mask either PN_DEFAULT or PN_ONESHOT
 */
int pnotify_call_function(struct );
#endif

#endif /* _PNOTIFY_H */
