/*
 * Kernel Module GPIO Compatibility Header
 * This header provides the same interface as the original ps_protocol.h
 * but intercepts direct memory access patterns with function calls
 */

#ifndef PS_PROTOCOL_H
#define PS_PROTOCOL_H

#include <stdint.h>

// Define the same PIN constants as in the original ps_protocol.h
#define PIN_TXN_IN_PROGRESS 0
#define PIN_IPL_ZERO 1
#define PIN_A0 2
#define PIN_A1 3
#define PIN_CLK 4
#define PIN_RESET 5
#define PIN_RD 6
#define PIN_WR 7
#define PIN_D(x) (8 + x)

#define REG_DATA 0
#define REG_ADDR_LO 1
#define REG_ADDR_HI 2
#define REG_STATUS 3

#define STATUS_BIT_INIT 1
#define STATUS_BIT_RESET 2

#define STATUS_MASK_IPL 0xe000
#define STATUS_SHIFT_IPL 13

// Define register addresses
#define GPIO_ADDR 0x200000
#define GPCLK_ADDR 0x101000

#define GPIO_BASE (0x3F000000 + 0x200000) /* GPIO controller */
#define GPCLK_BASE (0x3F000000 + 0x101000)

#define CLK_PASSWD 0x5a000000
#define CLK_GP0_CTL 0x070
#define CLK_GP0_DIV 0x074

// Define the same functions as in the original
unsigned int ps_read_8(unsigned int address);
unsigned int ps_read_16(unsigned int address);
unsigned int ps_read_32(unsigned int address);
void ps_write_8(unsigned int address, unsigned int data);
void ps_write_16(unsigned int address, unsigned int data);
void ps_write_32(unsigned int address, unsigned int data);
unsigned int ps_read_status_reg(void);
void ps_write_status_reg(unsigned int value);
void ps_setup_protocol(void);
void ps_reset_state_machine(void);
void ps_pulse_reset(void);
unsigned int ps_get_ipl_zero(void);

// Define macros that map direct memory access to function calls when using kernel module
#ifdef PISTORM_KMOD

// Redefine the global gpio pointer to be a function that intercepts access
#define gpio ((volatile unsigned int*)0xDEADBEEF)  // Dummy address

// Define macros to intercept *(gpio + offset) patterns
#define GPIOREG_ACCESS(offset) ps_gpio_direct_access(offset)

// Function to handle direct GPIO register access
unsigned int ps_gpio_direct_access(int offset);

#else

// Original implementation for /dev/mem access
extern volatile unsigned int* gpio;

// GPIO setup macros for original implementation
#define INP_GPIO(g) *(gpio + ((g) / 10)) &= ~(7 << (((g) % 10) * 3))
#define OUT_GPIO(g) *(gpio + ((g) / 10)) |= (1 << (((g) % 10) * 3))
#define SET_GPIO_ALT(g, a)  \
  *(gpio + (((g) / 10))) |= \
      (((a) <= 3 ? (a) + 4 : (a) == 4 ? 3 : 2) << (((g) % 10) * 3))

#define GPIO_PULL *(gpio + 37)      // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio + 38)  // Pull up/pull down clock

#endif

// Define the same aliases as the original
#define read8 ps_read_8
#define read16 ps_read_16
#define read32 ps_read_32

#define write8 ps_write_8
#define write16 ps_write_16
#define write32 ps_write_32

#define write_reg ps_write_status_reg
#define read_reg ps_read_status_reg

#define gpio_get_irq ps_get_ipl_zero

#endif /* PS_PROTOCOL_H */