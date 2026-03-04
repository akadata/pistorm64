mkdir -p build
make -j4 emulator

make -C amiga/zorro-ppc

cat <<'EOF'
Stage 6A build complete.

Manual test steps:
1) Enable the board in your PiStorm Amiga config:
   setvar zorro-ppc
   (alias: setvar ppc-accel)

2) Start emulator with that config and boot AmigaOS.

3) Copy/run the handshake tool from Amiga shell:
   ppcshake
   # or:
   ppcshake 10

Expected output:
- "PPC accel found at $........"
- "TIME32[0] = $........ (...)"
- Exit code 0
EOF
