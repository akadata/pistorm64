// src/gpio/ps_protocol.h - Updated header with conditional compilation
#ifndef _PS_PROTOCOL_H
#define _PS_PROTOCOL_H

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

// Define BCM2708_PERI_BASE based on platform
#define BCM2708_PERI_BASE 0x3F000000  // pi3 / pi zero w2

#define BCM2708_PERI_SIZE 0x01000000

#define GPIO_ADDR 0x200000 /* GPIO controller */
#define GPCLK_ADDR 0x101000

#define GPIO_BASE (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */
#define GPCLK_BASE (BCM2708_PERI_BASE + 0x101000)

#define CLK_PASSWD 0x5a000000
#define CLK_GP0_CTL 0x070
#define CLK_GP0_DIV 0x074

#ifdef PISTORM_KMOD

// Kernel module backend implementation
// Use function calls instead of direct memory access

// Define a dummy gpio pointer to satisfy external declarations in emulator.c
extern volatile unsigned int* gpio;

// Redefine the GPIO access macros to use function calls
#define INP_GPIO(g) ps_gpio_set_input(g)
#define OUT_GPIO(g) ps_gpio_set_output(g)
#define SET_GPIO_ALT(g, a) ps_gpio_set_alt(g, a)

#define GPIO_PULL ps_gpio_pull_read()
#define GPIO_PULLCLK0 ps_gpio_pullclk0_read()

#define GPFSEL0_INPUT 0x0024c240
#define GPFSEL1_INPUT 0x00000000
#define GPFSEL2_INPUT 0x00000000

#define GPFSEL0_OUTPUT 0x0924c240
#define GPFSEL1_OUTPUT 0x09249249
#define GPFSEL2_OUTPUT 0x00000249

// Additional helper functions for GPIO operations
void ps_gpio_set_input(int gpio_pin);
void ps_gpio_set_output(int gpio_pin);
void ps_gpio_set_alt(int gpio_pin, int alt_func);
unsigned int ps_gpio_pull_read(void);
unsigned int ps_gpio_pullclk0_read(void);

#else

// Original /dev/mem implementation
// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or
// SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio + ((g) / 10)) &= ~(7 << (((g) % 10) * 3))
#define OUT_GPIO(g) *(gpio + ((g) / 10)) |= (1 << (((g) % 10) * 3))
#define SET_GPIO_ALT(g, a)  \
  *(gpio + (((g) / 10))) |= \
      (((a) <= 3 ? (a) + 4 : (a) == 4 ? 3 : 2) << (((g) % 10) * 3))

#define GPIO_PULL *(gpio + 37)      // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio + 38)  // Pull up/pull down clock

#define GPFSEL0_INPUT 0x0024c240
#define GPFSEL1_INPUT 0x00000000
#define GPFSEL2_INPUT 0x00000000

#define GPFSEL0_OUTPUT 0x0924c240
#define GPFSEL1_OUTPUT 0x09249249
#define GPFSEL2_OUTPUT 0x00000249

// Declare the global gpio pointer for the original implementation
extern volatile unsigned int* gpio;

#endif

// Common function declarations for both backends
unsigned int ps_read_8(unsigned int address);
unsigned int ps_read_16(unsigned int address);
unsigned int ps_read_32(unsigned int address);

void ps_write_8(unsigned int address, unsigned int data);
void ps_write_16(unsigned int address, unsigned int data);
void ps_write_32(unsigned int address, unsigned int data);

unsigned int ps_read_status_reg();
void ps_write_status_reg(unsigned int value);

void ps_setup_protocol();
void ps_reset_state_machine();
void ps_pulse_reset();

unsigned int ps_get_ipl_zero();

#define read8 ps_read_8
#define read16 ps_read_16
#define read32 ps_read_32

#define write8 ps_write_8
#define write16 ps_write_16
#define write32 ps_write_32

#define write_reg ps_write_status_reg
#define read_reg ps_read_status_reg

#define gpio_get_irq ps_get_ipl_zero

#endif /* _PS_PROTOCOL_H */