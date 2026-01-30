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
- Intel/Altera Quartus Lite tooling.
- Access to the PiStorm FC RTL under:

```
rtl/fc_amiga/fc_amiga/
```

## Arch Linux install

On Arch, install the Quartus Lite tooling using AUR:

```
yay -S quartus-free-devinfo-arria_lite
```

This provides the Intel FPGA toolchain under `/opt/intelFPGA/20.1/` and ModelSim under:

```
/opt/intelFPGA/20.1/modelsim_ase/bin/
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
rtl/fc_amiga/fc_amiga/
```

Files:
- `pistorm_fc.v` – FC line logic.
- `README.md` – notes on pins and behavior.
- `build_svf.sh` – helper script to build an SVF.

Start by reading:

```
rtl/fc_amiga/fc_amiga/README.md
```

## Build an SVF (example)

From the repo root:

```
cd rtl/fc_amiga/fc_amiga
./build_svf.sh
```

This script is expected to create an SVF for programming. If it fails, note the error and report it.

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

- **WIP**: FC mode is stubbed in software. The CPLD flow is not enabled by default.
- Some systems need root for JTAG unless you add udev rules.
- Dark GTK themes can make the Pin Planner unreadable; run Quartus with a light theme if needed.

## What we need from testers

- Exact board model and programmer.
- Host OS and Quartus version.
- Whether the JTAG chain is detected.
- Whether the SVF programs correctly.
- Any FC‑line behavior observations.

Please post results with logs if possible.
