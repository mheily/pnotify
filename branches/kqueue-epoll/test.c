/*              $Id$          */

/*
 * Copyright (c) 2009 Mark Heily <mark@heily.com>
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

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "epoll.h"

int epfd;

void
success(const char *func)
{
    printf("%-70s %s\n", func, "passed");
}

void
test_epoll_create(void)
{
    if ((epfd = epoll_create(100)) < 0)
 	err(1, "epoll_create()");
    success("epoll_create()");
}

void
test_epoll_ctl_add(void)
{
    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0)
 	err(1, "epoll_ctl(EPOLL_CTL_ADD)");

    success("epoll_ctl(EPOLL_CTL_ADD)");
}

void
test_epoll_wait(void)
{
    int nfds;
    struct epoll_event ev;

    /* Infinite wait */
    puts("press <return> to trigger an event (will wait forever)");
    nfds = epoll_wait(epfd, &ev, 1, -1);
    if (nfds < 1)
 	err(1, "epoll_wait()");
    (void) getc(stdin);

    /* Timed wait */
    puts("press <return> within three seconds to trigger an event");
    nfds = epoll_wait(epfd, &ev, 1, 3000);
    if (nfds == 0)
 	errx(1, "epoll_wait() timed out");
    if (nfds < 0)
 	err(1, "epoll_wait()");

    success("epoll_wait()");
}

void
test_epoll_ctl_del(void)
{
    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;

    if (epoll_ctl(epfd, EPOLL_CTL_DEL, STDIN_FILENO, &ev) < 0)
 	err(1, "epoll_ctl(EPOLL_CTL_DEL)");

    success("epoll_ctl(EPOLL_CTL_DEL)");
}
int 
main(int argc, char **argv)
{
    test_epoll_create();
    test_epoll_ctl_add();
    test_epoll_wait();
    test_epoll_ctl_del();

    puts("all tests completed.");
    return (0);
}
