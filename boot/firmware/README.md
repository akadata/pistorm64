# boot/firmware Template Notes

Files in this directory are templates for Raspberry Pi firmware settings.

Important safety rule:

- Do not blindly copy `cmdline.txt` as-is between systems.
- Root device values (`root=...`, `rootfstype=...`) are host-specific.

Use the safe Make target instead:

```sh
sudo make install-boot-firmware
```

This target auto-detects the mounted `/` filesystem and chooses root in this order:

1. `PARTUUID`
2. `LABEL` (if it has no whitespace)
3. `UUID`

It then falls back to `/proc/cmdline`, and finally existing
`/boot/firmware/cmdline.txt`, before writing a safe merged cmdline.
