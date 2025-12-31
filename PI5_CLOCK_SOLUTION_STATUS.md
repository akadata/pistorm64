# PiStorm Pi5 200MHz Clock Solution - Status Report

## Current Status

✅ **GPIO Line Naming**: SUCCESS - Applied overlay successfully maps GPIO lines to PiStorm names:
- GPIO0 = pistorm:PI_TXN_IN_PROGRESS
- GPIO1 = pistorm:PI_IPL_ZERO  
- GPIO2 = pistorm:PI_A0
- GPIO3 = pistorm:PI_A1
- GPIO4 = pistorm:PI_CLK (GPCLK0)
- GPIO5 = pistorm:PI_RESET
- GPIO6 = pistorm:PI_RD
- GPIO7 = pistorm:PI_WR
- GPIO8-23 = pistorm:PI_D0 through pistorm:PI_D15

✅ **Clock Generation**: SUCCESS - GPCLK0 overlay provides 100MHz clock (kernel-limited from requested 200MHz)

✅ **Emulator Communication**: PARTIAL - Clock transitions detected but no proper bus communication

❌ **CPLD Communication**: FAILING - Status register still returns 0x0000, reset vectors invalid

## Analysis

The emulator shows:
- Clock transitions are active (clk_transitions_out=58/128, clk_transitions_in=62/128)
- Status register reads as 0x0000 (should be non-zero)
- Reset vectors are invalid (SP=0x00000000, PC=0x00000000)
- This indicates the CPLD is not responding properly to transactions

## Root Cause

The issue is that the PiStorm CPLD RTL was designed for 200MHz operation and may not function properly at 100MHz. The timing requirements of the CPLD state machine may not be met at 100MHz, causing it to not respond to bus transactions properly.

## Next Steps

### 1. External 200MHz Oscillator (Recommended)
- Connect external 200MHz oscillator to GPIO4
- Configure PiStorm to use external clock (PISTORM_ENABLE_GPCLK=0)
- This bypasses kernel frequency limitations entirely

### 2. Kernel GPCLK Divisor Fix
- Modify the GPCLK0 driver to allow div_int=1 (currently forced to div_int=2)
- This would restore the proper 200MHz (2000MHz/10 with divisor 1) instead of 100MHz (2000MHz/10 with divisor 2)

### 3. CPLD Timing Analysis
- Review the pistorm.v RTL to understand exact timing requirements
- Determine if 100MHz is fundamentally insufficient or just suboptimal

## Files Created

- `/home/smalley/pistorm/pi5/pistorm/pistorm.dtbo` - GPIO line naming overlay
- `/home/smalley/pistorm/pi5/pistorm/pistorm.dtso` - GPIO line naming overlay source

## Overlay Commands

Load: `sudo dtoverlay -d ./pi5/pistorm pistorm`
Remove: `sudo dtoverlay -r pistorm`

## Current Working Configuration

The system now has:
- Properly named GPIO lines matching PiStorm CPLD interface
- 100MHz clock from GPCLK0 (kernel-limited)
- Emulator running without crashes
- But no proper bus communication with Amiga hardware

## Conclusion

The GPIO naming overlay is working perfectly. The next step is to address the fundamental clock frequency issue. Either an external 200MHz oscillator or a kernel GPCLK driver modification is needed to achieve the required 200MHz for proper CPLD operation.