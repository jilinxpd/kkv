#
## Makefile for the In-Kernel Key/Value Store.
#
#

CC = gcc
LD_MEMCACHED = -lmemcached -lpthread

.PHONY: all
all: kkv memcached

.PHONY: kkv
kkv: kkv-client kkv-random kkv-rush kkv-shuffle

.PHONY: memcached
memcached: memcached-random memcached-rush memcached-shuffle

kkv-client: kkv-client.o libkkv.o
	${CC} -o $@ $^

kkv-random: kkv-random.o libkkv.o
	${CC} -o $@ $^

kkv-rush: kkv-rush.o libkkv.o
	${CC} -o $@ $^

kkv-shuffle: kkv-shuffle.o libkkv.o
	${CC} -o $@ $^

memcached-random: memcached-random.o
	${CC} ${LD_MEMCACHED} -o $@ $^

memcached-rush: memcached-rush.o
	${CC} ${LD_MEMCACHED} -o $@ $^

memcached-shuffle: memcached-shuffle.o
	${CC} ${LD_MEMCACHED} -o $@ $^

.PHONY: clean
clean:
	rm -f *.o kkv-client kkv-random kkv-rush kkv-shuffle memcached-random memcached-rush memcached-shuffle
