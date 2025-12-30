# Pi 5 GPCLK0 bring-up (GPIO4)

PiStorm Rev B CPLD logic expects a high-speed `PI_CLK` on GPIO4. On Raspberry Pi 5 the kernel often blocks direct `/dev/mem` access to the SoC clock block, so the emulator cannot reliably enable GPCLK0 from userspace.

This folder provides:

- A small **device-tree overlay** that routes **GPCLK0 to GPIO4** and requests **200MHz**.
- A tiny **kernel module** that **enables the GP0 clock** (via the Linux clock framework) so the signal actually toggles on the pin.

## Build

Overlay (`.dtbo`):

```sh
dtc -@ -I dts -O dtb -o pistorm-gpclk0.dtbo pistorm-gpclk0-overlay.dts
```

Kernel module:

```sh
make
```

## Install (manual, recommended for testing)

Load the overlay directly from this folder (no `/boot` modifications):

```sh
sudo dtoverlay -r pistorm-gpclk0 2>/dev/null || true
sudo rmmod pistorm_gpclk0 2>/dev/null || true
sudo dtoverlay -d "$(pwd)" pistorm-gpclk0
sudo insmod ./pistorm_gpclk0.ko
```

Optional: set a different output frequency (useful for low-speed verification with `gpiomon`):

```sh
sudo dtoverlay -r pistorm-gpclk0 2>/dev/null || true
sudo dtoverlay -d "$(pwd)" pistorm-gpclk0 freq=1000000
```

Or use the helper script from the repo root:

```sh
./tools/pi5_gpclk0_enable.sh --freq 1000000
```

High frequencies

- Some Pi 5 kernels/setups appear unstable when requesting high GPCLK0 rates (e.g. 200MHz), likely due to clock reparenting.
- Prefer validating at `freq=1000000` first, then try `freq=50000000` (xosc) before attempting anything higher.
- The helper script refuses `--freq > 50000000` unless you pass `--force-high`.

Verify GPIO4 is GPCLK0 and toggling:

```sh
sudo pinctrl get 4
sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 ./emulator --gpclk-probe
```

If debugfs clk info is available, verify the kernel thinks it’s running:

```sh
sudo mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
sudo sh -c 'test -d /sys/kernel/debug/clk/clk_gp0 && { cat /sys/kernel/debug/clk/clk_gp0/clk_parent; cat /sys/kernel/debug/clk/clk_gp0/clk_rate; cat /sys/kernel/debug/clk/clk_gp0/clk_enable_count 2>/dev/null || true; }'
```

If you see `gpio4_funcsel=0` and `clk_transitions` is non-zero, you have an active clock output on GPIO4.

## Persist across reboot (optional)

Copy the overlay into firmware overlays and apply it via `/boot/firmware/config.txt`:

```sh
sudo cp pistorm-gpclk0.dtbo /boot/firmware/overlays/
echo "dtoverlay=pistorm-gpclk0" | sudo tee -a /boot/firmware/config.txt
```

Then load the module at boot using your preferred mechanism (e.g. `/etc/modules-load.d/`), or just `insmod` it after boot while testing.

## Remove

```sh
sudo dtoverlay -r pistorm-gpclk0 || true
sudo rmmod pistorm_gpclk0 || true
```
