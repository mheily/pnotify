/*	$Id: $	*/

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
 *  Asynchronous function invocation.
 *
*/
 
#include "config.h"

#include "pnotify-internal.h"
#include "pnotify.h"


void
pn_func_dump(struct pn_watch *watch)
{
	struct pn_callback *func = watch->ident.func;

	printf("func: retval=%d errno=%d\n",
			func->retval, func->saved_errno
	      );
}

void *
pn_func_invoke(void *arg)
{
	struct pn_watch *watch = arg;
	int rv = -1;
	struct pn_callback *func = watch->ident.func;
	int (*fp)() = func->symbol;

#define _arg(i) (func->argt[i] == PN_TYPE_INT ? \
			func->argv[i].arg_int : func->argv[i].arg_long)

	dprintf("running function (argc=%zu)\n", func->argc);
	if (func->argc == 1) 
		rv = fp(_arg(0));
	else if (func->argc == 2) 
		rv = fp(_arg(0), _arg(1));
	else if (func->argc == 3) 
		rv = fp(_arg(0), _arg(1), _arg(2));
	else if (func->argc == 4) 
		rv = fp(_arg(0), _arg(1), _arg(2), _arg(3));
	else if (func->argc == 5) 
		rv = fp(_arg(0), _arg(1), _arg(2), _arg(3), _arg(4));
	else if (func->argc == 6) 
		rv = fp(_arg(0), _arg(1), _arg(2), _arg(3), _arg(4), _arg(5));
	else if (func->argc == 7) 
		rv = fp(_arg(0), _arg(1), _arg(2), _arg(3), _arg(4), _arg(5),
			_arg(6));
	else if (func->argc == 8) 
		rv = fp(_arg(0), _arg(1), _arg(2), _arg(3), _arg(4), _arg(5),
			_arg(6), _arg(7));
	else 
		errx(1, "FIXME - UNIMPLEMENTED");

#undef _arg

	/* Store the return value and errno value */
	func->saved_errno = errno;
	func->retval = rv;

	printf("function complete!\n");
	pn_func_dump(watch);

	/* Add the event to an event queue */
	pn_event_add(watch, PN_RETURN, NULL);

#if FIXME
	// wait until the caller reads the watch variable before deleting it !
	//
	/* Remove the watch */
	pnotify_rm_watch(watch->wd);
#endif

	pthread_exit(NULL);
}

int
pn_call_function(struct pn_watch *watch)
{
	pthread_t tid;

	/* Create a thread to execute the function call */
	/* TODO: use detached threads */
	if (pthread_create(&tid, NULL, pn_func_invoke, (void *) watch) != 0) {
		perror("pthread_create(3) failed");
		return -1;
	}

	return 0;
}


