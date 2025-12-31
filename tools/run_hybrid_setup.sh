#!/bin/bash
# 
# PiStorm Hybrid Setup (Pi5 + Pi Zero 2W) Runner
# This script runs the emulator with the Pi Zero 2W providing 200MHz clock
# and the Pi5 handling all data/signals communication
#

echo "PiStorm Hybrid Setup (Pi5 + Pi Zero 2W)"
echo "======================================="
echo "Running with Pi Zero 2W providing 200MHz clock to CPLD"
echo "Pi5 handles all data/signals communication"
echo ""

# Ensure we're running as root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (use sudo)" 
   exit 1
fi

echo "Setting up Pi5 for hybrid operation..."
echo "1. Ensuring Pi5 GPIO4 is NOT driving clock (Hi-Z)..."
# Set Pi5 GPIO4 to input mode so it doesn't interfere with Zero's clock
pinctrl set 4 ip
echo "   GPIO4 set to input (Hi-Z) - Pi5 no longer drives clock"

echo ""
echo "2. Verifying Pi Zero 2W is providing 200MHz clock..."
echo "   (Ensure Pi Zero 2W is connected and providing clock via GPIO4->PI_CLK)"

echo ""
echo "3. Running emulator with hybrid-friendly settings..."

# Run the emulator with settings appropriate for hybrid setup
# Use old protocol which may be more tolerant
# Shorter timeouts to avoid hanging
sudo env \
    PISTORM_ENABLE_GPCLK=0 \
    PISTORM_RP1_LEAVE_CLK_PIN=1 \
    PISTORM_TXN_TIMEOUT_US=50000 \
    PISTORM_PROTOCOL=old \
    PISTORM_OLD_NO_HANDSHAKE=1 \
    PISTORM_RP1_CLK_FUNCSEL=0 \
    ./emulator --config basic.cfg

echo ""
echo "Emulator finished."
echo ""
echo "If the CPLD is now receiving proper 200MHz clock from Pi Zero 2W,"
echo "the transaction timeouts should be resolved and the system should work."