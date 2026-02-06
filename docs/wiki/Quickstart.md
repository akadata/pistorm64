# Quickstart

This assumes a Pi 4 with the kernel module installed and `/dev/pistorm` present.

1) Build
```
make
```

2) Run with default config (Musashi, non-JIT)
```
./emulator
```

3) Optional: custom config
```
./emulator --config amiga.cfg
```

4) Optional: UAE JIT (experimental)
```
make USE_UAE_JIT=1 uae-jit
PISTORM_ENABLE_QUEUE=0 ./emulator --jit
```

5) Kernel module (if needed)
```
make kernel_module
make kernel_install
```

Notes:
- For legacy userspace GPIO, build with `make PISTORM_KMOD=0`.
- Use `--log` and `--debug-level` for verbose tracing.
- If you modify A314 files under `a314/`, re-install the A314 services to `/opt/pistorm64/a314/`.
