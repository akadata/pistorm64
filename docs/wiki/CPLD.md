# CPLD Setup (WIP)

This page is a **work in progress**. We need testers to validate and report back on different hosts and programmer adapters.

## Scope

This covers:
- Installing Quartus/ModelSim tooling on Arch.
- Detecting a USB‑Blaster.
- Building PiStorm FC CPLD bitstreams.
- Basic programming workflow.

It does **not** cover full hardware bring‑up or board‑specific jumper details. If you have a different CPLD board, please add notes.

## Requirements

- Supported USB‑Blaster (or compatible) programmer.
- Intel/Altera Quartus tooling (Quartus Prime Lite is fine).
- Access to the PiStorm FC RTL.

Primary EPM240 FC/BERR variant:

```
rtl.fc/
```

Legacy FC RTL (older flow):

```
rtl/fc_amiga/fc_amiga/
```

## Arch Linux install

Quartus is required for compilation (`quartus_sh`, `quartus_cpf`). ModelSim alone is not enough.

Recommended AUR packages:

```
yay -S quartus-free-quartus quartus-free-devinfo-max
```

Optional packages:

```
yay -S quartus-free-help quartus-free-questa arrow-usb-blaster
```

Alternative (older but known‑good for MAX II): `quartus-130` (Quartus II 13.0 SP1).

Quartus typically installs under `/opt/intelFPGA/`:

```
/opt/intelFPGA/20.1/quartus/bin/
/opt/intelFPGA/20.1/modelsim_ase/bin/
```

Quick check:

```
which quartus_sh
find /opt -path "*/quartus/bin/quartus_sh" -o -path "*/quartus/bin64/quartus_sh"
```

Example contents:

```
drill      jobspy  qhgencomp  qhsim     qvlcom    sm_entity  vcover  vencrypt   vlib   vopt  wlf2log     xml2ucdb
dumplog64  qhcvt   qhlib      qrun      sccom     triage     vdbg    verror     vlog   vovl  wlf2vcd
flps_util  qhdel   qhmake     qverilog  scgenmod  vcd2wlf    vdel    vgencomp   vmake  vrun  wlfman
hm_entity  qhdir   qhmap      qvhcom    sdfcom    vcom       vdir    vhencrypt  vmap   vsim  wlfrecover
```

## USB‑Blaster detection

Plug the USB‑Blaster and check for detection:

```
dmesg | tail
```

Then list JTAG chain devices:

```
sudo /opt/intelFPGA/20.1/quartus/bin/jtagconfig
```

If you get “Insufficient port permissions”, run `jtagconfig` as root.

Optional: add a udev rule for USB‑Blaster so you don’t need sudo (example rule varies by programmer vendor ID).

## PiStorm FC RTL

RTL source is here:

```
rtl.fc/
```

Files:
- `pistorm_fc.v` – FC/BERR logic (EPM240).
- `pistorm_fc.qpf` / `pistorm_fc.qsf` – Quartus project.
- `build_pistorm_fc.sh` – helper script to build an SVF.

## Build an SVF (EPM240 FC/BERR)

Use the FC/BERR RTL in `rtl.fc/pistorm_fc.v` with the EPM240 project files, then:

```
cd rtl.fc
./build_pistorm_fc.sh <project_name_without_ext>
```

The script expects a Quartus project (`.qpf`/`.qsf`) in the same directory and
will produce an `.svf` from the compiled `.pof`.

If Quartus is not on PATH, set:

```
export QUARTUS_ROOT=/opt/intelFPGA/20.1
# or: export QUARTUS_BIN=/opt/intelFPGA/20.1/quartus/bin
```

## PI_CLK frequency (200 MHz default)

The CPLD state machine runs from **PI_CLK** (GPIO4 / GPCLK0). This clock is
independent of the Amiga’s 7 MHz / 3.58 MHz clocks; it only drives the CPLD’s
internal logic that sequences transactions and samples bus edges.

We default to **200 MHz** (Pi4 `gpclk_src=5 gpclk_div=6`) for compatibility with
current Pi4 setups and proven boot stability:
- The CPLD logic was originally tuned around a fast PI_CLK, and lower GPCLK
  divisors have proven unstable on some Pi4 boards.
- 200 MHz keeps GPIO transactions and bus handshakes responsive on real
  hardware, even if worst‑case timing reports are pessimistic.

If you want to run slower (for experimentation), you can set GPCLK to 125 MHz or
100 MHz and update the SDC PI_CLK period. Note that some boards do **not** boot
reliably at 125/100 MHz.

- 100 MHz → 10.000 ns
- 125 MHz → 8.000 ns
- 200 MHz → 5.000 ns

This is a policy choice: 200 MHz is the current “works on Pi4” default, while
125/100 MHz are experimental options that may not boot on all boards.

## Quartus warnings (timing + c7m_sync)

Common warnings you might see:
- **“c7m_sync[2] was determined to be a clock…”**  
  `c7m_sync` is a logic‑derived sampling of the 68k clock. Quartus can treat it
  like a derived clock even though we don’t constrain it. This is expected.
- **“Timing requirements not met” (slow model)**  
  This usually refers to PI_CLK. If your GPCLK is 200 MHz but the SDC is set to
  100/125 MHz (or vice‑versa), you will see misleading slack. Always set the SDC
  to match your real GPCLK.

These warnings do not necessarily mean the CPLD will fail in hardware, but
they are signals that your timing constraints and actual GPCLK should match.

## Programming (generic)

From Quartus Programmer:
- Open **Programmer**.
- Auto‑detect the device.
- Add the generated `.svf` or `.pof` (depends on the flow you used).
- Start programming.

Command‑line (example):

```
sudo /opt/intelFPGA/20.1/quartus/bin/jtagconfig
```

## Notes / Known Issues

- Some systems need root for JTAG unless you add udev rules.
- Dark GTK themes can make the Pin Planner unreadable; run Quartus with a light theme if needed.

## What we need from testers

- Exact board model and programmer.
- Host OS and Quartus version.
- Whether the JTAG chain is detected.
- Whether the SVF programs correctly.
- Any FC‑line behavior observations.

Please post results with logs if possible.
