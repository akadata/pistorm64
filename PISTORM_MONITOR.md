# PiStorm Monitor (kmod backend)

`pistorm_monitor` is a small REPL that talks to `/dev/pistorm` via the kernel module. It lets you peek/poke the Amiga bus from the Pi side, either on stdin/stdout or over a local TCP port.

## Build

```sh
# On the Pi (arm64):
make pistorm_monitor PISTORM_KMOD=1
```

> Note: the top-level Makefile uses ARM tuning flags; build on the Pi or override `CPUFLAGS` if you compile elsewhere.

## Run (local REPL)

```sh
sudo ./pistorm_monitor
```

- Uses `/dev/pistorm` by default; override with `--dev /dev/pistorm`.
- Commands are case-insensitive; `help` shows the list.

### Commands

- `r8|r16|r32 <addr> [count]` : read 8/16/32-bit values. `count` defaults to 1.
- `w8|w16|w32 <addr> <value>` : write 8/16/32-bit value.
- `status` : read PiStorm status register (via BUSOP flag).
- `pins` : dump GPIO levels (debug).
- `reset_sm` : reset state machine (IOC_RESET_SM).
- `pulse_reset` : pulse RESET (IOC_PULSE_RESET).
- `help` : show help.
- `quit` / `exit` : close session.

Addresses/values accept decimal or hex (e.g., `0xdff006`).

## Run (TCP)

```sh
sudo ./pistorm_monitor --listen 6666
```

- Listens on `127.0.0.1:6666`; connect with `nc 127.0.0.1 6666`.
- Only one client at a time; every session starts with a prompt and the same commands as the local REPL.

## Notes / Limits

- Requires the pistorm kernel module loaded and `/dev/pistorm` accessible (use `sudo` or adjust permissions).
- No scripting or disassembly—this is a minimal peek/poke tool. For richer automation, wrap it or extend it.
- Reads/writes use the single BUSOP ioctl; batching is not implemented yet.
- When the emulator is running, it listens on `/tmp/pistorm_ctrl.sock`. `pistorm_monitor` will notify it after `reset_sm`/`pulse_reset` so the emulator can log `[MONITOR] …` and re-init the bus.
