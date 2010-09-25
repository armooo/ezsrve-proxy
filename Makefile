CC=gcc
CFLAGS=-Wall -std=c99 -D_XOPEN_SOURCE=500

ezsrve_proxy: ezsrve_proxy.o client.o

clean:
	rm -f ezsrve_proxy
	rm -f *.o
