# Requires xdftool from amitools (https://github.com/cnvogelg/amitools/)
set -e

write_if_exists() {
  local src="$1"
  local dst="$2"
  if [ -e "$src" ]; then
    if [ -n "$dst" ]; then
      xdftool pistorm.hdf open part=DH99 + write "$src" "$dst"
    else
      xdftool pistorm.hdf open part=DH99 + write "$src"
    fi
  else
    echo "[build_hdf] Skipping missing file: $src"
  fi
}

rm pistorm.hdf
rdbtool pistorm.hdf create size=2.5Mi + init rdb_cyls=2
rdbtool pistorm.hdf add size=100% name=DH99 dostype=ffs
rdbtool pistorm.hdf fsadd dos1.bin fs=DOS1
xdftool pistorm.hdf open part=DH99 + format PiStorm ffs
xdftool pistorm.hdf open part=DH99 + write Disk.info
write_if_exists pistorm-dev/pistorm_dev_amiga/PiSimple
write_if_exists pistorm-dev/pistorm_dev_amiga/PiStorm
write_if_exists pistorm-dev/pistorm_dev_amiga/PiStorm.info
write_if_exists pistorm-dev/pistorm_dev_amiga/libs13
write_if_exists pistorm-dev/pistorm_dev_amiga/libs20
write_if_exists pistorm-dev/pistorm_dev_amiga/libs13.info
write_if_exists pistorm-dev/pistorm_dev_amiga/libs20.info
write_if_exists pistorm-dev/pistorm_dev_amiga/CopyMems
write_if_exists ../../a314/software-amiga a314
xdftool pistorm.hdf open part=DH99 + makedir net
write_if_exists net/net_driver_amiga/pi-net.device net/pi-net.device
write_if_exists net64/net_driver_amiga/net64.device net/net64.device
write_if_exists net64/net_driver_amiga/net64info net/net64info
write_if_exists net64/net_driver_amiga/net64info.info net/net64info.info
xdftool pistorm.hdf open part=DH99 + makedir scsi
write_if_exists piscsi/device_driver_amiga/pi-scsi.device scsi/pi-scsi.device
write_if_exists piscsi64/device_driver_amiga/pi-scsi64.device scsi/pi-scsi64.device
xdftool pistorm.hdf open part=DH99 + makedir rtg
write_if_exists "pirtg64/Amiga/PiRTG64/PiRTG64 Installer" "rtg/PiRTG64 Install"
write_if_exists "pirtg64/Amiga/PiRTG64/PiRTG64 Installer.info" "rtg/PiRTG64 Install.info"
xdftool pistorm.hdf open part=DH99 + makedir "rtg/PiRTG64 Install/Files"
write_if_exists pirtg64/Amiga/rtg_driver_amiga/pirtg64.card "rtg/PiRTG64 Install/Files/pirtg64.card"
write_if_exists pirtg64/Amiga/rtg_driver_amiga/PiRTG64.info "rtg/PiRTG64 Install/Files/PiRTG64.info"
