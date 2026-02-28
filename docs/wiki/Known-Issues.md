# Known Issues

- SysInfo 4.4 (Benchmark) is known to be unreliable on fast emulators and can crash under some configurations.
  - Reference: https://sysinfo.d0.se
  - SysInfo notes: results not verified on 68060; unreliable on fast emulators; WinUAE JIT crash reported.

- Some Amiga utilities make assumptions about 24-bit addresses or cache behavior and can misbehave on 68020+ setups. See Amiga developer guidelines on 680x0 compatibility.
- RTG has a known window-decoration issue (display polish/usability), currently under active work.
- HDToolBox scan behavior can be limited to SCSI IDs `0..6` on some versions/tooltype setups.
  - PiSCSI64 itself supports units `1..15`; IDs above 6 are usable once prepared/mounted.
  - Treat this as an Amiga tool limitation, not a PiSCSI64 backend/controller limit.
- `piscsi64` currently conflicts with mixed Zorro demo/utility device configs (`z3bus`, `zorro-serial`, `zorro-rng`, `zorro-pissa`) and is not recommended in those combinations.
  - Workaround: use `piscsi` for storage in mixed Zorro setups.

Add new issues here as they are discovered.

## UAE JIT bring-up (AArch64)

- Current state is tracked in `UAE-JIT-Status.md`.
- JIT is still bring-up only and does not currently provide a reliable normal-boot runtime path.
- Runtime still shows a post-init overlay/reset issue (`OVL:0`) in JIT path.
- Z2 autoconfig can spam "Unexpected WORD read ..." under JIT-width access
  patterns until width handling is fully normalized.
- `68060` currently falls back to `68000` in this tree's parser/tables.
- `BAD JIT opcodes list` entries are expected fallback markers and are not alone
  a crash root-cause.
