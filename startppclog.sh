unset LD_LIBRARY_PATH
export QEMU_UAE_SO=/usr/local/lib/qemu-uae.so
export PPC_ACCEL_QEMU_LOG=1
export PPC_ACCEL_TRACE_IO=1
export PPC_ACCEL_TRACE_IO_LIMIT=128
./emulator --log ppc.log --log-level debug
