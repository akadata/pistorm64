/*
 * GPIO compatibility layer for kernel module backend
 * This provides the same interface as the original /dev/mem implementation
 * but routes all operations through the kernel module interface
 */

#ifndef GPIO_COMPAT_H
#define GPIO_COMPAT_H

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
#define GPEDS0  0x40
#define GPEDS1  0x44

// Global gpio pointer - this will be a fake pointer for kernel module compatibility
extern volatile unsigned int* gpio;

// Function to initialize the compatibility layer
int init_gpio_compat_layer(void);

// Function to read from a GPIO register offset (simulating *(gpio + offset))
unsigned int gpio_read_reg(int reg_offset);

// Function to write to a GPIO register offset (simulating *(gpio + offset) = value)
void gpio_write_reg(int reg_offset, unsigned int value);

#endif