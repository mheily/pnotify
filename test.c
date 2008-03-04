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
	struct event    *evt;
 	struct watch *w;

	printf("signal tests\n");
	test ((w = watch_signal(SIGUSR1, NULL, NULL)));
	test (kill(getpid(), SIGUSR1));
	evt = event_wait();
	if (!event_cmp(evt, w, 0)) 
		err(1, "unexpected event value");
	printf("signal tests complete\n");
}

static void
test_fd()
{
	struct event *evt;
 	struct watch *w;
	int fildes[2];

	printf("fd tests\n");
	test (pipe(fildes));
	test ((w = watch_fd(fildes[0], NULL, NULL)));
	if (write(fildes[1], "a", 1) != 1)
		err(1, "write(2)");
	evt = event_wait();
	if (!event_cmp(evt, w, PN_READ)) 
		err(1, "unexpected event value");
	printf("fd tests complete\n");
}

static void
test_timer()
{
	struct event *evt;
 	struct watch *w;

	printf("timer tests\n");
	test ((w = watch_timer(1, NULL, NULL)));
	evt = event_wait();
	printf("timer tests complete\n");
}

static void 
test_callback(void *arg)
{
	printf("all tests passed.\n");
	exit(EXIT_SUCCESS);
}

static void
test_dispatch()
{
	test (watch_timer(1, test_callback, 0));
	event_dispatch();
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

	pnotify_init();
	test_fd();
	test_signals();
	test_timer();
	test_dispatch(); 
}
