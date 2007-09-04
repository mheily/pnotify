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
	test (pnotify_trap_signal(SIGUSR1, NULL));
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
	test ((wd = pnotify_watch_fd(fildes[0], PN_READ, NULL)));
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
	test ((wd = pnotify_set_timer(1, PN_ONESHOT, NULL)));
	test (pnotify_get_event(&evt, ctx));
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
	test (pnotify_set_timer(1, PN_DEFAULT, CB_ENCODE(test_callback, 0)));
	test (pnotify_dispatch());
}

static void
test_vnode()
{
	int wd;
	struct pnotify_event    evt;

	/* Watch for events in the test directory */
	test((wd = pnotify_watch_vnode(".check", PN_CREATE | PN_DELETE | PN_MODIFY, NULL))); 

	/* Create a new file */
	test (system("touch .check/foo"));

	/* Read the event */
	test (pnotify_get_event(&evt, ctx)); 
	if (!event_cmp(&evt, wd, PN_CREATE, "foo")) 
		err(1, "unexpected event value");

	/* Create a new file #2 */
	test (system("touch .check/bar"));

	/* Read the event */
	test (pnotify_get_event(&evt, ctx));
	if (!event_cmp(&evt, wd, PN_CREATE, "bar")) 
		err(1, "unexpected event value");

	/* Delete the new file */
	test (system("rm .check/foo"));

	/* Read the delete event */
	test (pnotify_get_event(&evt, ctx));
	if (!event_cmp(&evt, wd, PN_DELETE, "foo")) 
		err(1, "unexpected event value");

	/* Modify file #2 */
	test (system("echo hi >> .check/bar"));

	/* Read the modify event */
	test (pnotify_get_event(&evt, ctx));
	if (!event_cmp(&evt, wd, PN_MODIFY, "bar")) 
		err(1, "unexpected event value");

	/* Remove the watch */
	test (pnotify_rm_watch(wd));
}

void
pn_func_dump2(struct pn_callback *f)
{
	(void) fprintf(stderr,
		"function dump: sym=%p argc=%d argv0=%p argt0=%d\n",
		f->symbol, f->argc, f->argv[0], f->argt[0]
	 	);
}

int test_cb(int x, long y, char *z)
{
	if (strcmp(z, "hello") != 0)
		errx(1, "invalid arg");
	if (x != y)
		errx(1, "invalid arg1");
	return 0;
}

void
test_function()
{
	struct pn_callback fn;
	struct pn_callback *test;
	struct pnotify_event    evt;
	int wd;

	test = CB_ENCODE(test_cb, 3, 1, 1, "hello");
	//pn_func_dump2(test);
	test ((wd = pnotify_call_function(test, NULL)));
	test (pnotify_get_event(&evt, ctx));
	if (!event_cmp(&evt, wd, PN_RETURN, NULL)) 
		err(1, "unexpected event value");
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

	test_vnode();
	test_function();
	test_fd();
	test_signals();
	test_timer();
	test_dispatch(); 
}
