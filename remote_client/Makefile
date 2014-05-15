#
# Makefile for the In-Kernel Key/Value Store.
#

CC = gcc
CFLAGS_KKV = -DVERBOSE_KKV_CLIENT

.PHONY: all
all: kkv-net

kkv-net: kkv-net.c libkkv-net.o
	${CC} ${CFLAGS_KKV} -o $@ $^

.PHONY: clean
clean:
	rm -f *.o kkv-net
