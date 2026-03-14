# RemoteWB

RemoteWB runs over the A314 link and provides Workbench access from the host.

Typical flow:
1) Ensure A314 services are installed in `/opt/pistorm64/a314/`.
2) Make sure `A314_SHARED` points to `/opt/pistorm64/data/a314-shared`.
3) Confirm the Amiga-side A314 drivers and RemoteWB components are installed.
4) Install Python dependencies on the Pi host:
```
sudo tools/install-a314-python-deps.sh
```
or at minimum:
```
sudo apt install -y python3-websockets
```

Limitations:
- RemoteWB currently supports the classic planar path (`640x256x3`).
- RTG/P96 modes (for example `1920x1080x8`) are not supported by RemoteWB.
- If RTG is enabled, disable it before using RemoteWB.

See:
- `a314/README.md`
- `a314/software-amiga/`

If RemoteWB does not start, check A314 service logs and verify the config file used by `a314d`.
