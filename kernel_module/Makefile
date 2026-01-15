# PiStorm kernel module

obj-m += pistorm.o
pistorm-objs := src/pistorm.o

ccflags-y += -I$(src)/include -I$(src)/include/uapi -I$(PWD)/../include -I$(PWD)/../include/uapi

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all: module

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install:
	sudo install -D -m 644 pistorm.ko /lib/modules/$(shell uname -r)/extra/pistorm.ko
	sudo depmod -a

load: module
	sudo insmod ./pistorm.ko || sudo modprobe pistorm
	sudo chmod 666 /dev/pistorm || true

unload:
	sudo rmmod pistorm 2>/dev/null || true

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all module install load unload clean
