# Compile for Amiga with Host Clang/LLVM

This note documents what is currently practical in this repo using host LLVM tools.

## Scope

- `ppcshake --elf` payloads: viable with host LLVM (if `ld.lld` is available).
- Full AmigaOS m68k user binaries (for example `ppcshake`): not yet a clean host-only LLVM path in this tree.

## 1. Check host LLVM toolchain

```bash
clang --version
llvm-readelf --version
```

For full host-only PPC ELF linking you also need:

```bash
ld.lld --version
```

If `ld.lld` is missing, install your distro package for LLVM lld.

## 2. Build a PPC32 big-endian ELF payload locally (for `ppcshake --elf`)

This builds the existing marker payload using only host LLVM tools.

```bash
cd /home/smalley/pistorm64

clang \
  --target=powerpc-unknown-elf \
  -c -x assembler-with-cpp \
  amiga/zorro-ppc/ppc-elf/hello_marker.S \
  -o /tmp/hello_marker.o

ld.lld \
  -m elf32ppc \
  -nostdlib \
  -T amiga/zorro-ppc/ppc-elf/hello_marker.ld \
  /tmp/hello_marker.o \
  -o amiga/zorro-ppc/C/ppc_hello_marker.elf

llvm-readelf -h -l amiga/zorro-ppc/C/ppc_hello_marker.elf | sed -n '1,80p'
```

Copy for Amiga-side test:

```bash
cp -f amiga/zorro-ppc/C/ppc_hello_marker.elf /opt/pistorm64/data/a314-shared/
```

Run on Amiga:

```text
ppcshake --elf pi0:ppc_hello_marker.elf
```

## 3. Why `ppcshake` itself is still built with Amiga GCC

Current `ppcshake` build uses:

```text
/opt/amiga/bin/m68k-amigaos-gcc
```

Reasons host-only clang is not drop-in yet:

- NDK inline call macros use constraints such as `"rf"` that clang does not accept in this m68k path.
- Clang-produced m68k ELF objects are not directly consumed by the current `m68k-amigaos` linker flow in this setup.
- Forcing `_NO_INLINE` avoids one class of errors but does not solve full link/ABI compatibility.

So today, keep this split:

- Amiga m68k binaries (`ppcshake`): `m68k-amigaos-gcc` toolchain in `/opt/amiga`.
- PPC test payload ELF (`--elf`): host clang/llvm (with `ld.lld`) is fine.

## 4. Current repo defaults

- `amiga/zorro-ppc/Makefile` builds `ppcshake` with `/opt/amiga/bin/m68k-amigaos-gcc`.
- `amiga/zorro-ppc/ppc-elf/build_remote.sh` currently builds PPC ELF on `homer` using the Amiga PPC cross toolchain.

You can keep `build_remote.sh` as the fallback path and use local LLVM payload builds when available.

