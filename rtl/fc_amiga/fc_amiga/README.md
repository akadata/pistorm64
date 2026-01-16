# FC-enabled Amiga CPLD build (experimental)

This variant ports dynamic FC-line handling from the Atari fork into the Amiga top-level. It leaves the original `rtl/pistorm.v` untouched; use `pistorm_fc.v` as the top entity when you want FC lines driven per transaction.

What changed
- `pistorm_fc.v` captures FC bits (PI_D[15:13]) on `REG_ADDR_HI` writes and drives `M68K_FC` during bus transactions (default FC=111); FC is tri-stated when BGACK is asserted.
- AS/UDS/LDS/RW/VMA are also gated with BGACK so PiStorm relinquishes the bus cleanly.
- Intent is to allow FC-aware MMU/mapping without sacrificing the stable Amiga bitstream.

How to build an SVF/POF (Quartus 20.1 Lite)
1) Copy/clone the existing Quartus project or open `rtl/pistorm.qpf`.
2) Add `fc_amiga/pistorm_fc.v` to the project and set the top-level entity to `pistorm_fc`.
3) Target device: EPM240T100C5 (or your board’s device), keep the 200 MHz PI_CLK constraint from `pistorm.sdc` (or adjust if your bitstream expects a different GPCLK rate).
4) Compile. In Quartus: `Assignments -> Settings -> Compilation Process Settings -> Generate programming files`.
5) Use `File -> Convert Programming Files` to generate `.pof`/`.svf` as needed. Note the resulting USERCODE to distinguish builds.

Notes
- Keep the original bitstreams intact; this is experimental and should be flashed only on hardware known to tolerate FC-line drive.
- If your hardware or firmware expects grounded/static FC, continue using the stock Amiga bitstream (`rtl/EPM240_bitstream.svf`).
