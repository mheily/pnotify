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

#include <dirent.h>
#include <stdint.h>
#include <limits.h>
#include <sys/queue.h>

/* kqueue(4) in MacOS/X does not support NOTE_TRUNCATE */
#ifndef NOTE_TRUNCATE
# define NOTE_TRUNCATE 0
#endif


/* 
 Some implementations of <sys/queue.h> do not include
 the STAILQ_* macros, so they are defined here as needed.
 These were taken from FreeBSD 6.2's queue.h.
*/

#ifndef STAILQ_HEAD
#define	STAILQ_HEAD(name, type)						\
struct name {								\
	struct type *stqh_first;/* first element */			\
	struct type **stqh_last;/* addr of last next element */		\
}
#endif

#ifndef STAILQ_HEAD_INITIALIZER
#define	STAILQ_HEAD_INITIALIZER(head)					\
	{ NULL, &(head).stqh_first }
#endif

#ifndef STAILQ_ENTRY
#define	STAILQ_ENTRY(type)						\
struct {								\
	struct type *stqe_next;	/* next element */			\
}
#endif

#ifndef STAILQ_EMPTY
#define	STAILQ_EMPTY(head)	((head)->stqh_first == NULL)
#endif

#ifndef STAILQ_FIRST
#define	STAILQ_FIRST(head)	((head)->stqh_first)
#endif

#ifndef STAILQ_FOREACH
#define	STAILQ_FOREACH(var, head, field)				\
	for((var) = STAILQ_FIRST((head));				\
	   (var);							\
	   (var) = STAILQ_NEXT((var), field))
#endif

#ifndef STAILQ_INIT
#define	STAILQ_INIT(head) do {						\
	STAILQ_FIRST((head)) = NULL;					\
	(head)->stqh_last = &STAILQ_FIRST((head));			\
} while (0)
#endif

#ifndef	STAILQ_INSERT_AFTER
#define	STAILQ_INSERT_AFTER(head, tqelm, elm, field) do {		\
	if ((STAILQ_NEXT((elm), field) = STAILQ_NEXT((tqelm), field)) == NULL)\
		(head)->stqh_last = &STAILQ_NEXT((elm), field);		\
	STAILQ_NEXT((tqelm), field) = (elm);				\
} while (0)
#endif

#ifndef	STAILQ_INSERT_HEAD
#define	STAILQ_INSERT_HEAD(head, elm, field) do {			\
	if ((STAILQ_NEXT((elm), field) = STAILQ_FIRST((head))) == NULL)	\
		(head)->stqh_last = &STAILQ_NEXT((elm), field);		\
	STAILQ_FIRST((head)) = (elm);					\
} while (0)
#endif

#ifndef	STAILQ_INSERT_TAIL
#define	STAILQ_INSERT_TAIL(head, elm, field) do {			\
	STAILQ_NEXT((elm), field) = NULL;				\
	*(head)->stqh_last = (elm);					\
	(head)->stqh_last = &STAILQ_NEXT((elm), field);			\
} while (0)
#endif

#ifndef STAILQ_LAST
#define	STAILQ_LAST(head, type, field)					\
	(STAILQ_EMPTY((head)) ?						\
		NULL :							\
	        ((struct type *)(void *)				\
		((char *)((head)->stqh_last) - __offsetof(struct type, field))))
#endif

#ifndef STAILQ_NEXT
#define	STAILQ_NEXT(elm, field)	((elm)->field.stqe_next)
#endif

#ifndef STAILQ_REMOVE
#define	STAILQ_REMOVE(head, elm, type, field) do {			\
	if (STAILQ_FIRST((head)) == (elm)) {				\
		STAILQ_REMOVE_HEAD((head), field);			\
	}								\
	else {								\
		struct type *curelm = STAILQ_FIRST((head));		\
		while (STAILQ_NEXT(curelm, field) != (elm))		\
			curelm = STAILQ_NEXT(curelm, field);		\
		if ((STAILQ_NEXT(curelm, field) =			\
		     STAILQ_NEXT(STAILQ_NEXT(curelm, field), field)) == NULL)\
			(head)->stqh_last = &STAILQ_NEXT((curelm), field);\
	}								\
} while (0)
#endif

