/*
 * Kernel Module GPIO Compatibility Header
 * This header provides the same interface as the original ps_protocol.h
 * but routes operations through the kernel module interface
 */

#ifndef KMODO_GPIO_COMPAT_H
#define KMODO_GPIO_COMPAT_H

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

// Define register addresses
#define REG_DATA 0
#define REG_ADDR_LO 1
#define REG_ADDR_HI 2
#define REG_STATUS 3

// Define GPIO register offsets
#define GPFSEL0 0x00
#define GPFSEL1 0x04
#define GPFSEL2 0x08
#define GPFSEL3 0x0c
#define GPFSEL4 0x10
#define GPFSEL5 0x14
#define GPSET0  0x1c
#define GPSET1  0x20
#define GPCLR0  0x28
#define GPCLR1  0x2c
#define GPLEV0  0x34
#define GPLEV1  0x38

// Define function select values
#define GPIO_INPUT  0
#define GPIO_OUTPUT 1
#define GPIO_ALT0   4
#define GPIO_ALT1   5
#define GPIO_ALT2   6
#define GPIO_ALT3   7
#define GPIO_ALT4   3
#define GPIO_ALT5   2

// Define register access macros that route through kernel module
#define write_reg(address, value) ps_write_16(address, value)
#define read_reg(address) ps_read_16(address)

// Define the same functions as in the original ps_protocol.h
unsigned ps_read_8(unsigned int address);
unsigned ps_read_16(unsigned int address);
unsigned ps_read_32(unsigned int address);
void ps_write_8(unsigned int address, unsigned int data);
void ps_write_16(unsigned int address, unsigned int data);
void ps_write_32(unsigned int address, unsigned int data);
unsigned ps_read_status_reg(void);
void ps_write_status_reg(unsigned int value);
int ps_setup_protocol(void);
int ps_reset_state_machine(void);
int ps_pulse_reset(void);
unsigned ps_get_ipl_zero(void);

// Define a compatibility macro for direct register access
// This maps *(gpio + offset) operations to appropriate functions
#define GPIO_ACCESS(offset) gpio_access_wrapper(offset)

// Wrapper function to handle *(gpio + offset) access pattern
unsigned int gpio_access_wrapper(int offset);

#endif