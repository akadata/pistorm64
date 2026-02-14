# PiSCSI64 Remote Utilities

## Binaries

From repo root:

```sh
make piscsi64-remote
make piscsi64-remote-server
make piscsi64-remote-client
```

From this folder (platform-specific output in `out/`):

```sh
cd tools/piscsi64_remote
make                # auto-selects platform makefile
make -f Makefile.linux
# or
make -f Makefile.mac
# or on Windows (MinGW)
mingw32-make -f Makefile.windows
```

- `piscsi64-remote`: exports one local file/device over TCP (primary command).
- `piscsi64-remote-server`: alias binary (same server implementation).
- `piscsi64-remote-client`: probe utility (handshake + optional read dump).

## Server (Linux/Unix)

If you built from `tools/piscsi64_remote`, run the binary from `out/`:

```sh
./out/piscsi64-remote \
  --listen 0.0.0.0:4964 \
  --export workbench \
  --path /dev/disk/by-id/usb-EXAMPLE \
  --token YOUR_SHARED_TOKEN \
  --mode ro
```

CD-ROM export example:

```sh
./out/piscsi64-remote \
  --listen 0.0.0.0:4964 \
  --export os39iso \
  --path /opt/Amiga/AmigaOS39.iso \
  --token YOUR_SHARED_TOKEN \
  --kind cdrom
```

Remote payload encryption:

- Token is required (`--token`) for the server and for `remote:token@host:port/export` on Pi side.
- Data payloads for READ/WRITE are AES-CTR encrypted with a session key derived from token + handshake nonces.
- Without the token, wire payloads are not readable.

## Client Probe

Probe is optional and only for diagnostics.

```sh
./out/piscsi64-remote-client 192.168.1.50:4964 workbench token
./out/piscsi64-remote-client 192.168.1.50:4964 workbench token 0 512
```

## PiStorm64 Mapping Examples

```ini
setvar piscsi64_6 remote:token@192.168.1.50:4964/workbench,mode=ro
setvar piscsi64_7 remote:token@192.168.1.50:4964/workbench,mode=rw
setvar piscsi64_8 cdrom:remote:token@192.168.1.50:4964/os39iso
```

## Windows Notes

- Native Windows probe client source is included:
  - `tools/piscsi64_remote/piscsi64_remote_client_win.c`
- Build example with MSVC Developer Prompt:
  - `cl /O2 /W3 tools\\piscsi64_remote\\piscsi64_remote_client_win.c ws2_32.lib libcrypto.lib`
- Usage:
  - `piscsi64_remote_client_win.exe 192.168.1.50 workbench token 0 512`

Windows-native remote server/service support is still a planned follow-on phase.
