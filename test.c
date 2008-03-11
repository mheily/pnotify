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

int FD_RESULT = -1;
int TIMER_RESULT = -1;
int SIGNAL_RESULT = -1;

#define test(x) do { \
   printf(" * " #x ": "); 				\
   fflush(stdout);					\
   printf("%s\n", ((x) >= 0) ? "passed" : "failed");	\
} while (0)


void
signal_cb(int signum, void *arg)
{
	SIGNAL_RESULT = (signum == SIGUSR1) ? 0 : 1;
}

static void
test_signals()
{
 	struct watch *w;

	test ((w = watch_signal(SIGUSR1, signal_cb, NULL)));
	test (kill(getpid(), SIGUSR1));
}

void
fd_cb(int fd, int evt, void *arg)
{
	FD_RESULT = (evt & PN_READ) ? 0 : 1;
}

static void
test_fd()
{
 	struct watch *w;
	int fildes[2];

	printf("fd tests\n");
	test (pipe(fildes));
	test ((w = watch_fd(fildes[0], fd_cb, NULL)));
	if (write(fildes[1], "a", 1) != 1)
		err(1, "write(2)");
}

void
timer_cb(void *arg)
{
	TIMER_RESULT = 0;
}

static void
test_timer()
{
 	struct watch *w;

	test ((w = watch_timer(1, timer_cb, NULL)));
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
	sleep(5);	/*XXX-FIXME*/
	printf ("fd: %d\n", FD_RESULT);
	printf ("timer: %d\n", TIMER_RESULT);
	printf ("signal: %d\n", SIGNAL_RESULT);

	if ( FD_RESULT || TIMER_RESULT || SIGNAL_RESULT ) 
		errx(1, "one or more test(s) failed");
	exit(0);
}
