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
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "ps_protocol.h"
#include "rpi_peri.h"
#include "m68k.h"

volatile unsigned int* gpio;
static volatile uint32_t* cm;
static uint32_t gpio_page_off;
static uint32_t cm_page_off;
static size_t map_len = 0x1000;
static int mem_fd = -1;
static volatile uint32_t* gpio_map_base;
static volatile uint32_t* cm_map_base;
static uint32_t saved_gp0ctl;
static uint32_t saved_gp0div;
static uint32_t saved_gpfsel0;
static int cleanup_registered;

unsigned int gpfsel0;
unsigned int gpfsel1;
unsigned int gpfsel2;

unsigned int gpfsel0_o;
unsigned int gpfsel1_o;
unsigned int gpfsel2_o;


#define NOP asm("nop"); asm("nop");

static inline volatile uint32_t* cm_reg(uint32_t off) {
  return (volatile uint32_t*)(((uint8_t*)cm) + off);
}

static inline volatile uint32_t* gpio_reg(uint32_t off) {
  return (volatile uint32_t*)(((uint8_t*)gpio) + off);
}

static int setup_io() {
  uint32_t peri = rpi_detect_peri_base();
  if (peri == 0) {
    fprintf(stderr, "[CLK] Warning: failed to detect PERI base; falling back to 0x3F000000\n");
    peri = 0x3F000000u;
  }

  if (!(peri == 0x20000000u || peri == 0x3F000000u || peri == 0xFE000000u)) {
    fprintf(stderr, "[CLK] Unsupported PERI base 0x%08x (likely Pi5/RP1)\n", peri);
    return -1;
  }

  uint32_t gpio_phys = peri + GPIO_OFFSET;
  uint32_t cm_phys = peri + CM_OFFSET;

  mem_fd = rpi_open_devmem();
  if (mem_fd < 0) {
    fprintf(stderr, "[CLK] Unable to open /dev/mem. Run as root using sudo?\n");
    return -1;
  }

  volatile uint32_t* gpio_map = rpi_map_block(mem_fd, gpio_phys, map_len, &gpio_page_off);
  if (gpio_map == (volatile uint32_t*)MAP_FAILED) {
    fprintf(stderr, "[CLK] mmap GPIO failed, errno=%d\n", errno);
    close(mem_fd);
    mem_fd = -1;
    return -1;
  }

  volatile uint32_t* cm_map = rpi_map_block(mem_fd, cm_phys, map_len, &cm_page_off);
  if (cm_map == (volatile uint32_t*)MAP_FAILED) {
    fprintf(stderr, "[CLK] mmap CM failed, errno=%d\n", errno);
    munmap((void*)gpio_map, map_len);
    close(mem_fd);
    mem_fd = -1;
    return -1;
  }

  close(mem_fd);
  mem_fd = -1;

  gpio_map_base = gpio_map;
  cm_map_base = cm_map;
  gpio = (volatile unsigned int*)(((uint8_t*)gpio_map) + gpio_page_off);
  cm = (volatile uint32_t*)(((uint8_t*)cm_map) + cm_page_off);

  saved_gp0ctl = *cm_reg(CLK_GP0_CTL);
  saved_gp0div = *cm_reg(CLK_GP0_DIV);
  saved_gpfsel0 = *gpio_reg(0x00u);

  printf("[CLK] PERI=0x%08x\n", peri);
  printf("[CLK] mmap gpio_map  = %p\n", (void*)gpio_map);
  printf("[CLK] mmap cm_map    = %p\n", (void*)cm_map);
  printf("[CLK] gpio ptr       = %p (GPIO)\n", (void*)gpio);
  printf("[CLK] cm ptr         = %p (CM)\n", (void*)cm);
  printf("[CLK] GP0CTL reg ptr  = %p\n", (void*)cm_reg(CLK_GP0_CTL));
  printf("[CLK] GP0DIV reg ptr  = %p\n", (void*)cm_reg(CLK_GP0_DIV));

  return 0;

}

