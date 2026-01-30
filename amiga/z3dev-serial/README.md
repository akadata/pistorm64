# z3dev-serial

Amiga-side helper library and tools for the Z2 serial echo demo device.

## Layout

- `include/` headers
- `lib/`   static library sources
- `C/`     command-line tool sources (to install into C:)
- `dev/`   device interface placeholders (WIP)

## Build

```sh
make
```

Defaults assume the Amiga NDK/SDK is installed under `/opt/amiga/`.
Override with `AMIGA_PREFIX=/path`.

## Install (manual)

```sh
# library
cp lib/libz3serial.a /opt/amiga/lib/
cp include/z3serial.h /opt/amiga/include/

# C: tool
cp C/serialecho /opt/amiga/C/
```

## Usage

The serial echo demo is a very small Z2 I/O window. The base address is assigned by
autoconfig; look for a line like:

```
[AUTOCONF] Zorro device z2-serial-echo assigned to $00EX0000
```

Then run:

```sh
serialecho 0x00EX0000 "hello"
```

It writes the string, then reads back the echoed bytes.

## WIP

A full `z3serial.device` is not implemented yet. See `dev/`.
