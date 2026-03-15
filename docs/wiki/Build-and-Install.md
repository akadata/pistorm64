# Build and Install

## Build targets

- Emulator:
```
make
```

- UAE JIT backend (bring-up/developer use only):
```
make USE_UAE_JIT=1 uae-jit
```

- Clean:
```
make clean
```

- Kernel module:
```
make kernel_module
make kernel_install
```

- Disable RTG or ALSA if needed:
```
make USE_RAYLIB=0
make USE_ALSA=0
```

## CPU correctness harness (Phase 1)

- Quick dataset validation:
```
make processortests-quick
```

- Core regression gate (quick + 68040 reference):
```
make stage1-680x0-ci
```

For details and target breakdown, see:
- `680x0TestSuite-Phase1.md`

## Install layout (runtime)

The canonical runtime layout is:

- `/opt/pistorm64/emulator`
- `/opt/pistorm64/a314/` (all A314 host services)
- `/opt/pistorm64/data/` with `lsegout.bin`, `adfs/`, `fs/`, `a314-shared/`

Do not place Python code under `data/a314-shared/`.

## Environment variables

The emulator sets the following before starting A314 services:

- `PISTORM_ROOT`
- `PISTORM_A314`
- `PISTORM_DATA`
- `A314_SHARED`
- recommended: `A314_CONF`, `A314_FS_CONF`

All Python services must use env-based paths (no `~` or hard-coded `/opt`).

## Runtime recommendation

- Use Musashi/non-JIT for normal operation.
- JIT remains under active bring-up and is not yet a stable production path.
