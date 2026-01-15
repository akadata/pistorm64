obj-m += pistorm_kernel_module.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	sudo install -D -m 0644 pistorm_kernel_module.ko /lib/modules/$(shell uname -r)/extra/pistorm_kernel_module.ko
	sudo depmod -a

load:
	sudo insmod ./pistorm_kernel_module.ko

unload:
	sudo rmmod pistorm_kernel_module 2>/dev/null || true

# Build the smoke test
smoke_test: pistorm_smoke.c
	gcc -o pistorm_smoke_test pistorm_smoke.c -I.

# Copy UAPI header to a location accessible by userspace builds
copy_uapi:
	cp include/uapi/linux/pistorm.h .

.PONY: all clean install load unload smoke_test copy_uapi