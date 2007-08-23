#		$Id: $		

#
# Copyright (c) 2007 Mark Heily <devel@heily.com>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

lib_LTLIBRARIES=	libpnotify.la
include_HEADERS=	pnotify.h
noinst_HEADERS=		queue.h pnotify-internal.h thread.h
dist_man3_MANS=		pnotify.3
EXTRA_DIST=		index.html Doxyfile nio.h

libpnotify_la_SOURCES=	pnotify.c signal.c timer.c \
                        bsd.c linux.c
libpnotify_la_CFLAGS=	-O2 -g -Wall -D_REENTRANT
libpnotify_la_LDFLAGS=  -lpthread
# fixme-  linux requires -lrt

#
# Unit testing program
# 
TESTS=			test
check_PROGRAMS=		test
test_SOURCES=		test.c
test_LDADD=		.libs/libpnotify.a -lpthread
test_CFLAGS=		-DPNOTIFY_DEBUG=1 -D_REENTRANT

# Preview the manpage in the current terminal window
preview-man:
	nroff -Tascii -mandoc pnotify.3 | less

# The 'doc' target generates additional documentation
doc:
	groff -Thtml -mandoc pnotify.3 > manpage.html
	groff -Tps -mandoc pnotify.3 > manpage.ps
	ps2pdf manpage.ps manpage.pdf
	lynx -dump index.html > README
	doxygen

# The 'sync' target rebuilds the webpage and uploads it to the public server

sync:	
	scp -r index.html manpage.html manpage.ps manpage.pdf doxygen/ \
	  heily.com:~/public_html/pnotify

# Ensure that the documentation is regenerated when building a source tarball
dist-hook:	doc
