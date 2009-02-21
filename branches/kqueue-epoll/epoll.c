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

#if ! defined(__linux__)

#include <errno.h>

#include "epoll.h"

int
epoll_create(int size)
{
    return kqueue();
}

int
epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    struct kevent kev;

    kev.ident = fd;
    kev.filter = (event->events & EPOLLIN) ? EVFILT_READ : EVFILT_WRITE;
    kev.flags = op;
    kev.fflags = 0;
    kev.data = 0;
    kev.udata = event->data.ptr;

    if (event->events & EPOLLET)
        kev.flags |= EV_CLEAR;

    return kevent(epfd, &kev, 1, NULL, 0, NULL);
}

int
epoll_wait(int epfd, struct epoll_event * events, int maxevents, int timeout)
{
    struct kevent kev;
    struct timespec tv;
    struct timespec *tvp = &tv;
    int nevents;

    /* Convert the timeout from milliseconds to seconds. */
    tv.tv_nsec = 0;
    if (timeout < 0) 
        tvp = NULL;
    else if (timeout == 0) 
        tv.tv_sec = 0;
    else if (timeout < 1000) 
        tv.tv_sec = 1;
    else 
        tv.tv_sec = timeout / 1000;

    /* TODO - Support returning more than one event */
    if (maxevents > 1)
        return -EINVAL;

    nevents = kevent(epfd, NULL, 0, &kev, 1, tvp);

    /* FIXME: error handling */
    if (kev.flags & EV_ERROR) {
        events->events = EPOLLERR;
    } else {
        events->events = (kev.filter == EVFILT_READ) ? EPOLLIN : EPOLLOUT;
        if (kev.flags & EV_EOF) 
            events->events |= EPOLLHUP;
    }
    events->data.ptr = kev.udata;

    return (nevents);
}

#endif  /* ! defined(__linux__) */ 
