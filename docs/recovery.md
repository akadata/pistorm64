# Recovery tmpfs helper

If the SD card/root filesystem is unhealthy, you can create a small tmpfs-based recovery shell with core tools (busybox, e2fsck) under `/mnt/recovery` using:

```sh
sudo tools/mkrecoveryram.sh            # default /mnt/recovery, 64MB tmpfs
# or custom mount point/size:
TMPFS_SIZE=128m sudo tools/mkrecoveryram.sh /mnt/recoveryshell
```

The script:
- mounts a tmpfs at the chosen path
- copies busybox and symlinks basic commands (sh, ls, mount, umount, dmesg, reboot, cat, echo)
- copies `/sbin/e2fsck` and its shared library dependencies

To enter the recovery shell:

```sh
sudo chroot /mnt/recovery /bin/sh
```

You can then run `e2fsck`, `mount`, `dmesg`, `reboot`, etc. Adjust the mount size via `TMPFS_SIZE` if you need more headroom. Busybox must be installed on the host for the core tools to be available. 
