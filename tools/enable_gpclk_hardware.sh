#!/bin/bash
# 
# PiStorm GPCLK0 200MHz Hardware Enable Script
# Uses the proven direct memory access method from the Raspberry Pi forums
#

echo "PiStorm GPCLK0 Hardware Clock Enable"
echo "===================================="
echo "This script enables the hardware GPCLK0 on GPIO4 using direct memory access"
echo "Address: 0x1f00018000 (RP1 clock manager)"
echo "Value: 0x1 (enables GPCLK0 output)"
echo ""

# Ensure we're running as root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (use sudo)" 
   exit 1
fi

echo "1. Setting GPIO4 to GPCLK0 function (ALT0)..."
pinctrl set 4 a0
if [ $? -eq 0 ]; then
    echo "   ✓ GPIO4 set to GPCLK0 function"
else
    echo "   ✗ Failed to set GPIO4 function"
    exit 1
fi

echo ""
echo "2. Enabling GPCLK0 hardware output via direct memory access..."
# This is the magic command from the forum that works
busybox devmem 0x1f00018000 32 0x1
if [ $? -eq 0 ]; then
    echo "   ✓ GPCLK0 hardware enabled"
else
    echo "   ✗ Failed to enable GPCLK0 hardware"
    exit 1
fi

echo ""
echo "3. Verifying GPIO4 configuration..."
pinctrl get 4
echo ""

echo "4. Testing clock transitions (this may take a few seconds)..."
timeout 2 pinctrl poll 4 > /tmp/pistorm_clock_test 2>&1 &
POLL_PID=$!
sleep 2
kill $POLL_PID 2>/dev/null

if [ -s /tmp/pistorm_clock_test ]; then
    LINES=$(wc -l < /tmp/pistorm_clock_test)
    if [ $LINES -gt 2 ]; then
        echo "   ✓ Clock transitions detected (sample output):"
        head -n 5 /tmp/pistorm_clock_test
    else
        echo "   ⚠ No clock transitions detected in short test"
    fi
else
    echo "   ⚠ No clock transitions detected"
fi

rm -f /tmp/pistorm_clock_test

echo ""
echo "GPCLK0 hardware enable completed!"
echo ""
echo "To run PiStorm with this clock:"
echo "  sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 \\"
echo "    PISTORM_TXN_TIMEOUT_US=200000 PISTORM_PROTOCOL=old PISTORM_OLD_NO_HANDSHAKE=1 \\"
echo "    ./emulator --config basic.cfg"
echo ""