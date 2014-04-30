#
# Makefile for the In-Kernel Key/Value Store.
#

CC = gcc
LD_MEMCACHED = -lmemcached -lpthread
CFLAGS_MEMCACHED = -DVERBOSE_MEMCACHED_CLIENT
CFLAGS_KKV = -DVERBOSE_KKV_CLIENT

.PHONY: all
all: kkv memcached

.PHONY: kkv
kkv: kkv-client kkv-random kkv-rush

.PHONY: memcached
memcached: memcached-random memcached-rush

kkv-client: kkv-client.c libkkv.o
	${CC} ${CFLAGS_KKV} -o $@ $^

kkv-random: kkv-random.c libkkv.o
	${CC} ${CFLAGS_KKV} -o $@ $^

kkv-rush: kkv-rush.c libkkv.o
	${CC} ${CFLAGS_KKV} -o $@ $^

memcached-random: memcached-random.c
	${CC} ${CFLAGS_MEMCACHED} ${LD_MEMCACHED} -o $@ $^

memcached-rush: memcached-rush.c
	${CC} ${CFLAGS_MEMCACHED} ${LD_MEMCACHED} -o $@ $^

.PHONY: clean
clean:
	rm -f *.o kkv-client kkv-random kkv-rush memcached-random memcached-rush 
