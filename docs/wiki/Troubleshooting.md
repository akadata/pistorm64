# Troubleshooting

## Emulator hangs or boots to CLI only

- Enable logging and capture a trace:
```
./emulator --log boot.log --debug-level debug
```

- Check for ROM write attempts or DMA into unmapped regions.

## No A314 services

- Verify env vars (`PISTORM_ROOT`, `PISTORM_A314`, `PISTORM_DATA`, `A314_SHARED`).
- Check `/opt/pistorm64/a314/` for installed services.

## PiSCSI not detected

- Confirm PiSCSI ROM is loaded.
- Validate HDF paths and filesystem handlers.
- Verify Z2 autoconfig addresses in logs.

## RTG issues

- Rebuild with `USE_RAYLIB=0` to test headless mode.
- Confirm RTG memory map range is present in config.
