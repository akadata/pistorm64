# Main Makefile for PiStorm kernel module

obj-m += pistorm_kernel_module.o

# Source files
pistorm_kernel_module-objs := src/pistorm_kernel_module.o

ccflags-y += -I$(src)/include -I$(src)/include/uapi

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Targets
all: module

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install:
	sudo install -D -m 644 pistorm_kernel_module.ko /lib/modules/$(shell uname -r)/extra/pistorm_kernel_module.ko
	sudo depmod -a

load:
	sudo insmod ./pistorm_kernel_module.ko
	sudo chmod 666 /dev/pistorm0

unload:
	sudo rmmod pistorm_kernel_module 2>/dev/null || true

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f pistorm_smoke_test

# Build smoke test
smoke_test: tools/pistorm_smoke.c
	gcc -o pistorm_smoke_test tools/pistorm_smoke.c

# Build with kernel module support
build_with_kmod:
	make PISTORM_KMOD=1

# Test the module
test: load
	@echo "Testing module functionality..."
	@if [ -e /dev/pistorm0 ]; then \
		echo "✓ Device node /dev/pistorm0 exists"; \
	else \
		echo "✗ Device node /dev/pistorm0 does not exist"; \
	fi
	@echo "Module ready for use with emulator (make PISTORM_KMOD=1)"

.PHONY: all module install load unload clean smoke_test build_with_kmod test
