#
# Makefile for the In-Kernel Key/Value Store.
#

obj-m += kkv.o

kkv-y +=  slab.o item.o itemx.o hash.o engine.o file.o inode.o fs.o module.o
