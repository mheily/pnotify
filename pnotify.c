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
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include "pnotify.h"

// FIXME - WORKAROUND
#if __linux__
# define HAVE_INOTIFY 1
# define HAVE_KQUEUE 0
#else
# define HAVE_INOTIFY 0
# define HAVE_KQUEUE 1
#endif

#if HAVE_KQUEUE
# include <sys/event.h>
#elif HAVE_INOTIFY
# include <sys/inotify.h>
#endif

/* The 'dprintf' macro is used for printf() debugging */
#ifdef PNOTIFY_DEBUG
# define dprintf printf
#else
# define dprintf(...) do { ; } while (0)
#endif

/** The maximum number of watches that a single controller can have */
static const int WATCH_MAX = 20000;

/** An entire directory that is under watch */
struct directory {

	/* All entries in the directory (struct dirent) */
	LIST_HEAD(, dentry) all;

	/* All 'new' entries in the directory */
	LIST_HEAD(, dentry) new;

	/* All 'deleted' entries in the directory */
	LIST_HEAD(, dentry) del;
};

/** A directory entry list element */
struct dentry {
	struct dirent   ent;
        LIST_ENTRY(dentry) entries;
};

/** An internal watch entry */
struct pnotify_watch {
	int             fd;	/**< kqueue(4) watched file descriptor */
	int             wd;	/**< Unique 'watch descriptor' */
	char            path[PATH_MAX + 1];	/**< Path associated with fd */
	uint32_t        mask;	/**< Mask of monitored events */

	bool            is_dir;	/**< TRUE, if the file is a directory */

	/* A list of directory entries (only used if is_dir is true) */
	struct directory dir;

	/* Pointer to the next watch */
	LIST_ENTRY(pnotify_watch) entries;
};

/* Forward declarations */
#if HAVE_KQUEUE
static int      scan_directory(struct directory * dir, int fd);
static int      kq_directory_event_handler(struct kevent kev, struct pnotify_cb * ctl, struct pnotify_watch * watch);
#endif

int
pnotify_init(struct pnotify_cb *ctl)
{
	assert(ctl != NULL);

	memset(ctl, 0, sizeof(*ctl));

#if HAVE_KQUEUE
	ctl->fd = kqueue();
#elif HAVE_INOTIFY
	ctl->fd = inotify_init();
#else
# error STUB
#endif
	if (ctl->fd < 0) {
		warn("unable to create event descriptor");
		return -1;
	}

	LIST_INIT(&ctl->watch);
	STAILQ_INIT(&ctl->event);

	return 0;
}


int
pnotify_add_watch(struct pnotify_cb *ctl, const char *pathname, int mask)
{
	struct pnotify_watch *watch;

	assert(ctl && pathname);

	/* Allocate a new entry */
	if ((watch = malloc(sizeof(*watch))) == NULL) {
		warn("malloc error");
		return -1;
	}
	(void) strncpy((char *) &watch->path, pathname, sizeof(watch->path));	

#if HAVE_KQUEUE

	struct kevent   kev;
	struct stat     st;

	/* Open the file */
	if ((watch->fd = open(pathname, O_RDONLY)) < 0) {
		warn("opening path `%s' failed", pathname);
		goto err;
	}

	/* Test if the file is a directory */
	if (fstat(watch->fd, &st) < 0) {
		warn("fstat(2) failed");
		goto err;
	}
	watch->is_dir = S_ISDIR(st.st_mode);

	/* Initialize the directory structure, if needed */
	if (watch->is_dir) {

		/* Initialize the li_directory structure */
		LIST_INIT(&watch->dir.all);
		LIST_INIT(&watch->dir.new);
		LIST_INIT(&watch->dir.del);

		/* Scan the directory */
		if (scan_directory(&watch->dir, watch->fd) < 0) {
			warn("scan_directory failed");
			goto err;
		}
	}

	/* Generate a new watch ID */
	/* FIXME - this never decreases and might fail */
	if ((watch->wd = ++ctl->next_wd) > WATCH_MAX) {
		warn("watch_max exceeded");
		goto err;
	}

	/* Create a kernel event structure */
	EV_SET(&kev, watch->fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, 0, 0, 0);
	if (mask & PN_ACCESS || mask & PN_MODIFY)
		kev.fflags |= NOTE_ATTRIB;
	if (mask & PN_CREATE)
		kev.fflags |= NOTE_WRITE;
	if (mask & PN_DELETE)
		kev.fflags |= NOTE_DELETE | NOTE_WRITE;
	if (mask & PN_MODIFY)
		kev.fflags |= NOTE_WRITE | NOTE_TRUNCATE | NOTE_EXTEND;
	if (mask & PN_ONESHOT)
		kev.flags |= EV_ONESHOT;

	/* Add the event to the kernel event queue */
		if (kevent(ctl->fd, &kev, 1, NULL, 0, NULL) < 0) {
		warn("kevent(2) failed");
		goto err;
	}

#elif HAVE_INOTIFY

	uint32_t        imask = 0;

	/* Generate the mask */
	if (mask & PN_ACCESS || mask & PN_MODIFY)
		imask |= IN_ACCESS | IN_ATTRIB;
	if (mask & PN_CREATE)
		imask |= IN_CREATE;
	if (mask & PN_DELETE)
		imask |= IN_DELETE | IN_DELETE_SELF;
	if (mask & PN_MODIFY)
		imask |= IN_MODIFY;
	if (mask & PN_ONESHOT)
		imask |= IN_ONESHOT;

	/* Add the event to the kernel event queue */
	if ((watch->wd = inotify_add_watch(ctl->fd, pathname, imask)) < 0) {
		warn("inotify_add_watch(2) failed");
		goto err;
	}

#else
# error STUB
#endif

	/* Update the control block */
	LIST_INSERT_HEAD(&ctl->watch, watch, entries);

	dprintf("added watch: wd=%d mask=%d path=%s\n", 
		watch->wd, watch->mask, watch->path);

	return watch->wd;

err:
	free(watch);
	return -1;
}


