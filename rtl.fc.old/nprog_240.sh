if pgrep -x "emulator" > /dev/null; then
    echo "PiStorm emulator is running, please stop it first"
    exit 1
fi

if lsmod | grep -q '^pistorm'; then
    echo "Unloading pistorm.ko to free GPIO/JTAG pins..."
    sudo rmmod pistorm || true
fi
echo "Flashing..."
if [ -n "${OPENOCD_SPEED:-}" ]; then
    sudo openocd -f ./68_240.cfg -c "adapter speed ${OPENOCD_SPEED}" > nprog_log.txt 2>&1
else
    sudo openocd -f ./68_240.cfg > nprog_log.txt 2>&1
fi
if [ $? -ne 0 ]
then
    echo "Flashing failed, please see nprog_log.txt for details"
    exit 1
else
    echo "Flashing successful!"
fi
