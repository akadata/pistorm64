## Current diagnosis snapshot

- **Symptom**: DH1 speed tests (large PFS3 partition on ~7.5 GB HDF) cause hangs/Guru. DH0/DH99 OK.
- **PiSCSI state**:
  - READBYTES/WRITEBYTES now use 64-bit offsets and lseek64; all I/O paths have u64 bounds checks (abort/log if `offset+len > file_size`).
  - Logs show bogus 64-bit offsets arriving from the guest, e.g. `io_Offset=0x400033001F50BE00` → “READBYTES beyond end of disk”. This is the crash trigger: the Amiga-side driver is issuing bad commands (upper word garbage).
  - Capacity: HDF is ~7.5 GB (size=0x1dc7fc000). Amiga tools report ~3.5 GB, likely due to driver/protocol limitations.
- **kmod state**:
  - BUSOP/BATCH ioctls log via pr_debug, but dynamic debug not available on this kernel/debugfs mount failed.
  - Ioctls can still block if the CPLD doesn’t respond; no timeout yet.
- **Threads on hang**: CPU/IPL threads block in kmod `ioctl()` (ps_read_8/16); userland not spinning.
- **Root cause**: guest PiSCSI driver populates 64-bit byte offsets incorrectly for READBYTES; needs to use correct high word or switch to READ64/WRITE64. Pi-side now rejects overrun instead of corrupting.

## Next steps (when resuming)
- Implement kmod bus timeout to avoid permanent ioctl hangs; log offending op/address on timeout.
- Optionally reject READBYTES/WRITEBYTES when high word != 0 to force guest to use 64-bit paths for >4 GB.
- Capture failing BUSOP via dmesg once timeout/logging is in place, or audit/fix Amiga pi-scsi.device to build correct offsets.