/**
  Remove a watch.

  @param ctl a pnotify control block
  @param wd FIXME --- WONT WORK
  @return 0 if successful, or -1 if an error occurred.
*/
int 
pnotify_rm_watch(struct pnotify_cb * ctl, int wd)
{
	struct pnotify_watch *watchp;
	struct pnotify_event *evt;

	assert(ctl && wd >= 0);

	/* Find the matching watch structure */
	LIST_FOREACH(watchp, &ctl->watch, entries) {
		if (watchp->wd == wd)
			break;
	}
	if (!watchp) {
		warn("watch # %d not found", wd);
		return -1;
	}

#if HAVE_KQUEUE

	/* Close the file descriptor (this causes the kevent to be deleted) */
	if (close(watchp->fd) < 0) {
		perror("unable to close watch fd");
		return -1;
	}

#elif HAVE_INOTIFY
	
	// FIXME - this causes an IN_IGNORE event to be generated; need to handle
	if (inotify_rm_watch(ctl->fd, wd) < 0) {
		perror("inotify_rm_watch(2)");
		return -1;
	}

#else
# error STUB
#endif

	/* Remove any pending events associated with the watch */
	/* Sets the mask to zero so the event will be ignored */
	STAILQ_FOREACH(evt, &ctl->event, entries) {
		if (evt->wd == wd) 
			evt->mask = 0;
	}

	/* Delete the watchlist entry and free the watch struct */
	LIST_REMOVE(watchp, entries);
	free(watchp);

	return 0;
}


