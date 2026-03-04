mkdir -p build
make -j4 emulator
make -C amiga/zorro-ppc

cat <<'EOF'
Stage 8 bootstrap-contract build complete.

Manual regression flow:
1) Enable board in config:
   setvar zorro-ppc

2) Launch emulator with clean runtime:
   unset LD_LIBRARY_PATH
   export QEMU_UAE_SO=/usr/local/lib/qemu-uae.so

3) Optional AutoConfig probe trace for compatibility debugging:
   export PPC_ACCEL_AC_TRACE=1

4) On Amiga shell, run regression commands:
   ppcshake --id
   ppcshake --irq
   ppcshake 10
   boardtype

Expected:
- ppcshake finds board at stable base (example: $00EC0000)
- --id prints contract-consistent register/shared-info values
- --irq reports: IRQ test OK: doorbell raise/ack and cmd_done raise/ack.
- repeated ppcshake runs complete without lockups/bus errors

Notes:
- boardtype "BlizzardPPC version 0" is currently treated as non-blocking
  until full classic firmware/flash compatibility is implemented.
- Disable probe trace for normal runs:
  unset PPC_ACCEL_AC_TRACE
EOF
