# z3bus Tasks (WIP)

This is a live task list for the Z3 bus migration. Keep it short and practical.

## Phase 1 — Bus Skeleton (now)

- [x] Add z3bus.ko skeleton (device/driver registry, static device table)
- [x] Add build targets for z3bus in top‑level Makefile
- [x] Document roadmap + tasks
- [ ] Add `docs/wiki/Home.md` link to z3bus roadmap + tasks

## Phase 2 — Emulator ↔ Bus Link

- [ ] Define uapi header for z3bus ioctl (read/write + enumerate)
- [ ] Add userspace stub in emulator to open `/dev/z3bus`
- [ ] Add demo autoconfig device backed by z3bus (read‑only ID ROM)
- [ ] Log z3bus reads/writes with card/offset/width

## Phase 3 — First Device Migration (PiSCSI)

- [ ] Decide address window + resources for PiSCSI card
- [ ] Move PiSCSI registers into z3bus driver
- [ ] Route DMA buffer through z3bus mapping
- [ ] Remove emulator‑side PiSCSI logic (except autoconfig)

## Phase 4 — RTG Migration

- [ ] Define RTG card window + registers
- [ ] Migrate RTG framebuffer to z3bus driver
- [ ] Attach DRM/Raylib frontend to RTG kernel buffer

## Phase 5 — Extras

- [ ] Add MJPEG / video card prototype
- [ ] Add net card driver on z3bus
- [ ] Optional: sysfs layout mirroring Zorro