#ifndef STAILQ_REMOVE_HEAD
#define	STAILQ_REMOVE_HEAD(head, field) do {				\
	if ((STAILQ_FIRST((head)) =					\
	     STAILQ_NEXT(STAILQ_FIRST((head)), field)) == NULL)		\
		(head)->stqh_last = &STAILQ_FIRST((head));		\
} while (0)
#endif

/** pnotify control block 

This structure MUST NOT be shared by multiple threads.

*/
struct pnotify_cb {

	/** Kernel event descriptor as returned by kqueue(4) or inotify_init(7) */
	int	fd;

	/** A list of watched files and/or directories */
	LIST_HEAD(, pnotify_watch) watch;

	/** A list of events that are ready to be delivered */
	STAILQ_HEAD(, pnotify_event) event;

	/** A counter used to assign unique watch IDs for pnotify_add_watch() */
	unsigned int next_wd;
};

/**
  Initialize a pnotify control block.

  The control block contains the current state of events and watches. 
  Before adding watches, the control block must be initialized via
  a call to pnotify_new(). 

  @param ctl pointer to a control block
  @return 0 if successful, or non-zero if an error occurred.
*/
int pnotify_init(struct pnotify_cb *ctl);


/**
  Add a watch.

  @param ctl a pnotify control block
  @param path full pathname of the file or directory to be watched
  @param mask a bitmask of events to be monitored
  @return a unique watch descriptor if successful, or -1 if an error occurred.
*/
int pnotify_add_watch(struct pnotify_cb *ctl, const char *pathname, int mask);


/**
  Remove a watch.

  @param ctl a pnotify control block
  @param wd FIXME --- WONT WORK
  @return 0 if successful, or non-zero if an error occurred.
*/
int pnotify_rm_watch(struct pnotify_cb *ctl, int wd);


/**
  Wait for an event to occur.

  @param evt an event structure that will store the result
  @param ctl a pnotify control block
  @return 0 if successful, or non-zero if an error occurred.
*/
int pnotify_get_event(struct pnotify_event *evt, struct pnotify_cb *ctl);


/**
 Print debugging information about an event to standard output.
 @param evt an event to be printed
*/
int pnotify_print_event(struct pnotify_event * evt);


/**
 Print the contents of the control block to standard output.
*/
void pnotify_dump(struct pnotify_cb *ctl);

/**
  Free all resources associated with an event queue.

  All internal data structures will be freed. 

  @return 0 if successful, or non-zero if an error occurred.
*/
int pnotify_free(struct pnotify_cb *ctl);


/** Event flags */
enum {

	/** The atime of a file has been modified */
	PN_ACCESS 		= 0x1 << 0,

	/** A file was created in a watched directory */
	PN_CREATE		= 0x1 << 1,

	/** A file was deleted from a watched directory */
	PN_DELETE		= 0x1 << 2,

	/** The modification time of a file has changed */
	PN_MODIFY		= 0x1 << 3,
	
	/** Automatically delete the watch after a matching event is triggered */
	PN_ONESHOT		= 0x1 << 4,

	/** Indicates an error condition in the underlying kernel event queue */
	PN_ERROR		= 0x1 << 5,

} __PN_BITMASK;

#define PN_ALL_EVENTS	(PN_ACCESS | PN_CREATE | PN_DELETE | PN_MODIFY)

/** An event */
struct pnotify_event {

	/** The watch descriptor returned by pnotify_add_watch() */
	int       wd;

	/** One or more bitflags containing the event(s) that occurred */
	int       mask;

	/** The filename associated with a directory entry creation/deletion.
		Only used when monitoring directories.
	*/
        char      name[NAME_MAX + 1];

	STAILQ_ENTRY(pnotify_event) entries;
};

#endif
