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


#if HAVE_KQUEUE

#include <sys/event.h>

const struct pnotify_vtable BSD_VTABLE = {
	.init_once = bsd_init_once,
	.add_watch = bsd_add_watch,
	.rm_watch = bsd_rm_watch,
	.trap_signal = bsd_trap_signal,
	.cleanup = bsd_cleanup,
};

/** The file descriptor returned by kqueue(2) */
static int KQUEUE_FD = -1;

void *
bsd_kqueue_loop(void * unused)
{
	const int nkev = 100;
	struct pn_watch *watch;
	struct kevent _kev[nkev];
	struct kevent *kev;
	int i, rc;

	/* Avoid a compiler warning */
	watch = unused;

	/* Create a kqueue descriptor */
	if ((KQUEUE_FD = kqueue()) < 0)
		err(1, "kqueue(2)");

	/* Loop forever waiting for events */
LOOP:

	/* Wait for an event */
	dprintf("waiting for kernel event..\n");
	rc = kevent(KQUEUE_FD, NULL, 0, &_kev, nkev, NULL);
	if (rc < 0) {
		warn("kevent(2) failed");
		return -1;
	}

	/* Process each event */
	for (i = 0; i < rc; i++) {

		kev = &_kev[i];

		/* Find the matching watch structure */
		watch = (struct pn_watch *) kev.udata;

		/* Add the event to the list of pending events */
		pn_event_add(watch->ctx, evt);

		/* Workaround:

		   Deleting a file in a watched directory causes two events:
		   NOTE_MODIFY on the directory
		   NOTE_DELETE on the file

		   We ignore the NOTE_DELETE on the file.
		   */
		if (watch->parent_wd && kev.fflags & NOTE_DELETE) {
			dprintf("ignoring NOTE_DELETE on a watched file\n");
			goto retry;
		}

		/* Convert the kqueue(4) flags to pnotify_event flags */
		if (!watch->is_dir) {
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

			/* If the event happened within a watched directory,
			   add the filename and the parent watch descriptor.
			   */
			if (watch->parent_wd) {

				/* KLUDGE: remove the leading basename */
				char *fn = strrchr(watch->path, '/') ;
				if (!fn) { 
					fn = watch->path;
				} else {
					fn++;
				}

				evt->wd = watch->parent_wd;
				/* FIXME: more error handling */
				(void) strncpy(evt->name, fn, strlen(fn));
			}

			/* Add the event to the list of pending events */
			memcpy(evp, evt, sizeof(*evp));
			STAILQ_INSERT_TAIL(&ctx->event, evp, entries);

			dprint_event(evt);

			/* Handle events on directories */
		} else {

			/* When a file is added or deleted, NOTE_WRITE is set */
			if (kev.fflags & NOTE_WRITE) {
				if (kq_directory_event_handler(kev, ctx, watch) < 0) {
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
	}

	goto LOOP;

	close(KQUEUE_FD);
	return NULL;
}


void
bsd_init_once(void)
{
	pthread_t tid;

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


int
bsd_add_watch(struct pnotify_ctx *ctx, struct pn_watch *watch)
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
bsd_rm_watch(struct pnotify_ctx *ctx, struct pn_watch *watch)
{
	if (inotify_rm_watch(INOTIFY_FD, watch->wd) < 0) {
		perror("inotify_rm_watch(2)");
		return -1;
	}

	return 0;
}

#if TODO
	// port to kqueue
	
void
bsd_dump_inotify_event(struct inotify_event *iev)
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

#endif


int
bsd_add_watch(struct pnotify_ctx *ctx, struct pn_watch *watch)
{
	struct stat     st;
	struct kevent *kev = &watch->kev;
	int mask = watch->mask;

	/* Open the file */
	if ((watch->fd = open(watch->path, O_RDONLY)) < 0) {
		warn("opening path `%s' failed", watch->path);
		return -1;
	}

	/* Test if the file is a directory */
	if (fstat(watch->fd, &st) < 0) {
		warn("fstat(2) failed");
		return -1;
	}
	watch->is_dir = S_ISDIR(st.st_mode);

	/* Initialize the directory structure, if needed */
	if (watch->is_dir) 
		directory_open(ctx, watch);

	/* Generate a new watch ID */
	/* FIXME - this never decreases and might fail */
	if ((watch->wd = ++ctx->next_wd) > WATCH_MAX) {
		warn("watch_max exceeded");
		return -1;
	}

	/* Create and populate a kevent structure */
	EV_SET(kev, watch->fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, 0, 0, watch);
	if (mask & PN_ONESHOT)
		kev->flags |= EV_ONESHOT;
	switch (watch->type) {

		case WATCH_FD:
			if (mask & PN_READ)
				kev->filter = EVFILT_READ;
			if (mask & PN_WRITE)
				kev->filter = EVFILT_WRITE;
			break;

		case WATCH_VNODE:
			kev->filter = EVFILT_VNODE;
			if (mask & PN_ACCESS || mask & PN_MODIFY)
				kev->fflags |= NOTE_ATTRIB;
			if (mask & PN_CREATE)
				kev->fflags |= NOTE_WRITE;
			if (mask & PN_DELETE)
				kev->fflags |= NOTE_DELETE | NOTE_WRITE;
			if (mask & PN_MODIFY)
				kev->fflags |= NOTE_WRITE;
		default:
			break;
	}

	/* Add the kevent to the kernel event queue */
	if (kevent(KQUEUE_FD, kev, 1, NULL, 0, NULL) < 0) {
		perror("kevent(2)");
		return -1;
	}

	return 0;
}


int
kq_rm_watch(struct pnotify_ctx *ctx, struct pn_watch *watch)
{
	/* Close the file descriptor.
	  The kernel will automatically delete the kevent 
	  and any pending events.
	 */
	if (close(watch->fd) < 0) {
		perror("unable to close watch fd");
		return -1;
	}

	/* Close the directory handle */
	if (watch->dir.dirp != NULL) {
		if (closedir(watch->dir.dirp) != 0) {
			perror("closedir(3)");
			return -1;
		}
	}

	return 0;
}

/**
 Open a watched directory.
*/
static int
directory_open(struct pnotify_ctx *ctx, struct pn_watch * watch)
{
	struct directory * dir;

	dir = &watch->dir;

	/* Initialize the li_directory structure */
	LIST_INIT(&dir->all);
	if ((dir->dirp = opendir(watch->path)) == NULL) {
		perror("opendir(2)");
		return -1;
	}

	/* Store the pathname */
	dir->path_len = strlen(watch->path);
	if ((dir->path_len >= PATH_MAX) || 
		((dir->path = malloc(dir->path_len + 1)) == NULL)) {
			perror("malloc(3)");
			return -1;
	}
	strncpy(dir->path, watch->path, dir->path_len);
		
	/* Scan the directory */
	if (directory_scan(ctx, watch) < 0) {
		warn("directory_scan failed");
		return -1;
	}

	return 0;
}


/**
 Scan a directory looking for new, modified, and deleted files.
 */
static int
directory_scan(struct pnotify_ctx *ctx, struct pn_watch * watch)
{
	struct pn_watch *wtmp;
	struct directory *dir;
	struct dirent   ent, *entp;
	struct dentry  *dptr;
	bool            found;
	char            path[PATH_MAX + 1];
	char           *cp;
	struct stat	st;

	assert(watch != NULL);

	dir = &watch->dir;

	dprintf("scanning directory\n");

	/* Generate the basename */
	(void) snprintf((char *) &path, PATH_MAX, "%s/", dir->path);
	cp = path + dir->path_len + 1;

	/* 
	 * Invalidate the status mask for all entries.
	 *  This is used to detect entries that have been deleted.
 	 */
	LIST_FOREACH(dptr, &dir->all, entries) {
		dptr->mask = PN_DELETE;
	}

	/* Skip the initial '.' and '..' entries */
	/* XXX-FIXME doesnt work with chroot(2) when '..' doesn't exist */
	rewinddir(dir->dirp);
	if (readdir_r(dir->dirp, &ent, &entp) != 0) {
		perror("readdir_r(3)");
		return -1;
	}
	if (strcmp(ent.d_name, ".") == 0) {
		if (readdir_r(dir->dirp, &ent, &entp) != 0) {
			perror("readdir_r(3)");
			return -1;
		}
	}

	/* Read all entries in the directory */
	for (;;) {

		/* Read the next entry */
		if (readdir_r(dir->dirp, &ent, &entp) != 0) {
			perror("readdir_r(3)");
			return -1;
		}

		/* Check for the end-of-directory condition */
		if (entp == NULL)
			break;
 
		/* Perform a linear search for the dentry */
		found = false;
		LIST_FOREACH(dptr, &dir->all, entries) {

			/*
			 * FIXME - BUG - this doesnt handle hardlinks which
			 * have the same d_fileno but different
			 * dirent structs
			 */
			//should compare the entire dirent struct...
			if (dptr->ent.d_fileno != ent.d_fileno) 
				continue;

			dprintf("old entry: %s\n", ent.d_name);
			dptr->mask = 0;

			found = true;
			break;
		}

		/* Add the entry to the list, if needed */
		if (!found) {
			dprintf("new entry: %s\n", ent.d_name);

			/* Allocate a new dentry structure */
			if ((dptr = malloc(sizeof(*dptr))) == NULL) {
				perror("malloc(3)");
				return -1;
			}

			/* Copy the dirent structure */
			memcpy(&dptr->ent, &ent, sizeof(ent));
			dptr->mask = PN_CREATE;

			/* Generate the full pathname */
			// BUG: name_max is not precise enough
			strncpy(cp, ent.d_name, NAME_MAX);

			/* Get the file status */
			if (stat((char *) &path, &st) < 0) {
				warn("stat(2) of `%s' failed", (char *) &path);
				return -1;
			}

			/* Add a watch if it is a regular file */
			if (S_ISREG(st.st_mode)) {
				if (pnotify_add_watch(ctx, path, 
					watch->mask) < 0)
						return -1;
				wtmp = LIST_FIRST(&ctx->watch);
				wtmp->parent_wd = watch->wd;
			}

			LIST_INSERT_HEAD(&dir->all, dptr, entries);
		}
	}

	return 0;
}



/**
 Handle an event inside a directory (kqueue version)
*/
static int
kq_directory_event_handler(struct kevent kev,
			   struct pnotify_ctx * ctx,
			   struct pn_watch * watch)
{
	struct pnotify_event *ev;
	struct dentry  *dptr, *dtmp;

	assert(ctx && watch);

	/* Re-scan the directory to find new and deleted files */
	if (directory_scan(ctx, watch) < 0) {
		warn("directory_scan failed");
		return -1;
	}

	/* Generate an event for each changed directory entry */
	LIST_FOREACH_SAFE(dptr, &watch->dir.all, entries, dtmp) {

		/* Skip files that have not changed */
		if (dptr->mask == 0)
			continue;

		/* Construct a pnotify_event structure */
		if ((ev = calloc(1, sizeof(*ev))) == NULL) {
			warn("malloc failed");
			return -1;
		}
		ev->wd = watch->wd;
		ev->mask = dptr->mask;
		(void) strlcpy(ev->name, dptr->ent.d_name, sizeof(ev->name));
		dprint_event(ev);

		/* Add the event to the list of pending events */
		STAILQ_INSERT_TAIL(&ctx->event, ev, entries);

		/* Remove the directory entry for a deleted file */
		if (dptr->mask & PN_DELETE) {
			LIST_REMOVE(dptr, entries);
			free(dptr);
		}
	}

	return 0;
}

#endif
