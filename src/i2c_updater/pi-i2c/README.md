# pi-i2c CPLD/FPGA Updater (I²C)

This tool programs the PiStorm CPLD/FPGA configuration over the Raspberry Pi I²C bus.

It is **not** a drop-in replacement for OpenOCD/OpenICD in day-to-day use: it requires the Pi I²C
interface to be enabled (device-tree overlay) and the correct `/dev/i2c-*` node to exist.
OpenOCD/OpenICD typically “just works” with fewer moving parts.

## Why this exists

- Removes dependency on OpenOCD/OpenICD for boards wired to support I²C programming.
- Useful for scripted updates where the I²C bus is already enabled and stable.

## Practical reality / trade-offs

**Pros**
- No OpenOCD/OpenICD install.
- Simple single-purpose binary.

**Cons**
- Requires Raspberry Pi I²C enabled (DT overlay / config change).
- Often requires a reboot after enabling I²C.
- Requires correct bus + slave address (varies by Pi model/board wiring).
- More fragile when bus is missing or permissions are wrong.

For a “works every time” workflow, OpenOCD/OpenICD is still the baseline.

## Prerequisites (Raspberry Pi)

1. Enable I²C (creates `/dev/i2c-0`, `/dev/i2c-1`, etc.)
   - This is done via device-tree overlay / firmware config.
   - After enabling, a reboot is commonly required.

2. Ensure permissions allow access to the device:
   - Run as root, or add the user to the `i2c` group (platform-dependent).

3. Confirm the bus exists:
   ```sh
   ls -l /dev/i2c-*
