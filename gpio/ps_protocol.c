// SPDX-License-Identifier: MIT

/*
  Original Copyright 2020 Claude Schwarz
  Code reorganized and rewritten by
  Niklas Ekström 2021 (https://github.com/niklasekstrom)
*/

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ps_protocol.h"
#include "m68k.h"

volatile unsigned int *gpio;
volatile unsigned int *gpclk;

unsigned int gpfsel0;
unsigned int gpfsel1;
unsigned int gpfsel2;

unsigned int gpfsel0_o;
unsigned int gpfsel1_o;
unsigned int gpfsel2_o;

#define NOP asm("nop"); asm("nop");

static uint32_t detect_peri_base() {
  uint32_t base = BCM2708_PERI_BASE; // default (Pi3/Zero2W)
  const char *ranges = "/proc/device-tree/soc/ranges";
  int fd = open(ranges, O_RDONLY);
  if (fd >= 0) {
    uint8_t buf[16] = {0};
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n >= 8) {
      // ranges: bus addr (4 bytes), cpu phys addr (4 bytes), size (4 bytes)...
      uint32_t candidate = be32toh(*(uint32_t *)&buf[4]);
      if (candidate != 0) {
        base = candidate;
      }
    }
  }
  return base;
}

static void setup_io() {
  int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd < 0) {
    printf("Unable to open /dev/mem. Run as root using sudo?\n");
    exit(-1);
  }

  uint32_t peri_base = detect_peri_base();

  void *gpio_map = mmap(
      NULL,                    // Any adddress in our space will do
      BCM2708_PERI_SIZE,       // Map length
      PROT_READ | PROT_WRITE,  // Enable reading & writting to mapped memory
      MAP_SHARED,              // Shared with other processes
      fd,                      // File to map
      peri_base                // Offset to GPIO peripheral
  );

  close(fd);

  if (gpio_map == MAP_FAILED) {
    printf("mmap failed, errno = %d\n", errno);
    exit(-1);
  }

  gpio = ((volatile unsigned *)gpio_map) + GPIO_ADDR / 4;
  gpclk = ((volatile unsigned *)gpio_map) + GPCLK_ADDR / 4;
}

static void setup_gpclk() {
  /* Program GPCLK0 for ~200MHz from PLLD (500MHz / 2.5). */
  const uint32_t ctl_idx = (CLK_GP0_CTL / 4);
  const uint32_t div_idx = (CLK_GP0_DIV / 4);

  /* Disable/kill and wait for BUSY to clear */
  *(gpclk + ctl_idx) = CLK_PASSWD | (1 << 5);
  usleep(10);
  while ((*(gpclk + ctl_idx)) & (1 << 7)) {
    usleep(10);
  }
  usleep(100);

  /* Divisor: 2.5 (2 + 0.5 fractional) -> 200MHz from 500MHz PLLD */
  uint32_t div = (2 << 12) | 0x800;
  *(gpclk + div_idx) = CLK_PASSWD | div;
  usleep(10);

  /* Enable: ENAB | src=PLLD (6) */
  *(gpclk + ctl_idx) = CLK_PASSWD | 6 | (1 << 4);
  usleep(10);

  /* Wait for BUSY to assert, log current CTL/DIV for diagnostics */
  int timeout = 1000;
  while (((*(gpclk + ctl_idx)) & (1 << 7)) == 0 && timeout--) {
    usleep(10);
  }
  uint32_t ctl = *(gpclk + ctl_idx);
  uint32_t div_rd = *(gpclk + div_idx);
  printf("[CLK] GP0CTL=0x%08X GP0DIV=0x%08X (target ~200MHz)\n", ctl, div_rd);

  // Enable 200MHz CLK output on GPIO4, adjust divider and pll source depending
  // on pi model
  SET_GPIO_ALT(PIN_CLK, 0);  // gpclk0
}

void ps_setup_protocol() {
  setup_io();
  setup_gpclk();

  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
}

void ps_write_16(unsigned int address, unsigned int data) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0000 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
}

void ps_write_8(unsigned int address, unsigned int data) {
  if ((address & 1) == 0)
    data = data + (data << 8);  // EVEN, A0=0,UDS
  else
    data = data & 0xff;  // ODD , A0=1,LDS

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0100 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
}

void ps_write_32(unsigned int address, unsigned int value) {
  ps_write_16(address, value >> 16);
  ps_write_16(address + 2, value);
}

#define NOP asm("nop"); asm("nop");

unsigned int ps_read_16(unsigned int address) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0200 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;

  return (value >> 8) & 0xffff;
}

unsigned int ps_read_8(unsigned int address) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;

  value = (value >> 8) & 0xffff;

  if ((address & 1) == 0)
    return (value >> 8) & 0xff;  // EVEN, A0=0,UDS
  else
    return value & 0xff;  // ODD , A0=1,LDS
}

unsigned int ps_read_32(unsigned int address) {
  return (ps_read_16(address) << 16) | ps_read_16(address + 2);
}

void ps_write_status_reg(unsigned int value) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((value & 0xffff) << 8) | (REG_STATUS << PIN_A0);

  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 7) = 1 << PIN_WR; // delay
#ifdef CHIP_FASTPATH
  *(gpio + 7) = 1 << PIN_WR; // delay 210810
#endif
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
}

unsigned int ps_read_status_reg() {
  *(gpio + 7) = (REG_STATUS << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;
  *(gpio + 7) = 1 << PIN_RD;
  *(gpio + 7) = 1 << PIN_RD;
  *(gpio + 7) = 1 << PIN_RD;
#ifdef CHIP_FASTPATH
  *(gpio + 7) = 1 << PIN_RD; // delay 210810
  *(gpio + 7) = 1 << PIN_RD; // delay 210810
#endif

  unsigned int value = *(gpio + 13);
  while ((value=*(gpio + 13)) & (1 << PIN_TXN_IN_PROGRESS)) {}
  
  *(gpio + 10) = 0xffffec;

  return (value >> 8) & 0xffff;
}

void ps_reset_state_machine() {
  // Set INIT high to disable the CPLD state machine while mapping IO
  ps_write_status_reg(STATUS_BIT_INIT);
  usleep(1500);
  ps_write_status_reg(0);
  usleep(100);
}

void ps_pulse_reset() {
  ps_write_status_reg(0);
  usleep(100000);
  ps_write_status_reg(STATUS_BIT_RESET);
}

unsigned int ps_get_ipl_zero() {
  unsigned int value = *(gpio + 13);
  while ((value=*(gpio + 13)) & (1 << PIN_TXN_IN_PROGRESS)) {}
  return value & (1 << PIN_IPL_ZERO);
}

#define INT2_ENABLED 1

void ps_update_irq() {
  unsigned int ipl = 0;

  if (!ps_get_ipl_zero()) {
    unsigned int status = ps_read_status_reg();
    ipl = (status & 0xe000) >> 13;
  }

  /*if (ipl < 2 && INT2_ENABLED && emu_int2_req()) {
    ipl = 2;
  }*/

  m68k_set_irq(ipl);
}
