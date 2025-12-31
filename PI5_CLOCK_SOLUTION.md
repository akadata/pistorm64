# PiStorm 200MHz Clock Solution for Raspberry Pi5

## Summary of Findings

Based on the analysis of the RP1 peripherals document and testing, here's what we've discovered:

### Current Issue
- The Raspberry Pi5 kernel enforces a 100MHz cap on GPCLK0, preventing the PiStorm from achieving the required 200MHz clock
- Even when requesting 200MHz via device tree overlays, the kernel reports `rate=100000000`
- The external clock sweep test (50MHz, 100MHz, 150MHz, 200MHz) showed the CPLD returns status=0x0000, suggesting timing issues

### RP1 Peripherals Analysis
From the RP1 peripherals document, GPIO4 can function as:
- `a0` = `GPCLK[0]` (General-purpose clock 0) - Current approach, limited to 100MHz
- `a7` = `PIO[4]` (PIO state machine 4) - **Alternative approach!**

### Alternative Solutions

#### Solution 1: Custom PIO Firmware (Recommended)
The RP1 has PIO blocks identical to RP2040 with doubled FIFO depth, and one of the dual Cortex-M3 processors is currently spare. This allows for:

1. **Programming a PIO state machine** to generate precise 200MHz clock signal
2. **Direct control** without kernel limitations
3. **Hardware-level timing** that bypasses GPCLK0 restrictions

#### Solution 2: PWM-PIO Overlay (Partially Working)
- `sudo dtoverlay pwm-pio,gpio=4` configures GPIO4 as PIO function
- PWM can be configured via sysfs (`/sys/class/pwm/pwmchipX/pwmY/`)
- However, may not provide the precise 200MHz needed

#### Solution 3: System Clock Routing
- RP1 has Core PLL running at 2GHz (divided to 200MHz clk_sys)
- Audio PLL at 1.536GHz for audio clocks
- Could potentially route these through GPIO muxing

### Scripts Created

1. `tools/install_pistorm_gpclk.sh` - Install GPCLK overlay with frequency options
2. `tools/remove_pistorm_gpclk.sh` - Remove GPCLK overlay and module
3. `tools/check_rp1_gpio.sh` - Verify RP1 GPIO configuration
4. `tools/test_gpclk_frequencies.sh` - Test different clock frequencies
5. `tools/explore_pio_alternative.sh` - Analysis of PIO alternative

### Next Steps for 200MHz Solution

#### Immediate:
1. Use the existing GPCLK infrastructure with 100MHz (works but suboptimal)
2. Try external oscillator as backup option

#### Advanced:
1. **Develop custom PIO firmware** to generate 200MHz clock:
   - Write PIO assembly code for precise 200MHz square wave
   - Load firmware to one of RP1's Cortex-M3 processors
   - Configure PIO state machine to output on GPIO4 (function select a7)

2. **Verify CPLD timing requirements**:
   - The RTL expects `c200m = PI_CLK` but may work at lower frequencies
   - Consider modifying CPLD bitstream for 100MHz operation

### Key Insight
The HDMI clock runs at just over 200MHz, proving the hardware is capable of these frequencies - it's just the GPCLK0 driver that limits it to 100MHz. The PIO approach bypasses this limitation entirely.

### Recommended Implementation
The most promising approach is developing custom PIO firmware for the RP1 Cortex-M3 processor to drive GPIO4 with a precise 200MHz clock signal, bypassing the kernel GPCLK0 restrictions entirely.