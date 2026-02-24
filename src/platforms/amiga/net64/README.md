# net64 (PiStorm64 Amiga network device)

`net64` is a new Zorro-II virtual Ethernet card implementation that coexists with the legacy `pi-net` code.

Design goals:
- No hard-coded IP address in device logic.
- Configurable MAC address and link behavior from emulator-side config (`setvar net64_*`).
- SANA-II driver model on the Amiga side so standard `bsdsocket.library` stacks (Roadshow/AmiTCP) can do IPv4/IPv6 TCP/UDP sockets normally.
- Host-side backend through Linux TAP (or loopback fallback for bring-up).

## Host-side architecture

Files:
- `net64_config.c` / `net64_config.h`: parser and defaults for `setvar net64_*`
- `net64_device.c` / `net64_device.h`: TAP I/O, RX queue, stats
- `net64_bus.c` / `net64_bus.h`: register map, DMA copy to/from Amiga memory
- `net64_autoconfig.c` / `net64_autoconfig.h`: Zorro-II autoconfig card registration

Autoconfig and address space:
- Card exposes a 64 KiB register window (`NET64_REGSIZE`) as a Z2 I/O board.
- Accesses are routed through the generic Zorro device handlers.

Register model:
- TX path: write `TX_ADDR`/`TX_LEN`, then command `NET64_CMD_TX_KICK`.
- RX path: write `RX_ADDR`/`RX_LEN`, then command `NET64_CMD_RX_POP`.
- MAC/config: `MAC_LO`/`MAC_HI`, `FEATURES`, then `NET64_CMD_APPLY_CFG`.
- Statistics counters exposed as 64-bit split hi/lo pairs.

## setvar options

- `setvar net64_tap tap0`
- `setvar net64_mac 02:50:12:34:56:78`
- `setvar net64_promisc 0|1`
- `setvar net64_queue_depth <8..1024>`
- `setvar net64_link_mbps <10..100000>`
- `setvar net64_duplex half|full`
- `setvar net64_debug off|on|all|tx,rx,cfg,regs,stats`
  - Debug logs are emitted only when emulator is started with `--log-level debug`.
  - `on` enables packet+config diagnostics (`tx,rx,cfg`).
- `setvar net64`
  - Enables net64 device registration.
  - Optional value may be a TAP interface name.

Recommended order in config:
1. Set `net64_*` values first.
2. Put `setvar net64` last to start with final config in one pass.

If `net64_*` values are changed after `setvar net64`, runtime config is now applied
to the already-initialized backend (MAC/promisc/link/duplex).

If no MAC override is provided, a locally administered unicast MAC is derived from host identity material.

## TAP / Open vSwitch hookup

Example host setup:

```bash
sudo ip tuntap add dev tap0 mode tap user $USER
sudo ip link set tap0 up
sudo ovs-vsctl add-port br0 tap0
```

Then use `setvar net64_tap tap0` and `setvar net64` in the PiStorm config.

All Ethernet payload types are forwarded untouched, including IPv6 (`0x86DD`), so TCP/UDP socket support is provided by the Amiga TCP/IP stack above SANA-II.
