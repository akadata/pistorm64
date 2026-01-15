// src/gpio/ps_protocol_kmod.h - Header for kernel module backend
#ifndef PS_PROTOCOL_H
#define PS_PROTOCOL_H

#include <stdint.h>

// Define the same interface as the original ps_protocol.h
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

// Define the same macros as the original
#define read8 ps_read_8
#define read16 ps_read_16
#define read32 ps_read_32

#define write8 ps_write_8
#define write16 ps_write_16
#define write32 ps_write_32

#define write_reg ps_write_status_reg
#define read_reg ps_read_status_reg

#define gpio_get_irq ps_get_ipl_zero

// For compatibility with code that expects a gpio pointer
// We'll define a dummy pointer and intercept its usage
extern volatile unsigned int* gpio;

// Define the same PIN constants as in the original
#define PIN_TXN_IN_PROGRESS 0
#define PIN_IPL_ZERO 1
#define PIN_A0 2
#define PIN_A1 3
#define PIN_CLK 4
#define PIN_RESET 5
#define PIN_RD 6
#define PIN_WR 7
#define PIN_D(x) (8 + x)

#endif