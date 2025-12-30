# Raspberry Pi 5 Header Map for PiStorm

This document records the exact mapping between the Raspberry Pi 5 40-pin GPIO header and the PiStorm internal signal names, as derived from the PiStorm schematic and verified live on hardware.

This is the authoritative reference for Codex, firmware, kernel, and CPLD work.

---

## Conventions

* **Pin** = physical 40-pin header number
* **BCM** = Broadcom GPIO number / primary function
* **PiStorm Net** = signal name used inside the PiStorm schematic/CPLD

Notes:

* `NLT*` labels seen in some netlists (NLTMS, NLTDI, NLTCK) are net-tie aliases. Electrically they are the same signals as **TMS / TDI / TCK**.
* GPIO2/GPIO3 (I2C) are *not* used as I2C by PiStorm; they are repurposed as SA lines.
* **PICLK is GPCLK0 on GPIO4 (pin 7)** and is timing-critical.

---

## Header Mapping

### Odd-numbered pins (left column)

| Pin | BCM / Function | PiStorm Net |
| --- | -------------- | ----------- |
| 1   | 3V3            | 3V3         |
| 3   | GPIO2 / SDA1   | SA2         |
| 5   | GPIO3 / SCL1   | SA1         |
| 7   | GPIO4 / GPCLK0 | **PICLK**   |
| 9   | GND            | GND         |
| 11  | GPIO17         | SD9         |
| 13  | GPIO27         | TDI         |
| 15  | GPIO22         | SD14        |
| 17  | 3V3            | 3V3         |
| 19  | GPIO10 / MOSI  | SD2         |
| 21  | GPIO9 / MISO   | SD1         |
| 23  | GPIO11 / SCLK  | SD3         |
| 25  | GND            | GND         |
| 27  | GPIO0 / ID_SD  | AUX0        |
| 29  | GPIO5          | SA0         |
| 31  | GPIO6          | SOE         |
| 33  | GPIO13         | SD5         |
| 35  | GPIO19         | SD11        |
| 37  | GPIO26         | TCK         |
| 39  | GND            | GND         |

---

### Even-numbered pins (right column)

| Pin | BCM / Function | PiStorm Net |
| --- | -------------- | ----------- |
| 2   | 5V0            | 5V0         |
| 4   | 5V0            | 5V0         |
| 6   | GND            | GND         |
| 8   | GPIO14 / TXD0  | SD6         |
| 10  | GPIO15 / RXD0  | SD7         |
| 12  | GPIO18         | SD10        |
| 14  | GND            | GND         |
| 16  | GPIO23         | SD15        |
| 18  | GPIO24         | TMS         |
| 20  | GND            | GND         |
| 22  | GPIO25         | TDO         |
| 24  | GPIO8 / CE0    | SD0         |
| 26  | GPIO7 / CE1    | SWE         |
| 28  | GPIO1 / ID_SC  | AUX1        |
| 30  | GND            | GND         |
| 32  | GPIO12         | SD4         |
| 34  | GND            | GND         |
| 36  | GPIO16         | SD8         |
| 38  | GPIO20         | SD12        |
| 40  | GPIO21         | SD13        |

---

## Critical Notes

* **PICLK (GPIO4 / pin 7)** is driven via **GPCLK0** and has been validated from **1 MHz → 8 MHz** on real hardware.
* Any HAT or pogo-pin accessory must **not load or clamp pin 7**. Even passive contact can break high-speed operation.
* GPIO2/GPIO3 are safe to use as SA lines; no I2C traffic is expected or required.
* All SDx lines map directly into the CPLD banks shown in the schematic.

---

## Validation Status

* GPCLK0 verified via `pinctrl`, `clk_debug`, and `gpiomon`
* Frequencies tested: 1 MHz, 2 MHz, 8 MHz
* NVMe N07 HAT: do **not** assume GPIO4 is electrically isolated just because the HAT “doesn’t use it” logically; pogo/header contact can still load/clamp pin 7 unless physically isolated.

This file should be kept in sync with schematic revisions and CPLD pin assignments.
