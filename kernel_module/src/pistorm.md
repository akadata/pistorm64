## pistorm.ko overview

### What it is
`pistorm.ko` is the kernel backend for PiStorm. It:
- Owns the Pi GPIO lines and GPCLK for the CPLD bus engine.
- Exposes a character device `/dev/pistorm` (misc, major 10 minor 262).
- Presents a small set of ioctls for bus operations and control.
- Provides a batch interface so user space can issue multiple bus ops per syscall.

### Interfaces (UAPI)
- UAPI header: `include/uapi/linux/pistorm.h`
- Ioctls:
  - `PISTORM_IOC_SETUP` — initialize protocol/lines.
  - `PISTORM_IOC_RESET_SM` — reset CPLD state machine.
  - `PISTORM_IOC_PULSE_RESET` — pulse reset line.
  - `PISTORM_IOC_GET_PINS` — snapshot GPIO GPLEV0/1.
  - `PISTORM_IOC_BUSOP` — single bus op (width 8/16/32, read/write, optional status flag).
  - `PISTORM_IOC_BATCH` — array of bus ops processed in one ioctl.
- File ops: `unlocked_ioctl` (and `compat_ioctl`) with `noop_llseek`; open is trivial, no mmap.

### How it drives hardware
- Uses pinctrl/DT to map GPIO/clock resources via `of_iomap`; refuses to proceed if mappings fail.
- Requests all PiStorm pins (0–23) and configures them as needed for bus cycles.
- GPCLK is enabled at probe and left running while the module is loaded.
- Bus cycles:
  - Read8/16 and Write8/16 are issued by toggling GPIO lines; 32-bit reads/writes are split into two 16-bit cycles.
  - Status read/write goes through the status register (STATUS_BIT_INIT/RESET).
  - CPLD state machine reset via `ps_reset_sm`, reset pulse via `ps_pulse_reset`.

### Batching
- `PISTORM_IOC_BATCH` copies a user-provided array of `pistorm_busop` into kernel memory and executes them sequentially under the device mutex.
- On read ops, results are written back into the array and copied to user space on success.

### Logging / debug
- `pr_debug` lines are emitted for BUSOP/BATCH ioctls (is_read/width/addr/flags, batch count/ptr). Enable dynamic debug or set `dyndbg` to see them.
- The module itself is otherwise quiet unless errors occur (copy faults, invalid ioctl).

### Intended limits / current guardrails
- Batch count is capped (`PISTORM_MAX_BATCH_OPS` in UAPI).
- All ioctls are serialized by a device-level mutex to avoid concurrent bit-banging.
- No mmap path; all access is via ioctls.
- No timeout yet on bus cycles: a hung CPLD/bus can block an ioctl. (Planned: add short timeout and return `-ETIMEDOUT` to avoid userland deadlock.)
- No hard resource arbitration: module assumes exclusive ownership of the listed GPIOs/GPCLK.

### Why this structure
- Keep all raw GPIO/clock touching in kernel space, so user space does not need `/dev/mem` or CAP_SYS_RAWIO.
- Minimal, stable UAPI: single/batch bus ops plus basic control; can be extended if needed (e.g., timeouts, more status).
- Batch support reduces syscall overhead for tight bus loops.

### Usage expectations
- Load module (`modprobe pistorm` after install).
- User space opens `/dev/pistorm` and issues BUSOP or BATCH ioctls via the kmod-aware backend (`ps_protocol_kmod.c`).
- Only one emulator instance should own the device at a time.

### Future TODOs
- Add timeouts on bus read/write paths to prevent permanent ioctl blocks.
- Optional stricter validation: refuse unmapped/illegal addresses at kmod level if needed.
- Surface CPLD/clock configuration info via debugfs for easier inspection.
