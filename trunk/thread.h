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

#ifndef _THREAD_H_
#define _THREAD_H_

/* 
 * These macros provide a subset of the POSIX threads API 
 * even when compiling without support for threads.
 */
#ifdef _REENTRANT

#include <pthread.h>

/*
 * Convenience macros for locking/unlocking structures 
 * which contain a 'mutex' member.
 */
#define mutex_debug 0

#define mutex_lock(st)		do {					\
   if (mutex_debug) 							\
	printf("%s takes the lock\n", __func__);			\
   (void) pthread_mutex_lock(&st->mutex);				\
} while (0)

#define mutex_unlock(st)	do {					\
   if (mutex_debug)			 				\
	printf("  %s releases the lock\n", __func__);			\
   (void) pthread_mutex_unlock(&st->mutex);				\
} while (0)


#else 

/* All locking operations become a NOOP when thread support is disabled */

#define pthread_noop             (0)
#define pthread_t                int
#define pthread_key_t            int
#define pthread_mutex_t          int
#define pthread_mutex_init(x,y)  pthread_noop
#define pthread_mutex_lock(x)    pthread_noop
#define pthread_mutex_unlock(x)  pthread_noop
#define pthread_key_create(x,y)  pthread_noop
#define pthread_key_delete(x)    pthread_noop
#define pthread_getspecific(x)   pthread_noop
#define pthread_setspecific(x,y) pthread_noop
#define pthread_sigmask(x,y,z)   setsigmask(x,y,z)
#define mutex_lock(x)            do { ; } while (0)
#define mutex_unlock(x)          do { ; } while (0)

/* pthread_once is emulated for single-threaded code */
#define pthread_once_t           int
#define PTHREAD_ONCE_INIT        0
#define pthread_once(var,func)	do { 				\
	  if (!*var)   							\
	       { func(); *var = 1; }					\
        } while (0)

/* 
 * pthread_cleanup_push() is not emulated since all resources are freed
 * when a process exits. This means that cleanup handlers should *only*
 * be used to deallocate memory.
 * 
 * @todo this should probably be emulated 
 */
#define pthread_cleanup_push(x,y) pthread_noop

#endif /* ! _REENTRANT */

#endif /* _THREAD_H_ */
