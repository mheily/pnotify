#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "pnotify.h"
#include "pnotify-internal.h"

/* Global context variable */
struct pnotify_ctx *ctx;

/* Compare a pnotify_event against an expected set of values */
int
event_cmp(struct event *ev, struct watch *watch, int mask)
{
	int i = (ev->watch == watch) &&
		(ev->mask == mask);
		
	if (!i) {
		printf(" *ERROR * mismatch: expecting (watch:mask) of '%p:%d' but got '%p:%d'\n",
			  watch, mask,
			  ev->watch, ev->mask);
	}
	return i;
}

#define test(x) do { \
   printf(" * " #x ": "); 				\
   fflush(stdout);					\
   printf("%s\n", ((x) >= 0) ? "passed" : "failed");	\
} while (0)

static void
test_signals()
{
	struct event    evt;
 	struct watch *w;

	printf("signal tests\n");
	test ((w = watch_signal(SIGUSR1, NULL, NULL)));
	test (kill(getpid(), SIGUSR1));
	test (event_wait(&evt));
	if (!event_cmp(&evt, w, PN_SIGNAL)) 
		err(1, "unexpected event value");
	printf("signal tests complete\n");
}

static void
test_fd()
{
	struct event evt;
 	struct watch *w;
	int fildes[2];

	printf("fd tests\n");
	test (pipe(fildes));
	test ((w = watch_fd(fildes[0], PN_READ, NULL, NULL)));
	if (write(fildes[1], "a", 1) != 1)
		err(1, "write(2)");
	test (event_wait(&evt));
	if (!event_cmp(&evt, w, PN_READ)) 
		err(1, "unexpected event value");
	printf("fd tests complete\n");
}

static void
test_timer()
{
	struct event evt;
 	struct watch *w;

	printf("timer tests\n");
	test ((w = watch_timer(1, PN_ONESHOT, NULL, NULL)));
	test (event_wait(&evt));
	printf("timer tests complete\n");
}

static void 
test_callback(int signum)
{
	printf("all tests passed.\n");
	exit(EXIT_SUCCESS);
}

static void
test_dispatch()
{
	test (watch_timer(1, PN_DEFAULT, test_callback, 0));
	test (event_dispatch());
}


int
main(int argc, char **argv)
{
	/* Create a test directory */
	(void) system("rm -rf .check");
	if (system("mkdir .check") < 0)
		err(1, "mkdir failed");
	if (system("mkdir .check/dir") < 0)
		err(1, "mkdir failed");

	/* Initialize the queue */
	test(ctx = pnotify_init());

	test_fd();
	test_signals();
	test_timer();
	test_dispatch(); 
}
