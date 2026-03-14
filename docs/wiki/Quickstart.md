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

4) Optional: UAE JIT bring-up (developer-only)
```
make USE_UAE_JIT=1 uae-jit
PISTORM_ENABLE_QUEUE=0 ./emulator --jit
```

5) Kernel module (if needed)
```
make kernel_module
make kernel_install
```

6) A314 Python service dependencies (recommended)
```
sudo tools/install-a314-python-deps.sh
```
Manual equivalent:
```
sudo apt update
sudo apt install -y python3-pyudev python3-websockets python3-pip
sudo python3 -m pip install --break-system-packages --upgrade python-pytun
```

Notes:
- For legacy userspace GPIO, build with `make PISTORM_KMOD=0`.
- Use `--log` and `--debug-level` for verbose tracing.
- If you modify A314 files under `a314/`, re-install the A314 services to `/opt/pistorm64/a314/`.
- `hid.py` requires `pyudev`; if missing, A314 HID on-demand startup will fail.
- `remotewb.py` requires `websockets`; if missing, install `python3-websockets`.
- RemoteWB currently expects classic planar Workbench (`640x256x3`); disable RTG/P96 while using RemoteWB.
- JIT is currently work-in-progress and not yet a reliable daily-use runtime mode.
