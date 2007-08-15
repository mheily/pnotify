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
event_cmp(struct pnotify_event *ev, int wd, int mask, const char *name)
{
	int i = (ev->watch->wd == wd) &&
		(ev->mask == mask);
		
	if (name) {
		i = i && (strcmp(ev->name, name) == 0);
	}

	if (!i) {
		printf(" *ERROR * mismatch: expecting (wd:mask:name) of '%d:%d:%s' but got '%d:%d:%s'\n",
			  wd, mask, name,
			  ev->watch->wd, ev->mask, ev->name);
		pnotify_print_event(ev);
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
	struct pnotify_event    evt;

	printf("signal tests\n");
	test (pnotify_trap_signal(SIGUSR1));
	test (kill(getpid(), SIGUSR1));
	test (pnotify_get_event(&evt, ctx));
	if (!event_cmp(&evt, SIGUSR1, PN_SIGNAL, NULL)) 
		err(1, "unexpected event value");
	printf("signal tests complete\n");
}

static void
test_fd()
{
	struct pnotify_event evt;
	int wd, fildes[2];

	printf("fd tests\n");
	test (pipe(fildes));
	test ((wd = pnotify_watch_fd(fildes[0], PN_READ)));
	if (write(fildes[1], "a", 1) != 1)
		err(1, "write(2)");
	test (pnotify_get_event(&evt, ctx));
	if (!event_cmp(&evt, wd, PN_READ, NULL)) 
		err(1, "unexpected event value");
	printf("fd tests complete\n");
}

static void
test_timer()
{
	struct pnotify_event evt;
	int wd;

	printf("timer tests\n");
	test ((wd = pnotify_set_timer(1, PN_DEFAULT)));
	test (pnotify_get_event(&evt, ctx));
	printf("timer tests complete\n");
}

static int
test_callback(int signum)
{
	printf("signum %d\n");
	return 0;
}

static void
test_dispatch()
{
	test (pnotify_set_callback(pnotify_trap_signal(SIGUSR2), test_callback));
	test (pnotify_dispatch());
}

int
main(int argc, char **argv)
{
	struct pnotify_event    evt;
	int wd;

	/* Create a test directory */
	(void) system("rm -rf .check");
	if (system("mkdir .check") < 0)
		err(1, "mkdir failed");
	if (system("mkdir .check/dir") < 0)
		err(1, "mkdir failed");

	/* Initialize the queue */
	test(ctx = pnotify_init());

	test_signals();
	test_fd();
	test_timer();
	//FIXME:test_dispatch();

	/* Watch for events in the test directory */
	test((wd = pnotify_watch_vnode(".check", PN_CREATE | PN_DELETE | PN_MODIFY))); 

	/* Create a new file */
	test (system("touch .check/foo"));

	/* Read the event */
	test (pnotify_get_event(&evt, ctx)); 
	if (!event_cmp(&evt, 1, PN_CREATE, "foo")) 
		err(1, "unexpected event value");

	/* Create a new file #2 */
	test (system("touch .check/bar"));

	/* Read the event */
	test (pnotify_get_event(&evt, ctx));
	if (!event_cmp(&evt, 1, PN_CREATE, "bar")) 
		err(1, "unexpected event value");

	/* Delete the new file */
	test (system("rm .check/foo"));

	/* Read the delete event */
	test (pnotify_get_event(&evt, ctx));
	if (!event_cmp(&evt, 1, PN_DELETE, "foo")) 
		err(1, "unexpected event value");

	/* Modify file #2 */
	test (system("echo hi >> .check/bar"));

	/* Read the modify event */
	test (pnotify_get_event(&evt, ctx));
	if (!event_cmp(&evt, 1, PN_MODIFY, "bar")) 
		err(1, "unexpected event value");

	/* Remove the watch */
	test (pnotify_rm_watch(ctx, wd));
		
#if DEADWOOD
	if ((fd = inotify_init()) < 0) 
		err(1, "%s", "inotify_init()");

	if ((wd = inotify_add_watch(fd, ".check", IN_ALL_EVENTS)) <= 0) 
		err(1, "%s - %d", "inotify_add_watch()", wd);

	if ((wd = inotify_rm_watch(fd, wd)) < 0) 
		err(1, "%s", "inotify_rm_watch()");

	if ((wd = inotify_rm_watch(fd, 666)) == 0) 
		err(1, "%s", "inotify_rm_watch() - false positive");

	/* Add it back so we can monitor events */
	if ((wd = inotify_add_watch(fd, ".check", IN_ALL_EVENTS)) <= 0) 
		err(1, "%s - %d", "inotify_add_watch()", wd);

	printf("touching foo\n");
	if (system("touch .check/foo") < 0)
		err(1, "touch failed");
	get_event(fd, &evt, &name, sizeof(name));

#if TODO
	if ((wd = inotify_add_watch(fd, ".check/foo", IN_ALL_EVENTS)) <= 0) 
		err(1, "%s - %d", "inotify_add_watch()", wd);

	printf("touching bar\n");
	if (system("touch .check/bar") < 0)
		err(1, "touch failed");

	printf("unlinking foo\n");
	if (system("rm .check/foo") < 0)
		err(1, "rm failed");

	printf("unlinking bar\n");
	if (system("rm .check/bar") < 0)
		err(1, "rm failed");

#endif
#endif

	printf("all tests passed.\n");
	exit(EXIT_SUCCESS);
}
