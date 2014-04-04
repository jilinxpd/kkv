#
# Makefile for the In-Kernel Key/Value Store.
#

obj-m += kkv.o

kkv-y +=  slab2.o item3.o itemx2.o hash.o engine.o file.o inode.o fs.o module.o
