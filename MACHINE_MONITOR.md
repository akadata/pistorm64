## PiStorm control/monitor socket

- Control socket: `/tmp/pistorm_ctrl.sock`
- Tool: `pistorm_monitor` (uses /dev/pistorm)
- Emulator logs when active: `[INFO] [MONITOR] control socket listening at /tmp/pistorm_ctrl.sock`

### Commands
- `setup` — re-run setup state machine.
- `pulse_reset` — pulse CPLD reset line.
- `reset_sm` — reset the CPLD state machine.

### Notes
- Commands execute via kmod; emulator does not yet auto-reload config after reset_sm/pulse_reset (full restart still needed).
- Build: `make PISTORM_KMOD=1 pistorm_monitor` (installed to /opt/pistorm64 by `make install`).
