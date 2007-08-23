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

#include <fcntl.h>
#include <sys/event.h>

/* Forward declarations */
static int kq_directory_event_handler(struct kevent kev, struct pn_watch * watch);
static int directory_open(struct pn_watch * watch);
static int directory_scan(struct pn_watch * watch);
void bsd_dump_kevent(struct kevent kev);

/** The file descriptor returned by kqueue(2) */
static int KQUEUE_FD = -1;

static void
bsd_handle_fd_event(struct pn_watch *watch)
{
	struct kevent *kev = &watch->kev;
	struct pnotify_event *evt;

	/* Construct a pnotify_event structure */
	if ((evt = calloc(1, sizeof(*evt))) == NULL) 
		err(1, "malloc failed");

	/* Set the mask */
	if (kev->filter & EVFILT_READ)
		evt->mask |= PN_READ;
	if (kev->filter & EVFILT_WRITE)
		evt->mask |= PN_WRITE;
	if (kev->flags & EV_EOF)
		evt->mask |= PN_CLOSE;

	/* Add the event to the list of pending events */
	pn_event_add(watch->ctx, evt);

	dprint_event(evt);
}


static void
bsd_handle_vnode_event(struct pn_watch *watch)
{
	struct kevent *kev = &watch->kev;
	struct pnotify_event *evt;

	/* Workaround:

	   Deleting a file in a watched directory causes two events:
	   NOTE_MODIFY on the directory
	   NOTE_DELETE on the file

	   We ignore the NOTE_DELETE on the file.
	 */
	if (watch->parent_wd && kev->fflags & NOTE_DELETE) {
		dprintf("ignoring NOTE_DELETE on a watched file\n");
		return;
	}

	/* Convert the kqueue(4) flags to pnotify_event flags */
	if (!watch->is_dir) {

		/* Construct a pnotify_event structure */
		if ((evt = calloc(1, sizeof(*evt))) == NULL) 
			err(1, "malloc failed");

		if (kev->fflags & NOTE_WRITE)
			evt->mask |= PN_MODIFY;
		if (kev->fflags & NOTE_TRUNCATE)
			evt->mask |= PN_MODIFY;
		if (kev->flags & NOTE_EXTEND)
			evt->mask |= PN_MODIFY;
		if (kev->fflags & NOTE_ATTRIB)
			evt->mask |= PN_ATTRIB;
		if (kev->fflags & NOTE_DELETE)
			evt->mask |= PN_DELETE;

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

			evt->watch = watch;
			/* FIXME: more error handling */
			(void) strncpy(evt->name, fn, strlen(fn));
		}

		/* Add the event to the list of pending events */
		pn_event_add(watch->ctx, evt);

		dprint_event(evt);

		/* Handle events on directories */
	} else {

		/* When a file is added or deleted, NOTE_WRITE is set */
		if (kev->fflags & NOTE_WRITE) {
			if (kq_directory_event_handler(*kev, watch) < 0) {
				warn("error processing diretory");
				return;
			}
		}
		/* FIXME: Handle the deletion of a watched directory */
		else if (kev->fflags & NOTE_DELETE) {
			warn("unimplemented - TODO");
			return;
		} else {
			warn("unknown event recieved");
			return;
		}

	}
}


