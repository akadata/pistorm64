# Requires xdftool from amitools (https://github.com/cnvogelg/amitools/)
set -e

SHARED_DIR="${SHARED_DIR:-/opt/pistorm64/data/a314-shared}"

write_if_exists() {
  local src="$1"
  local dst="$2"
  if [ -e "$src" ]; then
    if [ -n "$dst" ]; then
      if ! xdftool pistorm.hdf open part=DH99 + write "$src" "$dst"; then
        echo "[build_hdf] WARN: write failed src=$src dst=$dst"
      fi
    else
      if ! xdftool pistorm.hdf open part=DH99 + write "$src"; then
        echo "[build_hdf] WARN: write failed src=$src"
      fi
    fi
  else
    echo "[build_hdf] Skipping missing file: $src"
  fi
}

mkdir_p_hdf() {
  local full="$1"
  local path=""
  local part

  # Build each path segment to avoid xdftool nested-makedir quirks.
  IFS='/' read -r -a _parts <<< "$full"
  for part in "${_parts[@]}"; do
    [ -z "$part" ] && continue
    if [ -z "$path" ]; then
      path="$part"
    else
      path="$path/$part"
    fi
    xdftool pistorm.hdf open part=DH99 + makedir "$path" >/dev/null 2>&1 || true
  done
}

write_elf_with_info() {
  local elf_path="$1"
  local elf_name
  local dst_elf
  local dst_info

  elf_name="$(basename "$elf_path")"
  dst_elf="Arm/${elf_name}"
  dst_info="${dst_elf}.info"

  write_if_exists "$elf_path" "$dst_elf"
  if [ -e "${elf_path}.info" ]; then
    write_if_exists "${elf_path}.info" "$dst_info"
  elif [ -e "${SHARED_DIR}/app.elf.info" ]; then
    write_if_exists "${SHARED_DIR}/app.elf.info" "$dst_info"
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
mkdir_p_hdf "net"
write_if_exists net/net_driver_amiga/pi-net.device net/pi-net.device
write_if_exists net64/net_driver_amiga/net64.device net/net64.device
write_if_exists net64/net_driver_amiga/net64info net/net64info
write_if_exists net64/net_driver_amiga/net64info.info net/net64info.info
mkdir_p_hdf "scsi"
write_if_exists piscsi/device_driver_amiga/pi-scsi.device scsi/pi-scsi.device
mkdir_p_hdf "rtg"
write_if_exists "pirtg64/Amiga/PiRTG64/PiRTG64 Installer" "rtg/PiRTG64 Install"
write_if_exists "pirtg64/Amiga/PiRTG64/PiRTG64 Installer.info" "rtg/PiRTG64 Install.info"
mkdir_p_hdf "rtg/PiRTG64 Install/Files"
write_if_exists pirtg64/Amiga/rtg_driver_amiga/pirtg64.card "rtg/PiRTG64 Install/Files/pirtg64.card"
write_if_exists pirtg64/Amiga/rtg_driver_amiga/PiRTG64.info "rtg/PiRTG64 Install/Files/PiRTG64.info"

# ARMAccel runtime + payloads
mkdir_p_hdf "Tools"
mkdir_p_hdf "Arm"
mkdir_p_hdf "Libs"
mkdir_p_hdf "Devs"

write_if_exists "${SHARED_DIR}/armrun" "Tools/armrun"
write_if_exists "${SHARED_DIR}/armrun.info" "Tools/armrun.info"
write_if_exists "${SHARED_DIR}/armshake" "Tools/armshake"
write_if_exists "${SHARED_DIR}/armshake.info" "Tools/armshake.info"

write_if_exists "${SHARED_DIR}/armaccel.library" "Libs/armaccel.library"
write_if_exists "${SHARED_DIR}/armaccel.device" "Devs/armaccel.device"

if compgen -G "${SHARED_DIR}/*.elf" > /dev/null; then
  for elf in "${SHARED_DIR}"/*.elf; do
    write_elf_with_info "$elf"
  done
else
  echo "[build_hdf] No ELF payloads found in ${SHARED_DIR}"
fi

# Keep a reusable project-icon template on disk for new payloads.
write_if_exists "${SHARED_DIR}/app.elf.info" "Arm/app.elf.info"
