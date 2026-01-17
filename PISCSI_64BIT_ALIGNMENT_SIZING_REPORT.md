## PiSCSI 64-bit alignment / sizing notes

Context: PiSCSI on a 7.5 GB UAE/PFS3 HDF (heads=16, spt=63, block size=512). DH0/DH1 partitions live on unit 0; DH99 is the small ROM disk on unit 6.

### Symptoms
- SysInfo “speed” on DH1 (large PFS3 partition) locks the emulator; DH0/DH99 work.
- Drive size shown as ~3.5 GB instead of 7.5 GB image, suggesting truncation of capacity/offsets.
- Logs show large I/Os completing; occasional 512-byte reads to chip RAM fall back to CPU copy (unmapped “DMA” target) and succeed. No PiSCSI errors logged before hang. After lock, CPU/IPL threads block in kmod `ioctl` (ps_read_8/16/ps_gpio_lev), meaning the kmod never returns.

### Image details
- HDF: UAE/PFS3, ~7.5 GB (`Rigid Disk Block on UAE BlankPFS3-8GB.h`).
- CHS from RDB: heads=16, sectors=63, block size=512.
- DH0 low/high cyl: 2–1017. DH1: 1018–15489.
- MaxTransfer 0x1FE00, Mask 0x7FFFFFFE.

### Suspected cause
Residual 32-bit truncation or missing bounds checks on large LBA/file offsets, leading to out-of-range accesses and the CPLD/kmod hang. SysInfo showing ~3.5 GB vs 7.5 GB image supports a size cap/truncation.

### Actions taken
- Kmod: added BUSOP/BATCH ioctl debug prints (`ps_ioctl` now logs is_read/width/addr/flags, batch count/ptr). `compat_ioctl` hooked. (branch `akadata/machine-monitor`).
- PiSCSI: added u64 bounds checks on every READ/WRITE/READ64/WRITE64/READBYTES/WRITEBYTES; abort/log if `offset + len > file_size` (commit `39d5a8cb`).
- Kmod logging remains otherwise quiet; dmesg shows only `/dev/pistorm ready` on load.

### Next steps
- Add a timeout/error return in kmod bus read/write to avoid permanent ioctl block; log the offending address/op when timing out.
- Capture dmesg after a hang with the new ioctl debug to identify the stuck BUSOP (address/width/is_read).
- Verify PiSCSI reports full capacity: log `st_size` and derived total sectors on attach; compare to what Amiga tools report.
- If needed, refuse unmapped “DMA” targets instead of CPU-copying chip RAM to surface errors earlier.