void *
bsd_kqueue_loop()
{
	struct pn_watch *watch;
	struct kevent kev;
	int rc;

	/* Create a kqueue descriptor */
	if ((KQUEUE_FD = kqueue()) < 0)
		err(1, "kqueue(2)");

	/* Loop forever waiting for events */
	for (;;) {

		/* Wait for an event */
		dprintf("waiting for kernel event..\n");
		rc = kevent(KQUEUE_FD, NULL, 0, &kev, 1, NULL);
		if (rc < 0) 
			err(1, "kevent(2) failed");

		bsd_dump_kevent(kev);

		/* Find the matching watch structure */
		watch = (struct pn_watch *) kev.udata;

		/* Handle the event */
		switch (watch->type) {
			case WATCH_FD:
				bsd_handle_fd_event(watch);
				break;
			case WATCH_VNODE:
				bsd_handle_vnode_event(watch);
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
bsd_dump_kevent(struct kevent kev)
{
	static const char *nam[] = {
		"EV_ADD", "EV_ENABLE", "EV_DISABLE", "EV_DELETE", "EV_ONESHOT",
		"EV_CLEAR", "EV_EOF", "EV_ERROR",
		"EVFILT_READ", "EVFILT_WRITE", "EVFILT_AIO", "EVFILT_VNODE",
		"EVFILT_PROC", "EVFILT_SIGNAL", "EVFILT_TIMER", "EVFILT_NETDEV",
		NULL
		};
	static const int val[] = {
		EV_ADD, EV_ENABLE, EV_DISABLE, EV_DELETE, EV_ONESHOT,
		EV_CLEAR, EV_EOF, EV_ERROR,
		EVFILT_READ, EVFILT_WRITE, EVFILT_AIO, EVFILT_VNODE,
		EVFILT_PROC, EVFILT_SIGNAL, EVFILT_TIMER, EVFILT_NETDEV,
		0
		};
	int i;

	fprintf(stderr, "kevent: ident=%d filter=", kev.ident);
	for (i = 0; val[i] != 0; i++) {
		if (kev.filter & val[i])
			fprintf(stderr, "%s ", nam[i]);
	}
	fprintf(stderr, "flags=");
	for (i = 0; val[i] != 0; i++) {
		if (kev.flags & val[i])
			fprintf(stderr, "%s ", nam[i]);
	}
	fprintf(stderr, "udata=%p", kev.udata);
	fprintf(stderr, "\n");
}


int
bsd_add_vnode_watch(struct pn_watch *watch)
{
	struct stat     st;

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
		directory_open(watch);
	return 0;
}

int
bsd_add_watch(struct pn_watch *watch)
{
	struct kevent *kev = &watch->kev;
	int mask = watch->mask;
	int i = 0;

	/* Create and populate a kevent structure */
	switch (watch->type) {

		case WATCH_SIGNAL:
			return 0;

		case WATCH_FD:
			if (mask & PN_READ)
				i = EVFILT_READ;
			if (mask & PN_WRITE)
				i = EVFILT_WRITE;
			EV_SET(kev, watch->ident.fd, i, EV_ADD | EV_CLEAR, 0, 0, watch);
			break;

		case WATCH_VNODE:
			if (bsd_add_vnode_watch(watch) < 0)
				return -1;
			EV_SET(kev, watch->fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, 0, 0, watch);
			if (mask & PN_ATTRIB)
				kev->fflags |= NOTE_ATTRIB;
			if (mask & PN_CREATE)
				kev->fflags |= NOTE_WRITE;
			if (mask & PN_DELETE)
				kev->fflags |= NOTE_DELETE | NOTE_WRITE;
			if (mask & PN_MODIFY)
				kev->fflags |= NOTE_WRITE;
			break;

		default:
			break;
	}

	/* Set the 'oneshot' flag */
	if (mask & PN_ONESHOT)
		kev->flags |= EV_ONESHOT;

	// FIXME - Testing
	kev->udata = watch;

	/* Add the kevent to the kernel event queue */
	if (kevent(KQUEUE_FD, kev, 1, NULL, 0, NULL) < 0) {
		perror("kevent(2)");
		return -1;
	}

	return 0;
}


int
bsd_rm_watch(struct pn_watch *watch)
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
directory_open(struct pn_watch * watch)
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
	if (directory_scan(watch) < 0) {
		warn("directory_scan failed");
		return -1;
	}

	return 0;
}


/**
 Scan a directory looking for new, modified, and deleted files.
 */
static int
directory_scan(struct pn_watch * watch)
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
				int wd;

				wd = pnotify_watch_vnode(path, watch->mask, NULL, NULL);
				if (wd < 0)
					return -1;
				wtmp = pn_get_watch_by_id(wd);
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
kq_directory_event_handler(struct kevent kev, struct pn_watch * watch)
{
	struct pnotify_ctx * ctx;
	struct pnotify_event *evt;
	struct dentry  *dptr, *dtmp;

	assert(watch);

	ctx = watch->ctx;

	/* Re-scan the directory to find new and deleted files */
	if (directory_scan(watch) < 0) {
		warn("directory_scan failed");
		return -1;
	}

	/* Generate an event for each changed directory entry */
	LIST_FOREACH_SAFE(dptr, &watch->dir.all, entries, dtmp) {

		/* Skip files that have not changed */
		if (dptr->mask == 0)
			continue;

		/* Construct a pnotify_event structure */
		if ((evt = calloc(1, sizeof(*evt))) == NULL) {
			warn("malloc failed");
			return -1;
		}
		evt->watch = watch;
		evt->mask = dptr->mask;
		(void) strlcpy(evt->name, dptr->ent.d_name, sizeof(evt->name));
		dprint_event(evt);

		/* Add the event to the list of pending events */
		pn_event_add(watch->ctx, evt);

		/* Remove the directory entry for a deleted file */
		if (dptr->mask & PN_DELETE) {
			LIST_REMOVE(dptr, entries);
			free(dptr);
		}
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
