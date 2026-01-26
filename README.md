# PiStorm64 Kernel Driver (pistorm)

This repository contains the Linux kernel driver for the PiStorm CPLD bus engine.
It provides a misc character device at /dev/pistorm and a stable UAPI under:

include/uapi/linux/pistorm.h

## Scope:

Kernel: GPIO/clock/pinctrl backend only

Userspace: CPU/emulator stays in userspace (not included here)

## Directory layout (kernel-style):

- `drivers/misc/pistorm/` driver sources + Kconfig/Makefile
- `include/uapi/linux/pistorm.h` UAPI ABI (ioctl interface)
- `Documentation/devicetree/bindings/misc/` DT binding
- `Documentation/ABI/testing/dev-pistorm` ABI documentation
- `overlays/` example Raspberry Pi overlays (DTS)

## Device Tree:
The driver is gpiod-first. Required GPIOs are:

- txn-in-progress-gpios
- ipl-zero-gpios
- a0-gpios
- a1-gpios
- reset-gpios
- rd-gpios
- wr-gpios
- data-gpios (16 entries)

Optional:

- clocks / clock-names (gpclk)
- pinctrl-* states (default, bus-out, bus-in)
- reg / reg-names (direct GPIO register mapping may be used as an optimisation path, not required)

## Build (out-of-tree, against an installed kernel build tree):

Ensure kernel headers/build dir exist on the target Pi:
/lib/modules/$(uname -r)/build

Build:
```
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

Load:
```
sudo insmod drivers/misc/pistorm/pistorm.ko
```

Verify:
```
ls -l /dev/pistorm
dmesg | tail -n 100
```

## Testing targets:

- Pi Zero 2 W (Pi 3-class)
- Pi 4 Model B (Pi 4-class)
- Pi 5 (RP1 Pi 5-class)

## Upstreaming:
This repo is structured to mirror the Linux kernel source tree, so the contents can be submitted as a patchset after validation across the target platforms.