int
pnotify_get_event(struct pnotify_event * evt,
		  struct pnotify_cb * ctl
		 )
{
	struct pnotify_watch *watchp;
	struct pnotify_event *evp;

	assert(evt && ctl);

	/* If there are pending events in the queue, return the first one */
	if (!STAILQ_EMPTY(&ctl->event)) 
		goto next_event;

retry_wait:

	/* Reset the event structure to be empty */
	memset(evt, 0, sizeof(*evt));

#if HAVE_KQUEUE
	struct kevent   kev;
	int rc;

	/* Wait for an event notification from the kernel */
	rc = kevent(ctl->fd, &kev, 0, &kev, 1, NULL);
	if (rc < 0) {
		warn("kevent(2) failed");
		return -1;
	}
	if (kev.flags & EV_ERROR) {
		evt->mask = EV_ERROR;
		return 0;
	}
	/* Find the matching watch structure */
	LIST_FOREACH(watchp, &ctl->watch, entries) {
		if (watchp->fd == kev.ident)
			break;
	}
	if (!watchp) {
		warn("watch not found for event");
		goto retry_wait;
	}

	/* Convert the kqueue(4) flags to pnotify_event flags */
	if (!watchp->is_dir) {
		if (kev.fflags & NOTE_WRITE)
			evt->mask |= PN_MODIFY;
		if (kev.fflags & NOTE_TRUNCATE)
			evt->mask |= PN_MODIFY;
		if (kev.fflags & NOTE_EXTEND)
			evt->mask |= PN_MODIFY;
#if TODO
		// not implemented yet
		if (kev.fflags & NOTE_ATTRIB)
			evt->mask |= PN_ATTRIB;
#endif
		if (kev.fflags & NOTE_DELETE)
			evt->mask |= PN_DELETE;

		/* Construct a pnotify_event structure */
		if ((evp = calloc(1, sizeof(*evp))) == NULL) {
			warn("malloc failed");
			return -1;
		}

		/* Add the event to the list of pending events */
		memcpy(evp, evt, sizeof(*evp));
		STAILQ_INSERT_TAIL(&ctl->event, evp, entries);
	
	} else {

		/* When a file is added or deleted, NOTE_WRITE is set */
		if (kev.fflags & NOTE_WRITE) {
			if (kq_directory_event_handler(kev, ctl, watchp) < 0) {
				warn("error processing diretory");
				return -1;
			}
		}
		/* FIXME: Handle the deletion of a watched directory */
		else if (kev.fflags & NOTE_DELETE) {
				warn("unimplemented - TODO");
				return -1;
		} else {
				warn("unknown event recieved");
				return -1;
		}

	}

#elif HAVE_INOTIFY

	struct inotify_event *iev, *endp;
	ssize_t         bytes;
	char            buf[4096];

retry:

	/* Read the event structure */
	bytes = read(ctl->fd, &buf, sizeof(buf));
	if (bytes <= 0) {
		if (errno == EINTR)
			goto retry;
		else
			perror("read(2)");

		return -1;
	}
	/* Compute the beginning and end of the event list */
	iev = (struct inotify_event *) & buf;
	endp = iev + bytes;

	/* Process each pending event */
	while (iev < endp) {

		/* XXX-WORKAROUND */
		if (iev->wd == 0)
			break;

		/* Find the matching watch structure */
		LIST_FOREACH(watchp, &ctl->watch, entries) {
			if (watchp->wd == iev->wd)
				break;
		}
		if (!watchp) {
			warn("watch # %d not found", iev->wd);
			return -1;
		}
		/* Construct a pnotify_event structure */
		if ((evp = calloc(1, sizeof(*evp))) == NULL) {
			warn("malloc failed");
			return -1;
		}
		evp->wd = watchp->wd;
		(void) strncpy(evp->name, iev->name, iev->len);
		if (iev->mask & IN_ACCESS)
			evp->mask |= PN_ACCESS;
#if TODO
	// not implemented
		if (iev->mask & IN_ATTRIB)
			evp->mask |= PN_ATTRIB;
#endif
		if (iev->mask & IN_MODIFY)
			evp->mask |= PN_MODIFY;
		if (iev->mask & IN_CREATE)
			evp->mask |= PN_CREATE;
		if (iev->mask & IN_DELETE)
			evp->mask |= PN_DELETE;
		if (iev->mask & IN_DELETE_SELF) {
			evp->mask |= PN_DELETE;
			(void) strncpy(evp->name, "", 0);
		}

		/* Add the event to the list of pending events */
		STAILQ_INSERT_TAIL(&ctl->event, evp, entries);

		/* Go to the next event */
		iev += sizeof(*iev) + iev->len;
	}

#else
#error STUB
#endif

next_event:

	/* Shift the first element off of the pending event queue */
	if (!STAILQ_EMPTY(&ctl->event)) {
		evp = STAILQ_FIRST(&ctl->event);
		STAILQ_REMOVE_HEAD(&ctl->event, entries);
		memcpy(evt, evp, sizeof(*evt));
		free(evp);

		/* If the event mask is zero, get the next event */
		if (evt->mask == 0)
			goto next_event;
	} else {
		//warn("spurious wakeup");
		goto retry_wait;
	}

	return 0;
}


int
pnotify_print_event(struct pnotify_event * evt)
{
	assert(evt);

	return printf("event: wd=%d mask=(%s%s%s%s%s) name=`%s'\n",
		      evt->wd,
		      evt->mask & PN_ACCESS ? "access," : "",
		      evt->mask & PN_CREATE ? "create," : "",
		      evt->mask & PN_DELETE ? "delete," : "",
		      evt->mask & PN_MODIFY ? "modify," : "",
		      evt->mask & PN_ERROR ? "error," : "",
		      evt->name);
}


void
pnotify_dump(struct pnotify_cb *ctl)
{
	struct pnotify_event *evt;

	printf("\npending events:\n");
	STAILQ_FOREACH(evt, &ctl->event, entries) {
		printf("\t");
		(void) pnotify_print_event(evt);
	}
	printf("/* end: pending events */\n");
}


