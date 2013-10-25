#
# Makefile for the linux ext3301-filesystem routines.
#

obj-m += ext3301.o

ext3301-y := balloc.o dir.o file.o ialloc.o inode.o \
	  ioctl.o namei.o super.o symlink.o

MOD_DIR=/local/comp3301/linux-3.9.4

.PHONY: all
all:
	make -C $(MOD_DIR) M=$(PWD) ARCH=um modules

.PHONY: clean
clean:
	make -C $(MOD_DIR) M=$(PWD) ARCH=um clean
