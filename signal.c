/*	$Id: signal.nc 22 2007-03-24 21:16:33Z mark $	*/

/*
 * Copyright (c) 2004, 2005, 2006, 2007 Mark Heily <devel@heily.com>
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
 *  Signal handling.
 *
*/
 
#include "config.h"

#include <assert.h>
#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "pnotify-internal.h"
#include "pnotify.h"

/** The watch for each signal event, indexed by signal number */
struct watch *SIG_WATCH[NSIG + 1];


static void
default_signal_handler(int signum)
{
	switch (signum) {

		case SIGCHLD:
			/* Ignored */
			break;

		case SIGINT:
			fprintf(stderr, "Caught SIGINT, exiting..\n");
			exit(1);
			break;

		case SIGTERM:
			fprintf(stderr, "Caught SIGTERM, exiting..\n");
			exit(1);
			break;
		default:
			fprintf(stderr, "Caught signal %d but no handler..\n", signum);
			exit(254);
	}
}

void *
pn_signal_loop(void * unused)
{
	sigset_t signal_set;
	struct watch *watch;
	int signum;

	/* Avoid a compiler warning */
	watch = unused;

	/* Loop forever waiting for signals */
	for (;;) {

		/* Wait for a signal */
		sigfillset(&signal_set);
		sigdelset(&signal_set, SIGALRM);
		dprintf("sigwait..\n");
		sigwait(&signal_set, &signum);

		/* Determine if the signal is being watched */
		watch = SIG_WATCH[signum];
		if (watch != NULL) {
			/* Add the event to an event queue */
			pn_event_add(watch, PN_SIGNAL);
		} else {
			default_signal_handler(signum);
			continue;
		}

	}

	return NULL;
}

void
pn_mask_signals()
{
	sigset_t signal_set;

	/* Block all signals */
	sigfillset(&signal_set);
	if (pthread_sigmask(SIG_BLOCK, &signal_set, NULL) != 0)
		err(1, "pthread_sigmask(3)");
}
