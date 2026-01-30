# Configuration

The emulator uses a config file to define memory maps, ROMs, devices, and platform features.
See `config_file/readme.md` for full syntax details.

## Map syntax

Example:
```
map type=rom address=0xF80000 size=0x80000 file=kick.rom ovl=0 id=kickstart autodump_mem
```

Key map types:
- `rom`: Read-only memory region (writes ignored or refused).
- `ram`: CPU-visible RAM (not DMA-capable for peripherals).
- `register`: I/O / register range.
- `ram_noalloc`: RAM backed by external buffer.
- `wtcram`: Write-through cache region (writes go to bus).

## Typical Amiga maps

- Kickstart ROM at `0x00F80000` (with `ovl=0` during overlay).
- Z2 fast at `0x00200000-0x009FFFFF` (8 MB).
- Z3 fast in high address space (e.g. `0x50000000+`).
- RTG framebuffer at `0x70010000+`.
- Optional PiSCSI DMA window at `0x60000000+`.

## Logging

Use the CLI to direct logs:
```
./emulator --log boot.log --debug-level debug
```

For heavy tracing only enable debug briefly, as it slows performance.
