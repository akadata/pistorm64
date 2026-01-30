# Quickstart

This is the fastest path to a working emulator build.

## Build

```bash
make
```

This builds the emulator plus diagnostics (`buptest`, `pistorm_truth_test`).

## Run

```bash
./emulator
```

If you need a specific config file:

```bash
./emulator --config default.cfg
```

## Optional

- Build tools:
  ```bash
  ./build_regtool.sh
  ./build_clkpeek.sh
  ./build_pimodplay.sh
  ```

- Disable RTG/raylib:
  ```bash
  make USE_RAYLIB=0
  ```

- Disable ALSA:
  ```bash
  make USE_ALSA=0
  ```

See `Build-and-Install.md` for full details.
