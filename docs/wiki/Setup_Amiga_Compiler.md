# Setup Amiga Compiler (`m68k-amigaos-gcc` + `vc`)

This page documents a full, repeatable setup for the Amiga cross-toolchain used by this repo, including both:

- `m68k-amigaos-gcc` (GNU cross compiler)
- `vc` (VBCC frontend used by some legacy Amiga-side tools)

## Why both compilers matter

- Most Amiga-side code in this tree builds with `m68k-amigaos-gcc`.
- Some legacy tools (for example `src/platforms/amiga/pistorm-dev/pistorm_dev_amiga` GUI) still expect VBCC-style flow and `vc`.

If `vc` is missing, GUI builds may fail even when GCC works.

## Prerequisites (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y make wget git gcc g++ lhasa \
  libgmp-dev libmpfr-dev libmpc-dev flex bison gettext texinfo \
  ncurses-dev autoconf rsync libreadline-dev
```

## Prepare `/opt/amiga` permissions (non-root workflow)

Use this if you want regular builds as user `smalley` (recommended):

```bash
sudo mkdir -p /opt/amiga
sudo chown -R smalley:smalley /opt/amiga
```

## Get toolchain sources

```bash
mkdir -p /opt/amiga/src
cd /opt/amiga/src
git clone https://github.com/bebbo/amiga-gcc m68k-amigaos-gcc
cd m68k-amigaos-gcc
make update
```

## Build and install the full toolchain

The build installs directly into `/opt/amiga` (no separate `make install` step).

```bash
cd /opt/amiga/src/m68k-amigaos-gcc
make clean
make drop-prefix
time make all -j4
```

Note:

- `make all` includes `gdb`.
- On modern hosts (Python 3.12/3.13), this repo's `binutils-gdb` branch can fail in GDB Python glue (`PySys_SetPath`, `_PyOS_ReadlineTState`).
- If your goal is compiler/linker + `vc`, use the safer target set below instead of `make all`.

## Recommended target set (skip fragile GDB)

```bash
cd /opt/amiga/src/m68k-amigaos-gcc
# IMPORTANT: clear host compiler overrides before cross targets
unset CC CXX CPPFLAGS CFLAGS CXXFLAGS LDFLAGS
make min vlink -j4

# build vbcc separately with host-compiler workaround
make vbcc CC='gcc -D_DEFAULT_SOURCE -std=c9x' -j4
```

This builds:

- binutils (assembler/linker/tools)
- gcc runtime/toolchain pieces from `min`
- VBCC frontend (`vc`)
- `vlink`

without requiring a successful GDB build.

## Fast path: build only missing VBCC/VLINK

If GCC already exists and only `vc` is missing:

```bash
cd /opt/amiga/src/m68k-amigaos-gcc
make vbcc vlink -j4
```

## Known fix for `make vbcc` failure (`readlink` implicit declaration)

On newer host toolchains, VBCC build can fail with:

```text
frontend/vc.c: error: implicit declaration of function 'readlink'
```

Use:

```bash
cd /opt/amiga/src/m68k-amigaos-gcc
make vbcc CC='gcc -D_DEFAULT_SOURCE -std=c9x' -j4
make vlink CC='gcc -D_DEFAULT_SOURCE -std=c9x' -j4
```

This keeps VBCC's expected C dialect while restoring the needed libc prototypes.

## GDB + modern Python failures (known)

If you see errors like:

- `PySys_SetPath was not declared`
- `_PyOS_ReadlineTState was not declared`

that is an upstream compatibility issue between older GDB Python integration code and modern Python headers.

Practical guidance for this project:

- Do not run plain `make` from `build-*/binutils` (it triggers `all`, including GDB).
- Build from the top-level using the recommended target set:
  - `unset CC ... && make min vlink -j4`
  - `make vbcc CC='gcc -D_DEFAULT_SOURCE -std=c9x' -j4`
- Only pursue GDB if you explicitly need it; otherwise skip it.

## Add toolchain to PATH

```bash
echo 'export PATH=/opt/amiga/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
```

## Verify install

```bash
which m68k-amigaos-gcc
which vc
which vlink

m68k-amigaos-gcc --version
vc
```

Expected:

- `m68k-amigaos-gcc`, `vc`, `vlink` all resolve from `/opt/amiga/bin`.
- Running `vc` should not fail with missing backend tools.

## Repo-specific checks

### PiStorm dev tools (Amiga side)

```bash
cd /home/smalley/pistorm64/src/platforms/amiga/pistorm-dev/pistorm_dev_amiga
make
```

For GUI tool:

```bash
BUILD_PISTORM_GUI=1 make
```

Note:

- GUI build also needs ReqTools pieces on the Amiga side (`reqtools.library`).
- This repo contains `libs13/reqtools.library` and `libs20/reqtools.library` for install/use on Amiga.

## Troubleshooting quick list

- `vc: command not found`
  - `vc` is not installed; run `make vbcc`.
- `vc.config` exists but `vc` does not
  - previous toolchain build was partial; rebuild VBCC (`make vbcc CC='gcc -D_DEFAULT_SOURCE -std=c9x'`).
- libnix fails with unknown `-m68020` / `-fbaserel`
  - host `gcc` leaked into `CC`; run `unset CC CXX CPPFLAGS CFLAGS CXXFLAGS LDFLAGS` and rebuild `make libnix` or `make min`.
- permissions problems under `/opt/amiga`
  - avoid mixed root/user ownership; fix with `sudo chown -R smalley:smalley /opt/amiga`.
- headers/libs not found in Amiga builds
  - verify `/opt/amiga/bin` is in `PATH`, and NDK headers/libs are present under `/opt/amiga/include` and `/opt/amiga/lib`.
