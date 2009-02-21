all:
	gcc -Wall -Werror -g -O2 -o test epoll.c test.c

dist: clean
	cd .. ; tar cvf kqueue-epoll-`date +%y%m%d`.tgz kqueue-epoll

clean:
	rm -f *~ test
