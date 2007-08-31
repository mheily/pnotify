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

#ifndef _NIO_H
#define _NIO_H

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

/** @file nio.h

    Nonblocking I/O library.

*/

/* Constants that correspond with syscalls or libc functions */
enum nio_op {
	NIO_INVALID = 0,

	/* Disk I/O */
	NIO_OPEN,
	NIO_CLOSE,
	NIO_READ,
	NIO_WRITE,
	NIO_LSEEK,
	NIO_OPENDIR,
	NIO_CLOSEDIR,
	NIO_REWINDDIR,
	NIO_READDIR,
	NIO_TELLDIR,
	NIO_LINK,
	NIO_SYMLINK,
	NIO_UNLINK,
	NIO_RENAME,
	NIO_FCNTL,
	NIO_STAT,
	NIO_FSTAT,
	NIO_LSTAT,
	NIO_FDATASYNC,
	NIO_FSYNC,
	NIO_TRUNCATE,
	NIO_FTRUNCATE,

	/* Network I/O */
	NIO_GETADDRINFO,
	NIO_GETNAMEINFO,
};

// TODO: split into nio_req and nio_res; have nio_req STAILQ to enqueue requests
//

/** A nonblocking I/O operation control block */
struct niocb {

	/* The 'request' data fields filled in by the caller */

	enum nio_op req_op;		/**< The requested system call */
	int         req_fd;		/**< An open file descriptor */
	off_t       req_offset;		/**< The offset for lseek(2) */
	size_t      req_len;		/**< The length for read(2) */
	mode_t      req_mode;		/**< The mode for open(2) */
	char       *req_path;		/**< The path for open(2) */
	int         req_flags;		/**< Bitmask flags for open(2) */      

	/* The 'response' data fields filled in after the syscall completes */

	union {
		struct stat    st;	/**< File statistics from stat(2) */
		struct dirent  ent;	/**< Directory entry from readdir(2) */
		int            fd;	/**< File descriptor from open(2) */
		size_t         len;	/**< The length of data from read(2) */
		DIR           *dirh;    /**< Directory handle from opendir(2) */	
		off_t          offset;  /**< File position from lseek(2) */
	} res_data;
	int         res_errno;		/**< The value of errno(3) after the syscall */
	int         res_retval;		/**< The return value of the syscall */
	int         res_shmid;		/**< The id of the shared memory segment */
};


/* Replacement for POSIX I/O system calls */

/** Wrapper for open(2) */
static inline int 
nio_open(const char *path, int flags, mode_t mode, struct pn_callback *cb)
{
	return pnotify_call_function(CB_ENCODE(open, 3, path, flags, mode), cb);
}

/** Wrapper for read(2) */
static inline int
nio_read(int fd, void *buf, size_t count, struct pn_callback *cb)
{
	return pnotify_call_function(FUNC_ENCODE(read, 3, fd, buf, count), cb);
}

// NOT NEEDED: int nio_write(struct niocb *cb, int fd, const void *buf, size_t count);
int nio_lseek(int fd, off_t offset, int whence, struct pn_callback *cb);

/* opendir(2) related */

int nio_opendir(struct niocb *cb, const char *name);
int nio_closedir(struct niocb *cb, DIR *dir);
int nio_rewinddir(struct niocb *cb, DIR *dir);
int nio_readdir(struct niocb *cb, DIR *dir);
int nio_telldir(struct niocb *cb, DIR *dir);

/* link(2) related */

int nio_link(struct niocb *cb, const char *oldpath, const char *newpath);
int nio_symlink(struct niocb *cb, const char *oldpath, const char *newpath);
int nio_unlink(struct niocb *cb, const char *path);
int nio_rename(struct niocb *cb, const char *oldpath, const char *newpath);

/* fnctl(2) file locking */

int nio_fcntl(struct niocb *cb, int fd, int op, const struct flock *fl);

/* stat(2) related */

int nio_stat(struct niocb *cb, const char *path);
int nio_fstat(struct niocb *cb, int fd);
int nio_lstat(struct niocb *cb, const char *path);

/* sync(2) related */

int nio_fdatasync(struct niocb *cb, int fd);
int nio_fsync(struct niocb *cb, int fd);

/* truncate(2) related */

int nio_truncate(struct niocb *cb, const char *path, off_t length);
int nio_ftruncate(struct niocb *cb, int fd, off_t length);

/* network I/O */

int nio_getaddrinfo(struct niocb *cb, 
		const char *node,
		const char *service,
		const struct addrinfo *hints,
		struct addrinfo **res);

int nio_getnameinfo(struct niocb *cb,
		const struct sockaddr *sa, 
		socklen_t salen,
		char *host,
		size_t hostlen,
                char *serv,
		size_t servlen,
		int flags);

#endif
