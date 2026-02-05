// SPDX-License-Identifier: MIT

/*
Code reorganized and rewritten by
Niklas Ekström 2021 (https://github.com/niklasekstrom)
Further stripped down and enhanced by Andrew Smalley https://github.com/akadata/pistorm64
Kernel Pistorm 64 now handles much of the ps_protocol bring up and communication with the pistorm device 
*/

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

// the status bit is not used now we have kernel pistorm
//#define STATUS_BIT_INIT 1
//#define STATUS_BIT_RESET 2

#define STATUS_MASK_IPL 0xe000
#define STATUS_SHIFT_IPL 13
#include <stdint.h>


uint8_t  ps_read_8 (uint32_t address);
uint16_t ps_read_16(uint32_t address);
uint32_t ps_read_32(uint32_t address);

void ps_write_8 (uint32_t address, uint8_t  data);
void ps_write_16(uint32_t address, uint16_t data);
void ps_write_32(uint32_t address, uint32_t data);

uint16_t ps_read_status_reg(void);
void     ps_write_status_reg(uint16_t value);

void ps_setup_protocol(void);
void ps_reset_state_machine(void);
void ps_pulse_reset(void);
void ps_protocol_dump_stats(void);

// Flush the batch queue of operations
int ps_flush_batch_queue(void);

// Helper function to flush before reads that need immediate results
static inline void ps_flush_before_read(void) {
#if PISTORM_ENABLE_BATCH
ps_flush_batch_queue();
#endif
}

unsigned int ps_get_ipl_zero(void);
unsigned int ps_gpio_lev(void);

#ifdef PISTORM_KMOD
void ps_fc_write(uint8_t fc);
#else
static inline void ps_fc_write(uint8_t fc) { (void)fc; }
#endif

#define read8 ps_read_8
#define read16 ps_read_16
#define read32 ps_read_32

#define write8 ps_write_8
#define write16 ps_write_16
#define write32 ps_write_32

#define write_reg ps_write_status_reg
#define read_reg ps_read_status_reg

#define gpio_get_irq ps_get_ipl_zero

static inline uint32_t read_long(uint32_t address) { return ps_read_32(address); }
static inline uint16_t read_word(uint32_t address) { return ps_read_16(address); }
static inline uint8_t read_byte(uint32_t address) { return ps_read_8(address); }

static inline void write_long(uint32_t address, uint32_t value) { ps_write_32(address, value); }
static inline void write_word(uint32_t address, uint16_t value) { ps_write_16(address, value); }
static inline void write_byte(uint32_t address, uint8_t value) { ps_write_8(address, value); }

#endif


/* _PS_PROTOCOL_H */
