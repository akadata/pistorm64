#!/bin/bash
#
# cryptodad
# Jan 2023
# 
# Argument added for PiStorm latch type
# L373A and L374
# BLITTER
# Syntax: makepi4 <CPLD> <revision>
#
# Modified for PiStorm64 FC Enhancement
# This script should be executed via SSH on homer
#

# Check for command line argument
if [ $# -eq 0 ]; then
    echo "Missing argument: expected EPM240 or EPM570"
    echo "Syntax: $0 <CPLD>"
    exit 1
fi

CPLD=$1

# Correct path for quartus on homer
QUARTUS_BIN_PATH="/opt/intelFPGA/20.1/quartus/bin"

# Check if quartus tools exist
if [ ! -f "$QUARTUS_BIN_PATH/quartus_map" ]; then
    echo "ERROR: Quartus tools not found at $QUARTUS_BIN_PATH"
    echo "Please verify Quartus installation on homer"
    exit 1
fi

# Define remote connection parameters
WIFI="192.168.0.99"
piaddress="$WIFI"
STREAMHOME="dev/ATARI/pistorm-atari"

# Execute quartus_map command
echo "Running quartus_map for ${CPLD}_enhanced..."
"$QUARTUS_BIN_PATH/quartus_map" "pistormsxb_dev${CPLD}_enhanced"
if [ $? -ne 0 ]; then
    echo "ERROR in quartus_map"
    exit 1
fi

# Continue with compilation
BITSTREAM="${CPLD}_bitstream_dev_enhanced"
DST_BITSTREAM="${CPLD}_bitstream.svf"

echo "Compiling with quartus_sh..."
"$QUARTUS_BIN_PATH/quartus_sh" --flow compile "pistormsxb_dev${CPLD}_enhanced"
if [ $? -ne 0 ]; then
    echo "ERROR COMPILE"
    exit 1
fi

echo "Generating SVF file..."
"$QUARTUS_BIN_PATH/quartus_cpf" -c -q 100KHz -g 3.3 -n p "output_files/pistormsxb_dev${CPLD}_enhanced.pof" "${BITSTREAM}.svf"
if [ $? -ne 0 ]; then
    echo "ERROR SVF"
    exit 1
fi

echo "Transferring SVF file to Pi..."
scp "${BITSTREAM}.svf" pi@"$piaddress":"$STREAMHOME/rtl/$DST_BITSTREAM"
if [ $? -ne 0 ]; then
    echo "ERROR SCP"
    exit 1
fi

echo "Programming CPLD..."
ssh pi@"$piaddress" "cd $STREAMHOME && ./flash.sh"
if [ $? -ne 0 ]; then
    echo "ERROR PROGRAMMING"
    exit 1
fi

echo "Compilation and programming completed successfully!"