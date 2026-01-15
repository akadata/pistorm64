#!/bin/bash
# build_kmod.sh - Build script for PiStorm kernel module

set -e  # Exit on any error

echo "Building PiStorm kernel module..."

# Create necessary directory structure
mkdir -p ~/pistorm-kmod/include/uapi/linux

# Copy the UAPI header to the module directory
cp ~/pistorm/include/uapi/linux/pistorm.h ~/pistorm-kmod/include/uapi/linux/

# Copy the minimal kernel module to the build directory
cp ~/pistorm/pistorm_kernel_module_minimal.c ~/pistorm-kmod/pistorm_kernel_module.c

# Create a proper Makefile for the kernel module
cat > ~/pistorm-kmod/Makefile << 'EOF'
obj-m += pistorm_kernel_module.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

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
	gcc -o pistorm_smoke_test pistorm_smoke.c

.PHONY: all clean install load unload smoke_test
EOF

# Copy the smoke test
cp ~/pistorm/tools/pistorm_smoke.c ~/pistorm-kmod/

# Change to the module directory and build
cd ~/pistorm-kmod
make clean
make

echo "Kernel module built successfully!"

# Try to load the module
echo "Loading module..."
make load

# Check if it loaded
if lsmod | grep pistorm_kernel_module; then
    echo "Module loaded successfully!"
    echo "Device should be available at /dev/pistorm0"
    ls -la /dev/pistorm0 2>/dev/null || echo "Device node not found - may need udev rules"
else
    echo "Module failed to load"
    dmesg | tail -20
fi