int
pnotify_free(struct pnotify_cb *ctl)
{
	struct pnotify_watch *watch;
	struct pnotify_event *evt, *nxt;

	assert(ctl != NULL);

	/* Delete all watches */
	LIST_FOREACH(watch, &ctl->watch, entries) {
		LIST_REMOVE(watch, entries);
		free(watch);
	}

	/* Delete all pending events */
  	evt = STAILQ_FIRST(&ctl->event);
        while (evt != NULL) {
		nxt = STAILQ_NEXT(evt, entries);
		free(evt);
		evt = nxt;
    	}

	/* Close the event descriptor */
	if (close(ctl->fd) < 0) {
		perror("close(2)");
		return -1;
	}

	return 0;
}


/*-------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------*/

#if HAVE_KQUEUE

/**
 Scan a directory looking for new, modified, and deleted files.
 */
static int
scan_directory(struct directory * dir, int fd)
{
	char           *buf, *ebuf, *cp;
	long            base;
	size_t          bufsize;
	int             nbytes;
	struct stat     sb;
	struct dirent  *dp;
	struct dentry  *dentry, *dptr, *dtmp;
	bool            found;

	assert(dir != NULL);

	/* Delete all entries from the 'deleted' list */
	if (!LIST_EMPTY(&dir->del)) {
		dptr = LIST_FIRST(&dir->del);
		while (dptr != NULL) {
			dtmp = LIST_NEXT(dptr, entries);
			free(dptr);
			dptr = dtmp;
		}
	}

	/* Compute the buffer size and allocate a buffer */
	if (fstat(fd, &sb) < 0)
		err(2, "fstat");
	bufsize = sb.st_size;
	if (bufsize < sb.st_blksize)
		bufsize = sb.st_blksize;
	if ((buf = malloc(bufsize)) == NULL)
		err(2, "cannot malloc %lu bytes", (unsigned long) bufsize);

	/* Read the entire directory using buffer-sized chunks */
	(void) lseek(fd, 0, SEEK_SET);
	while ((nbytes = getdirentries(fd, buf, bufsize, &base)) > 0) {
		ebuf = buf + nbytes;
		cp = buf;
		while (cp < ebuf) {
			dp = (struct dirent *) cp;

			/* Perform a linear search for the dentry */
			found = false;
			LIST_FOREACH(dptr, &dir->all, entries) {
				/*
				 * BUG - this doesnt handle hardlinks which
				 * have the same d_fileno but different
				 * dirent structs
				 */
				//should compare the entire dirent struct...

				if (dptr->ent.d_fileno == dp->d_fileno) {
					dprintf("old entry: %s\n", dp->d_name);
					found = true;
					break;
				}
			}

			/* Add the entry to the 'new' list if needed */
			if (!found) {
				dprintf("new entry: %s\n", dp->d_name);
				dentry = malloc(sizeof(*dentry));
				if (dentry == NULL)
					err(2, "cannot malloc %lu bytes",
					    (unsigned long) bufsize);
				memcpy(&dentry->ent, dp, sizeof(struct dirent));

				LIST_INSERT_HEAD(&dir->all, dentry, entries);
				LIST_INSERT_HEAD(&dir->new, dentry, entries);
			}
			cp += dp->d_reclen;
		}
	}
	if (nbytes < 0)
		err(2, "getdirentries");

	return 0;
}



/**
 Handle an event inside a directory (kqueue version)
*/
static int
kq_directory_event_handler(struct kevent kev,
			   struct pnotify_cb * ctl,
			   struct pnotify_watch * watch)
{
	struct pnotify_event *ev;
	struct dentry  *dptr, *dtmp;

	assert(ctl && watch);


	/* Re-scan the directory to find new and deleted files */
	if (scan_directory(&watch->dir, watch->fd) < 0)
		err(3, "scan_directory failed");

	/* Generate an event for each 'new' file */
	dptr = LIST_FIRST(&watch->dir.new);
	while (dptr != NULL) {

		/* Construct a pnotify_event structure */
		if ((ev = calloc(1, sizeof(*ev))) == NULL) {
			warn("malloc failed");
			return -1;
		}
		ev->wd = watch->wd;
		ev->mask = PN_CREATE;
		(void) strlcpy(ev->name, dptr->ent.d_name, sizeof(ev->name));

		/* Add the event to the list of pending events */
		STAILQ_INSERT_TAIL(&ctl->event, ev, entries);

		/* Remove the entry from the 'new' file list */
		dtmp = LIST_NEXT(dptr, entries);
		free(dptr);
		dptr = dtmp;
	}

	/* XXX - FIXME */

	/** @todo Generate an event for each 'deleted' file */

	/** @todo Generate an event for each 'modified' file */

	return 0;
}
#endif
