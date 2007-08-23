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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pnotify.h"
#include "queue.h"
#include "thread.h"

/* WORKAROUND */
#if defined(__linux__)
# define HAVE_INOTIFY 1
# include <sys/inotify.h>
# include <sys/epoll.h>
#else
# define HAVE_KQUEUE 1
# include <sys/event.h>
#endif

/** An event */
struct pnotify_event {

	/** The watch that is interested in this event  */
	struct pn_watch *watch;

	/** The parent watch descriptor, when monitoring files
 	    within a directory using kqueue(4). If no parent, this is zero.
	*/
	int 	  parent;

	/** One or more bitflags containing the event(s) that occurred */
	int       mask;

	/** The filename associated with a directory entry creation/deletion.
		Only used when monitoring directories.
	*/
        char      name[NAME_MAX + 1];

	TAILQ_ENTRY(pnotify_event) entries;
};

/** pnotify context */
struct pnotify_ctx {

	/** A list of events that are ready to be delivered */
	TAILQ_HEAD(, pnotify_event) event;

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

/** An entire directory that is under watch */
struct directory {

	/** The full pathname of the directory */
	char    *path;
	size_t   path_len;

	/** A directory handle returned by opendir(3) */
	DIR     *dirp;

	/* All entries in the directory (struct dirent) */
	LIST_HEAD(, dentry) all;
};

/** A directory entry list element */
struct dentry {

	/** The directory entry from readdir(3) */
	struct dirent   ent;

	/** A file descriptor used by kqueue(4) to monitor the entry */
	int  fd;

	/** The file status from stat(2) */
	struct stat     st;

	/** All event(s) that occurred on this file */
	int mask;

	/** Pointer to the next directory entry */
        LIST_ENTRY(dentry) entries;
};

/** An internal watch entry */
struct pn_watch {

	/* ---- public fields accessible via pnotify_watch structure */

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

	/* ---- private fields accessible via pn_watch structure */

	/** The context that owns the watch */
	struct pnotify_ctx * ctx;

	int             fd;	/**< The watched file descriptor */
	int             wd;	/**< Unique 'watch descriptor' */
	bool            is_dir;	/**< TRUE, if the file is a directory */

	/* A list of directory entries (only used if is_dir is true) */
	struct directory dir;

	/* The parent watch descriptor, for watches that are
	   automatically added on files within a monitored
	   directory. If zero, there is no parent.
	 */
	int parent_wd;

#if HAVE_KQUEUE

	/* The associated kernel event structure */
	struct kevent    kev;

	/* The pathname of the directory (only for directories) */
	//DEADWOOD: use ident.path instead of: char path[PATH_MAX + 1];

#elif __linux__

	struct epoll_event epoll_evt;

#endif

	/* Pointer to the next watch */
	LIST_ENTRY(pn_watch) entries;
};


/* The 'dprintf' macro is used for printf() debugging */
#if PNOTIFY_DEBUG
# define dprintf printf
# define dprint_event(e) (void) pnotify_print_event(e)
#else
# define dprintf(...)    do { ; } while (0)
# define dprint_event(e) do { ; } while (0)
#endif

/** KLUDGE: The maximum number of watches that can exist at a single time */
#define WATCH_MAX 1000

/* Forward declarations for private functions */

int pnotify_reap_signal(struct pnotify_event *, struct pnotify_ctx *);
int sys_trap_signal(struct pnotify_ctx *, int signum);
int pn_trap_signal(struct pnotify_ctx *, int signum);
void * pn_signal_loop(void *);
void * pn_timer_loop(void *);
void pn_event_add(struct pnotify_ctx *, struct pnotify_event *);
struct pn_watch * pn_get_watch_by_id(int wd);
void pn_mask_signals();
void pn_timer_init(void);
int pn_add_timer(struct pn_watch *watch);
int pn_rm_timer(struct pn_watch *watch);

/* vtable for system-specific functions */
struct pnotify_vtable {
	void (*init_once)(void);
	int (*add_watch)(struct pn_watch *);
	int (*rm_watch)(struct pn_watch *);
	void (*cleanup)();
};
extern const struct pnotify_vtable * const sys;
extern const struct pnotify_vtable LINUX_VTABLE;
extern const struct pnotify_vtable BSD_VTABLE;

#endif /* _PNOTIFY_INTERNAL_H */
