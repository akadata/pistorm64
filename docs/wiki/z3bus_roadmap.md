# z3bus Roadmap (WIP)

This document captures the agreed plan for a **PiStorm64 Z2/Z3 virtual bus** on Linux.
It is **not** a reuse of the m68k Zorro core; it is a new ARM‑side bus that mirrors
Zorro concepts and allows us to migrate emulator‑side devices into kernel modules.

## Agreement Summary

- Linux Zorro core exists in source but is **m68k‑only** and not built on Pi kernels.
- We will **not** try to enable CONFIG_ZORRO on ARM.
- We will implement a **new virtual bus** (`z3bus.ko`) that is conceptually aligned
  with Zorro/Z3, but powered by PiStorm’s ps_protocol kernel backend.
- The emulator will talk to `z3bus.ko` to service Z2/Z3 device cycles and autoconfig.
- Over time, device logic migrates from emulator C into Linux drivers bound to z3bus.

## Reference Materials

- Mainline Linux Zorro headers (shape and ABI only):
  - `include/uapi/linux/zorro.h`
  - `include/linux/zorro.h`
  - `drivers/zorro/*`
  - `arch/m68k/include/asm/zorro.h`

- ZZ9000 Linux‑m68k driver (device side patterns):
  - `../zz9000-linux-m68k/*.c`

These are **references**, not build targets on ARM.

## Goal

Provide an ARM‑side Z2/Z3 bus that:

- Exposes Zorro‑style device IDs and resource windows.
- Allows Linux drivers to register/probe devices (rtg, piscsi, mjpeg, net, etc.).
- Uses `pistorm.ko` as the raw bus engine.
- Lets the emulator gradually shed device‑side logic.

## Proposed Architecture

```
Musashi + emulator
    |
    | (Z2/Z3 config + memory cycles)
    v
z3bus.ko  <---->  pistorm.ko (GPIO/GPCLK)
    |
    +-- rtg.ko
    +-- piscsi.ko
    +-- net.ko
    +-- mjpeg.ko
```

- `pistorm.ko`: raw bus + timing engine.
- `z3bus.ko`: virtual Z2/Z3 bus + device registry.
- Device drivers: consume z3bus resources, map windows, expose Linux subsystems.

## Key Concepts (Zorro‑aligned)

- **Device IDs**: vendor/product, similar to Zorro ID scheme.
- **Resources**: address ranges (Z2/Z3), sizes, flags.
- **Probe/remove**: driver registration model similar to Zorro.
- **Sysfs**: optional, mirror Zorro bus layout for visibility.

## Why Not Use Linux Zorro Core Directly?

- CONFIG_ZORRO is tied to ARCH_M68K/Amiga.
- ARM Pi kernel does not include the Zorro bus.
- Zorro core expects real Amiga hardware, not ps_protocol.

## Migration Plan

### Phase 0 — Discovery
- Capture Zorro IDs and driver patterns from Linux m68k.
- Review ZZ9000 driver structure and resource use.

### Phase 1 — Bus Skeleton (now)
- Create `z3bus.ko` with device registry and static device table.
- Minimal driver registration API (probe/remove callbacks).
- Dummy resources (no real memory yet).

### Phase 2 — Bus ↔ PiStorm Bridge
- Connect z3bus to `pistorm.ko` for read/write cycles.
- Add resource windows for Z2/Z3 address space.
- Implement bounds checks and logging for safety.

### Phase 3 — First Device Migration
- Migrate one device (likely PiSCSI or RTG) out of emulator into a Linux driver.
- Confirm autoconfig + IO works through z3bus.

### Phase 4 — Expand and Stabilize
- Move more devices to z3bus (A314, net, mjpeg, etc.).
- Add shared memory or DMA buffers where needed.
- Introduce a stable userspace control API.

### Phase 5 — Optional Extensions
- Remote Z3 devices (networked cards).
- Userspace card emulation plugged into z3bus.

## Compatibility Notes

- Z2 must remain within 0x00200000–0x00A00000.
- Z3 lives above 16 MB (32‑bit address space).
- Zorro‑style IDs should stay stable once published.

## Checkpoints

- [x] z3bus.ko skeleton builds and loads (no ps_protocol yet).
- [ ] Emulator can enumerate z3bus devices.
- [ ] Demo autoconfig device served via z3bus.
- [ ] PiSCSI migrated to z3bus driver.
- [ ] RTG migrated to z3bus driver.

## Open Questions

- Best Z3 address window layout for each card class.
- DMA safety and masking rules for device drivers.
- How to represent autoconfig ROMs in the bus layer.
- Whether to mirror Linux’s zorro_device_id matching exactly.

## Status

**WIP — roadmap only.** No production z3bus implementation exists yet.