#define GPCLK_CTL_BUSY (1u << 7)
#define GPCLK_CTL_ENAB (1u << 4)
#define GPCLK_CTL_KILL (1u << 5)
#define GPCLK_CTL_SRC_MASK 0xFu
#define GPCLK_SRC_PLLD 6u

static int wait_busy(volatile uint32_t* ctl, int want_busy, int timeout_us) {
  struct timespec t0;
  struct timespec tn;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (;;) {
    uint32_t v = *ctl;
    int busy = !!(v & GPCLK_CTL_BUSY);
    if (busy == want_busy) return 0;

    clock_gettime(CLOCK_MONOTONIC, &tn);
    long us = (tn.tv_sec - t0.tv_sec) * 1000000L + (tn.tv_nsec - t0.tv_nsec) / 1000L;
    if (us >= timeout_us) return -1;

    struct timespec sl = { .tv_sec = 0, .tv_nsec = 20000 };
    nanosleep(&sl, NULL);
  }
}

static uint32_t cm_make_div(double d) {
  if (d < 1.0) d = 1.0;
  if (d > 4095.999) d = 4095.999;
  uint32_t divi = (uint32_t)d;
  uint32_t divf = (uint32_t)((d - (double)divi) * 4096.0 + 0.5);
  if (divf >= 4096) {
    divf = 0;
    divi++;
  }
  return (divi << 12) | (divf & 0xFFF);
}

static uint32_t try_read_plld_hz_from_debugfs(void) {
  const char* paths[] = {
    "/sys/kernel/debug/clk/plld/clk_rate",
    "/sys/kernel/debug/clk/plld_core/clk_rate",
    "/sys/kernel/debug/clk/plld_per/clk_rate",
  };

  for (size_t i = 0; i < (sizeof(paths) / sizeof(paths[0])); i++) {
    int fd = open(paths[i], O_RDONLY);
    if (fd < 0) continue;
    char buf[64] = {0};
    ssize_t rd = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (rd <= 0) continue;
    unsigned long long hz = strtoull(buf, NULL, 10);
    if (hz > 1000000ull && hz < 2000000000ull) return (uint32_t)hz;
  }
  return 0;
}

static void setup_gpclk() {
  const uint32_t target_hz = 200000000u;
  volatile uint32_t* gp0ctl = cm_reg(CLK_GP0_CTL);
  volatile uint32_t* gp0div = cm_reg(CLK_GP0_DIV);

  uint32_t plld_hz = try_read_plld_hz_from_debugfs();
  uint32_t src_hz = plld_hz ? plld_hz : 500000000u;

  if (!plld_hz) {
    fprintf(stderr, "[CLK] PLLD rate not found; assuming %u Hz\n", src_hz);
  } else {
    fprintf(stderr, "[CLK] PLLD rate: %u Hz\n", src_hz);
  }

  double div = (double)src_hz / (double)target_hz;
  uint32_t div_reg = cm_make_div(div);

  uint32_t ctl = *gp0ctl;
  *gp0ctl = CLK_PASSWD | (ctl & ~GPCLK_CTL_ENAB);
  if (wait_busy(gp0ctl, 0, 2000) < 0) {
    fprintf(stderr, "[CLK] Warning: GPCLK0 BUSY did not clear (continuing)\n");
  }

  *gp0div = CLK_PASSWD | div_reg;

  uint32_t newctl = *gp0ctl;
  newctl &= ~GPCLK_CTL_SRC_MASK;
  newctl |= (GPCLK_SRC_PLLD & GPCLK_CTL_SRC_MASK);
  newctl |= GPCLK_CTL_ENAB;
  *gp0ctl = CLK_PASSWD | newctl;

  if (wait_busy(gp0ctl, 1, 2000) < 0) {
    fprintf(stderr, "[CLK] Warning: GPCLK0 BUSY did not assert (continuing)\n");
  }

  uint32_t rd_ctl = *gp0ctl;
  uint32_t rd_div = *gp0div;
  uint32_t rd_divi = (rd_div >> 12) & 0xFFF;
  uint32_t rd_divf = rd_div & 0xFFF;
  double rd_div_total = (double)rd_divi + ((double)rd_divf / 4096.0);
  double actual_hz = src_hz / rd_div_total;

  if ((rd_ctl & GPCLK_CTL_SRC_MASK) != GPCLK_SRC_PLLD) {
    fprintf(stderr, "[CLK] Warning: GPCLK0 SRC=%u (expected %u)\n",
            rd_ctl & GPCLK_CTL_SRC_MASK, GPCLK_SRC_PLLD);
  }

  fprintf(stderr,
          "[CLK] GP0CTL=0x%08X GP0DIV=0x%08X (target %u Hz, ~%.1f MHz)\n",
          rd_ctl, rd_div, target_hz, actual_hz / 1000000.0);

  // Enable 200MHz CLK output on GPIO4
  printf("setup_gpclk PIN_CLK, = %d\n", PIN_CLK);
  SET_GPIO_ALT(PIN_CLK, 0); // gpclk0

  uint32_t fsel0 = *gpio_reg(0x00u);
  uint32_t gpio4_f = (fsel0 >> 12) & 0x7;
  if (gpio4_f != 4) {
    fprintf(stderr,
            "[CLK] Warning: GPIO4 not ALT0 after setup (GPFSEL0=0x%08X gpio4_f=%u)\n",
            fsel0, gpio4_f);
  }
}

