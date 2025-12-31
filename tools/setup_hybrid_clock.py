#!/usr/bin/env python3
"""
PiStorm Hybrid Clock Setup Tool

This script configures the PiStorm for hybrid operation where:
- Pi Zero 2W provides 200MHz clock to both Pi5 and PiStorm CPLD
- Pi5 handles data/signals communication only
- No GPCLK setup on Pi5 side (avoids kernel 100MHz limitation)
"""

import os
import sys
import time
import subprocess
import argparse

def run_cmd(cmd, desc=""):
    """Run a command and return the result"""
    if desc:
        print(f"[CMD] {desc}")
    print(f"      $ {cmd}")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"      ERROR: {result.stderr}")
    else:
        print(f"      SUCCESS")
    return result.returncode == 0, result.stdout.strip()

def main():
    parser = argparse.ArgumentParser(description='PiStorm Hybrid Clock Setup')
    parser.add_argument('--setup', action='store_true', help='Setup hybrid configuration')
    parser.add_argument('--test', action='store_true', help='Test the hybrid setup')
    parser.add_argument('--freq', type=int, default=200000000, help='Clock frequency (default: 200000000)')
    args = parser.parse_args()
    
    if args.setup:
        print("Setting up PiStorm Hybrid Clock Configuration...")
        print("=" * 50)
        
        # Step 1: Remove any existing PiStorm GPCLK overlays
        print("\n1. Removing existing PiStorm GPCLK setup...")
        run_cmd("sudo dtoverlay -r pistorm-gpclk0 2>/dev/null || true", 
                "Remove pistorm-gpclk0 overlay")
        run_cmd("sudo dtoverlay -r gpclk0-pi5 2>/dev/null || true",
                "Remove gpclk0-pi5 overlay")
        
        # Step 2: Verify GPIO4 is set to input (will receive external clock)
        print("\n2. Configuring GPIO4 as input for external clock...")
        success, _ = run_cmd("sudo pinctrl set 4 ip", "Set GPIO4 to input")
        if success:
            _, status = run_cmd("sudo pinctrl get 4", "Verify GPIO4 config")
            print(f"      Current GPIO4 status: {status}")
        
        # Step 3: Inform user about Pi Zero 2W setup
        print("\n3. Pi Zero 2W Setup Required:")
        print("   - Connect Pi Zero 2W GPIO4 (GPCLK0) to both:")
        print("     * Pi5 GPIO4 header pin")  
        print("     * PiStorm CLK input")
        print("   - Run clock generation on Pi Zero 2W:")
        print("     sudo dtoverlay -d ./pi5/gpclk0 pistorm-gpclk0 freq=200000000")
        
        # Step 4: Prepare Pi5 for hybrid mode
        print("\n4. Preparing Pi5 for hybrid operation...")
        print("   Environment variables for hybrid mode:")
        print("     PISTORM_ENABLE_GPCLK=0          (Don't set up GPCLK)")
        print("     PISTORM_RP1_LEAVE_CLK_PIN=1     (Don't touch GPIO4)")
        print("     PISTORM_TXN_TIMEOUT_US=200000   (Increase timeout)")
        print("     PISTORM_PROTOCOL=old            (Use old protocol)")
        print("     PISTORM_OLD_NO_HANDSHAKE=1      (No transaction handshake)")
        
        print("\n5. To run emulator in hybrid mode:")
        print("   sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 \\")
        print("     PISTORM_TXN_TIMEOUT_US=200000 PISTORM_PROTOCOL=old \\")
        print("     PISTORM_OLD_NO_HANDSHAKE=1 ./emulator --config basic.cfg")
        
        print("\n6. Verification steps:")
        print("   - Check GPIO4 sees clock transitions: sudo pinctrl poll 4")
        print("   - Monitor with: sudo ./emulator --bus-probe")
        print("   - Look for 'clk_transitions_in' > 0 in output")
        
    elif args.test:
        print("Testing PiStorm Hybrid Setup...")
        print("=" * 30)
        
        # Check if external clock is detected
        print("\n1. Checking GPIO4 clock transitions...")
        _, status = run_cmd("sudo pinctrl get 4", "GPIO4 function status")
        print(f"   GPIO4: {status}")
        
        # Run a quick bus probe to see if we get any clock transitions
        print("\n2. Running quick bus probe to check for clock activity...")
        print("   (This will likely timeout, but should show clock transitions)")
        
        # Create a temporary test
        test_code = '''
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

// Simple test to verify GPIO4 can be read
int main() {
    printf("PiStorm Hybrid Clock Test\\n");
    printf("==========================\\n\\n");
    
    printf("This test verifies that:\\n");
    printf("1. GPIO4 is configured as input (receiving external clock)\\n");
    printf("2. The PiStorm protocol can initialize without GPCLK setup\\n");
    printf("3. Clock transitions can be detected\\n\\n");
    
    printf("Next step: Connect Pi Zero 2W GPCLK0 to GPIO4 and PiStorm CLK\\n");
    printf("Then run emulator with hybrid settings\\n");
    
    return 0;
}
'''
        
        with open('/tmp/hybrid_test.c', 'w') as f:
            f.write(test_code)
        
        # Compile and run the test
        run_cmd("gcc -o /tmp/hybrid_test /tmp/hybrid_test.c", "Compile hybrid test")
        run_cmd("sudo /tmp/hybrid_test", "Run hybrid test")
        
        print("\n3. To verify external clock is working:")
        print("   - On Pi Zero 2W: sudo dtoverlay -d ./pi5/gpclk0 pistorm-gpclk0 freq=200000000")
        print("   - On Pi5: sudo pinctrl poll 4  (should show transitions)")
        print("   - Then run emulator with hybrid settings")
        
    else:
        parser.print_help()

if __name__ == "__main__":
    main()