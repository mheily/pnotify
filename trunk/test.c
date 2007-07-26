#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "pnotify.h"

/* Compare a pnotify_event against an expected set of values */
int
event_cmp(struct pnotify_event *ev, int wd, int mask, const char *name)
{
	int i = (ev->wd == wd) &&
		(ev->mask == mask) &&
		  (strcmp(ev->name, name) == 0);
	if (!i) {
		printf(" *ERROR * mismatch: expecting '%d:%d:%s' but got '%d:%d:%s'\n",
			  wd, mask, name,
			  ev->wd, ev->mask, ev->name);
	}
	return i;
}

int
main(int argc, char **argv)
{
	struct pnotify_event    evt;
	struct pnotify_cb ctl;
	int wd;

	/* Create a test directory */
	(void) system("rm -rf .check");
	if (system("mkdir .check") < 0)
		err(1, "mkdir failed");
	if (system("mkdir .check/dir") < 0)
		err(1, "mkdir failed");

	/* Initialize the control structure */
	if (pnotify_init(&ctl) != 0) 
		err(1, "%s", "pnotify_new()");

	/* Watch for events in the test directory */
	if ((wd = pnotify_add_watch(&ctl, ".check", PN_ALL_EVENTS)) < 0) 
		err(1, "%s", "inotify_add_watch()");

	/* Create a new file */
	if (system("touch .check/foo") < 0)
		err(1, "touch failed");

	/* Read the event */
	if (pnotify_get_event(&evt, &ctl) != 0)
		err(1, "pnotify_get_event()");
	if (!event_cmp(&evt, 1, PN_CREATE, "foo")) 
		err(1, "unexpected event value");

	/* Create a new file #2 */
	if (system("touch .check/bar") < 0)
		err(1, "touch failed");

	/* Read the event */
	if (pnotify_get_event(&evt, &ctl) != 0)
		err(1, "pnotify_get_event()");
	if (!event_cmp(&evt, 1, PN_CREATE, "bar")) 
		err(1, "unexpected event value");

	/* Delete the new file */
	if (system("rm .check/foo") < 0)
		err(1, "rm failed");

	/* Read the delete event */
	if (pnotify_get_event(&evt, &ctl) != 0)
		err(1, "pnotify_get_event()");
	if (!event_cmp(&evt, 1, PN_DELETE, "foo")) 
		err(1, "unexpected event value");

	/* Remove the watch */
	if (pnotify_rm_watch(&ctl, wd) < 0)
		err(1, "pnotify_rm_watch()");
		
	/* Destroy the control structure */
	if (pnotify_free(&ctl) != 0) 
		err(1, "%s", "pnotify_free()");
	
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
