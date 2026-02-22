REM Requires xdftool from amitools (https://github.com/cnvogelg/amitools/)
del pistorm.hdf
rdbtool pistorm.hdf create size=2.5Mi + init rdb_cyls=2
rdbtool pistorm.hdf add size=100% name=DH99 dostype=ffs
rdbtool pistorm.hdf fsadd dos1.bin fs=DOS1
xdftool pistorm.hdf open part=DH99 + format PiStorm ffs
xdftool pistorm.hdf open part=DH99 + write Disk.info
if exist pistorm-dev/pistorm_dev_amiga/PiSimple xdftool pistorm.hdf open part=DH99 + write pistorm-dev/pistorm_dev_amiga/PiSimple
if exist pistorm-dev/pistorm_dev_amiga/PiStorm xdftool pistorm.hdf open part=DH99 + write pistorm-dev/pistorm_dev_amiga/PiStorm
if exist pistorm-dev/pistorm_dev_amiga/PiStorm.info xdftool pistorm.hdf open part=DH99 + write pistorm-dev/pistorm_dev_amiga/PiStorm.info
if exist pistorm-dev/pistorm_dev_amiga/libs13 xdftool pistorm.hdf open part=DH99 + write pistorm-dev/pistorm_dev_amiga/libs13
if exist pistorm-dev/pistorm_dev_amiga/libs20 xdftool pistorm.hdf open part=DH99 + write pistorm-dev/pistorm_dev_amiga/libs20
if exist pistorm-dev/pistorm_dev_amiga/libs13.info xdftool pistorm.hdf open part=DH99 + write pistorm-dev/pistorm_dev_amiga/libs13.info
if exist pistorm-dev/pistorm_dev_amiga/libs20.info xdftool pistorm.hdf open part=DH99 + write pistorm-dev/pistorm_dev_amiga/libs20.info
if exist pistorm-dev/pistorm_dev_amiga/CopyMems xdftool pistorm.hdf open part=DH99 + write pistorm-dev/pistorm_dev_amiga/CopyMems
if exist ../../a314/software-amiga xdftool pistorm.hdf open part=DH99 + write ../../a314/software-amiga a314
xdftool pistorm.hdf open part=DH99 + makedir net
if exist net/net_driver_amiga/pi-net.device xdftool pistorm.hdf open part=DH99 + write net/net_driver_amiga/pi-net.device net/pi-net.device
if exist net64/net_driver_amiga/net64.device xdftool pistorm.hdf open part=DH99 + write net64/net_driver_amiga/net64.device net/net64.device
if exist net64/net_driver_amiga/net64info xdftool pistorm.hdf open part=DH99 + write net64/net_driver_amiga/net64info net/net64info
if exist net64/net_driver_amiga/net64info.info xdftool pistorm.hdf open part=DH99 + write net64/net_driver_amiga/net64info.info net/net64info.info
xdftool pistorm.hdf open part=DH99 + makedir scsi
if exist piscsi/device_driver_amiga/pi-scsi.device xdftool pistorm.hdf open part=DH99 + write piscsi/device_driver_amiga/pi-scsi.device scsi/pi-scsi.device
if exist piscsi64/device_driver_amiga/pi-scsi64.device xdftool pistorm.hdf open part=DH99 + write piscsi64/device_driver_amiga/pi-scsi64.device scsi/pi-scsi64.device
xdftool pistorm.hdf open part=DH99 + makedir rtg
if exist "pirtg64/Amiga/PiRTG64/PiRTG64 Installer" xdftool pistorm.hdf open part=DH99 + write "pirtg64/Amiga/PiRTG64/PiRTG64 Installer" "rtg/PiRTG64 Install"
if exist "pirtg64/Amiga/PiRTG64/PiRTG64 Installer.info" xdftool pistorm.hdf open part=DH99 + write "pirtg64/Amiga/PiRTG64/PiRTG64 Installer.info" "rtg/PiRTG64 Install.info"
xdftool pistorm.hdf open part=DH99 + makedir "rtg/PiRTG64 Install/Files"
if exist pirtg64/Amiga/rtg_driver_amiga/pirtg64.card xdftool pistorm.hdf open part=DH99 + write pirtg64/Amiga/rtg_driver_amiga/pirtg64.card "rtg/PiRTG64 Install/Files/pirtg64.card"
if exist pirtg64/Amiga/rtg_driver_amiga/PiRTG64.info xdftool pistorm.hdf open part=DH99 + write pirtg64/Amiga/rtg_driver_amiga/PiRTG64.info "rtg/PiRTG64 Install/Files/PiRTG64.info"
