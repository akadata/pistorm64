# PiStorm Pi5 Overlay Documentation

## Overview
This document describes the device tree overlays created to enable PiStorm operation on Raspberry Pi 5.

## Problem Statement
The Raspberry Pi 5 kernel enforces a 100MHz limit on GPCLK0, and the GPIO pins need to be properly configured for PiStorm operation.

## Solution Components

### 1. pistorm-gpclk0-overlay.dts
This overlay configures the GPCLK0 clock source on GPIO4:

```dts
// Sets up GPCLK0 on GPIO4 with requested frequency
fragment@0 {
    target = <&gpio4>;
    __overlay__ {
        function = "gpclk0";
        drive-push-pull;
        bias-pull-up;
    };
};

fragment@1 {
    target-path = "/";
    __overlay__ {
        pistorm_gpclk0: pistorm-gpclk0 {
            compatible = "pistorm,gpclk0";
            #clock-cells = <0>;
            clock-frequency = <200000000>;  /* Request 200MHz (gets limited to 100MHz by kernel) */
            clocks = <&rp1_clocks 33>;  /* RP1_CLK_GP0 */
            clock-output-names = "pistorm_gpclk0";
        };
    };
};
```

### 2. pistorm-naming-overlay.dts
This overlay provides proper GPIO line naming to match PiStorm interface:

```dts
fragment@0 {
    target = <&{/axi/pcie@1000120000/rp1/gpio@d0000}>;
    __overlay__ {
        gpio-line-names = 
            /* GPIO0-7: PiStorm control signals */
            "pistorm:PI_TXN_IN_PROGRESS", /* GPIO0 */
            "pistorm:PI_IPL_ZERO",        /* GPIO1 */
            "pistorm:PI_A0",              /* GPIO2 */
            "pistorm:PI_A1",              /* GPIO3 */
            "pistorm:PI_CLK",             /* GPIO4 (GPCLK0) */
            "pistorm:PI_RESET",           /* GPIO5 */
            "pistorm:PI_RD",              /* GPIO6 */
            "pistorm:PI_WR",              /* GPIO7 */

            /* GPIO8-23: PiStorm data bus */
            "pistorm:PI_D0",  "pistorm:PI_D1",  "pistorm:PI_D2",  "pistorm:PI_D3",
            "pistorm:PI_D4",  "pistorm:PI_D5",  "pistorm:PI_D6",  "pistorm:PI_D7",
            "pistorm:PI_D8",  "pistorm:PI_D9",  "pistorm:PI_D10", "pistorm:PI_D11",
            "pistorm:PI_D12", "pistorm:PI_D13", "pistorm:PI_D14", "pistorm:PI_D15",

            /* GPIO24-27: Spare */
            "pistorm:GPIO24", "pistorm:GPIO25", "pistorm:GPIO26", "pistorm:GPIO27";
    };
};
```

## Installation

### Apply both overlays:
```bash
# Apply GPCLK0 clock overlay (first)
sudo dtoverlay -d ./pi5/gpclk0 pistorm-gpclk0 freq=200000000

# Apply GPIO naming overlay (second)
sudo dtoverlay -d ./pi5/pistorm pistorm
```

### Remove overlays:
```bash
# Remove in reverse order
sudo dtoverlay -r pistorm
sudo dtoverlay -r pistorm-gpclk0
```

## Verification

### Check overlay status:
```bash
sudo dtoverlay -l
```

### Verify GPIO4 configuration:
```bash
sudo pinctrl get 4
# Should show: 4: a0    pn | hi // pistorm:PI_CLK/GPIO4 = GPCLK0
```

### Monitor clock transitions:
```bash
sudo pinctrl poll 4
# Should show continuous hi/lo transitions
```

### Check clock summary:
```bash
sudo cat /sys/kernel/debug/clk/clk_summary | grep clk_gp0
# Should show clk_gp0 running at 100000000 Hz (kernel-limited from 200MHz request)
```

## Results

### ✅ GPIO4 Clock Output: CONFIRMED WORKING
- GPIO4 properly configured as GPCLK0 output (function select a0)
- Clock transitions actively running (verified with pinctrl poll)
- Shows 100MHz clock (kernel-limited from requested 200MHz)

### ✅ Device Tree Integration: COMPLETE
- Both overlays successfully applied
- GPIO lines properly named for PiStorm interface
- Emulator detects clock transitions

### ✅ Emulator Stability: ACHIEVED
- No more transaction timeout crashes
- Clock transitions properly detected
- Ready for external oscillator for full 200MHz operation

## Known Limitations

1. **Kernel GPCLK0 Limit**: Kernel enforces 100MHz limit on GPCLK0 (from requested 200MHz)
2. **CPLD Timing**: May require true 200MHz for proper operation

## Next Steps for Full 200MHz

1. **External Oscillator**: Connect 200MHz oscillator to GPIO4, disable GPCLK0 software setup
2. **Kernel Modification**: Modify GPCLK driver to allow divisor=1 for true 200MHz operation

## Files Created

- `pi5/gpclk0/pistorm-gpclk0-overlay.dts` - GPCLK0 clock setup overlay
- `pi5/gpclk0/pistorm-gpclk0.dtbo` - Compiled GPCLK0 overlay
- `pi5/pistorm/pistorm.dtso` - GPIO naming overlay source
- `pi5/pistorm/pistorm.dtbo` - Compiled GPIO naming overlay
- `tools/install_pistorm_gpclk.sh` - Installation script
- `tools/remove_pistorm_gpclk.sh` - Removal script
- `tools/check_rp1_gpio.sh` - GPIO verification script
- `tools/test_gpclk_frequencies.sh` - Frequency testing script
- `tools/explore_pio_alternative.sh` - PIO alternative exploration
- `PI5_CLOCK_SOLUTION_COMPLETE.md` - Complete solution documentation