void ps_cleanup_protocol() {
  if (cm) {
    volatile uint32_t* gp0ctl = cm_reg(CLK_GP0_CTL);
    volatile uint32_t* gp0div = cm_reg(CLK_GP0_DIV);

    uint32_t ctl = *gp0ctl;
    *gp0ctl = CLK_PASSWD | (ctl & ~GPCLK_CTL_ENAB) | GPCLK_CTL_KILL;
    if (wait_busy(gp0ctl, 0, 2000) < 0) {
      fprintf(stderr, "[CLK] Warning: GPCLK0 BUSY did not clear on shutdown\n");
    }

    *gp0div = CLK_PASSWD | saved_gp0div;
    *gp0ctl = CLK_PASSWD | (saved_gp0ctl & ~GPCLK_CTL_KILL);
  }

  if (gpio) {
    uint32_t fsel0 = *gpio_reg(0x00u);
    fsel0 &= ~(0x7u << 12);
    fsel0 |= (saved_gpfsel0 & (0x7u << 12));
    *gpio_reg(0x00u) = fsel0;
  }

  if (gpio_map_base && gpio_map_base != (volatile uint32_t*)MAP_FAILED) {
    munmap((void*)gpio_map_base, map_len);
  }
  if (cm_map_base && cm_map_base != (volatile uint32_t*)MAP_FAILED) {
    munmap((void*)cm_map_base, map_len);
  }
  gpio_map_base = NULL;
  cm_map_base = NULL;
  gpio = NULL;
  cm = NULL;
}

void ps_setup_protocol()  {
  if (setup_io() < 0) {
    exit(-1);
  }
  if (!cleanup_registered) {
    atexit(ps_cleanup_protocol);
    cleanup_registered = 1;
  }
  setup_gpclk();
  usleep(5000);

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

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {
  }
}

void ps_write_8(unsigned int address, unsigned int data) {
  if ((address & 1) == 0)
    data = data + (data << 8); // EVEN, A0=0,UDS
  else
    data = data & 0xff; // ODD , A0=1,LDS

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

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {
  }
}

void ps_write_32(unsigned int address, unsigned int value) {
  ps_write_16(address, value >> 16);
  ps_write_16(address + 2, value);
}

#define NOP  asm("nop"); asm("nop");

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

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {
  }
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

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {
  }
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;

  value = (value >> 8) & 0xffff;

  if ((address & 1) == 0)
    return (value >> 8) & 0xff; // EVEN, A0=0,UDS
  else
    return value & 0xff; // ODD , A0=1,LDS
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
  while ((value = *(gpio + 13)) & (1 << PIN_TXN_IN_PROGRESS)) {
  }

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
  while ((value = *(gpio + 13)) & (1 << PIN_TXN_IN_PROGRESS)) {
  }
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
