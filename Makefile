all: main
clear:
	rm myhttpd
main: myhttpd.c reqQueue.h
	gcc myhttpd.c -o myhttpd -pthread
