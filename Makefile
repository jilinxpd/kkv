#
## Makefile for the In-Kernel Key/Value Store.
#

DEBUG_KKV_FS = y
DEBUG_KKV_ENGINE = n
DEBUG_KKV_SLAB = n
DEBUG_KKV_STAT = n

ifeq (${DEBUG_KKV_FS},y)
  EXTRA_CFLAGS += -DDEBUG_KKV_FS
endif

ifeq (${DEBUG_KKV_ENGINE},y)
  EXTRA_CFLAGS += -DDEBUG_KKV_ENGINE
endif

ifeq (${DEBUG_KKV_SLAB},y)
 EXTRA_CFLAGS += -DDEBUG_KKV_SLAB
endif

ifeq (${DEBUG_KKV_STAT},y)
 EXTRA_CFLAGS += -DDEBUG_KKV_STAT
endif

KERNELDIR = /lib/modules/$(shell uname -r)/build
MODULEDIR= $(shell pwd)

obj-m += kkv.o

kkv-y +=  slab.o item.o itemx.o hash.o engine.o file.o inode.o fs.o module.o

all:
	${MAKE} -C ${KERNELDIR} M=${MODULEDIR} modules

clean:
	rm -rf *.o *.ko *.symvers *.order *.mod.c .*.cmd .tmp_versions

