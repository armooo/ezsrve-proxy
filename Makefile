CC=gcc
CFLAGS=-Wall -std=c99 -D_BSD_SOURCE=1 -D_XOPEN_SOURCE=500

ezsrve_proxy: ezsrve_proxy.o client.o log.o

clean:
	rm -f ezsrve_proxy
	rm -f *.o
