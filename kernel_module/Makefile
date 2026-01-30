# PiStorm kernel module

ifeq ($(MODULE),pistorm)
obj-m := pistorm.o
pistorm-objs := src/pistorm.o
else ifeq ($(MODULE),z3bus)
obj-m := z3bus.o
z3bus-objs := src/z3bus.o
else
obj-m += pistorm.o
obj-m += z3bus.o
pistorm-objs := src/pistorm.o
z3bus-objs := src/z3bus.o
endif

ccflags-y += -I$(PWD)/../include/uapi

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

ccflags-y += -I$(M)/../include -I$(M)/../include/uapi
ccflags-y += -DPISTORM64_GIT=\"$(shell git rev-parse --short HEAD 2>/dev/null || echo nogit)\"

all: module

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

module_pistorm:
	$(MAKE) -C $(KDIR) M=$(PWD) MODULE=pistorm modules

module_z3bus:
	$(MAKE) -C $(KDIR) M=$(PWD) MODULE=z3bus modules

install:
	sudo install -D -m 644 pistorm.ko /lib/modules/$(shell uname -r)/extra/pistorm.ko
	sudo install -D -m 644 z3bus.ko /lib/modules/$(shell uname -r)/extra/z3bus.ko
	sudo depmod -a

install_pistorm:
	sudo install -D -m 644 pistorm.ko /lib/modules/$(shell uname -r)/extra/pistorm.ko
	sudo depmod -a

install_z3bus:
	sudo install -D -m 644 z3bus.ko /lib/modules/$(shell uname -r)/extra/z3bus.ko
	sudo depmod -a

load: module
	sudo insmod ./pistorm.ko || sudo modprobe pistorm
	sudo chmod 666 /dev/pistorm || true

unload:
	sudo rmmod pistorm 2>/dev/null || true

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all module module_pistorm module_z3bus install install_pistorm install_z3bus load unload clean
