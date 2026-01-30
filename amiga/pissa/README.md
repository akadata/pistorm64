# PISSA Amiga Tooling

This directory builds:

- `libpissa.a` (static helper)
- `C:pissa` + `C:pissa020`, `C:pissa030`, `C:pissa040`

Current CLI usage:

```
pissa 0x00E90000
```

It reads the status register at the provided base address and prints it.

Build:
```
make
```

Install into `/opt/amiga`:
```
make install
```
