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

#include "ps_protocol.h"
#include "m68k.h"

#ifdef HAVE_LIBGPIOD
#include <gpiod.h>
#endif

volatile unsigned int *gpio;
volatile unsigned int *gpclk;

unsigned int gpfsel0;
unsigned int gpfsel1;
unsigned int gpfsel2;

unsigned int gpfsel0_o;
unsigned int gpfsel1_o;
unsigned int gpfsel2_o;

#if defined(PISTORM_RP1)
static volatile uint32_t *rp1_io_bank0;
static volatile uint32_t *rp1_sys_rio0;
static size_t rp1_io_bank0_size;
static size_t rp1_sys_rio0_size;
static int rp1_mem_fd = -1;

static uint64_t be64(const uint8_t *p) {
  return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
         ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) | ((uint64_t)p[6] << 8) | ((uint64_t)p[7] << 0);
}

static int read_file_bytes(const char *path, uint8_t *buf, size_t len) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return -1;
  }
  ssize_t r = read(fd, buf, len);
  close(fd);
  return (r == (ssize_t)len) ? 0 : -1;
}

static int read_dt_string(const char *path, char *buf, size_t buf_len) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return -1;
  }
  ssize_t r = read(fd, buf, buf_len - 1);
  close(fd);
  if (r <= 0) {
    return -1;
  }
  buf[r] = '\0';
  return 0;
}

static int rp1_load_gpio_bases(uint64_t *io_bank0_addr, size_t *io_bank0_len, uint64_t *sys_rio0_addr,
                               size_t *sys_rio0_len) {
  char alias_path[256];
  if (read_dt_string("/proc/device-tree/aliases/gpio0", alias_path, sizeof(alias_path)) != 0) {
    return -1;
  }

  char reg_path[512];
  snprintf(reg_path, sizeof(reg_path), "/proc/device-tree%s/reg", alias_path);

  uint8_t reg[48];
  if (read_file_bytes(reg_path, reg, sizeof(reg)) != 0) {
    return -1;
  }

  uint64_t a0 = be64(&reg[0]);
  uint64_t s0 = be64(&reg[8]);
  uint64_t a1 = be64(&reg[16]);
  uint64_t s1 = be64(&reg[24]);

  if (!a0 || !a1 || !s0 || !s1) {
    return -1;
  }

  *io_bank0_addr = a0;
  *io_bank0_len = (size_t)s0;
  *sys_rio0_addr = a1;
  *sys_rio0_len = (size_t)s1;
  return 0;
}

static int rp1_map_blocks() {
  uint64_t io_addr = 0, rio_addr = 0;
  size_t io_len = 0, rio_len = 0;

  if (rp1_load_gpio_bases(&io_addr, &io_len, &rio_addr, &rio_len) != 0) {
    printf("RP1: failed to locate gpio0 device-tree reg.\n");
    return -1;
  }

  rp1_mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (rp1_mem_fd < 0) {
    perror("open(/dev/mem)");
    return -1;
  }

  void *io_map = mmap(NULL, io_len, PROT_READ | PROT_WRITE, MAP_SHARED, rp1_mem_fd, (off_t)io_addr);
  if (io_map == MAP_FAILED) {
    printf("RP1: mmap io_bank0 failed, errno=%d\n", errno);
    close(rp1_mem_fd);
    rp1_mem_fd = -1;
    return -1;
  }

  void *rio_map = mmap(NULL, rio_len, PROT_READ | PROT_WRITE, MAP_SHARED, rp1_mem_fd, (off_t)rio_addr);
  if (rio_map == MAP_FAILED) {
    printf("RP1: mmap sys_rio0 failed, errno=%d\n", errno);
    munmap(io_map, io_len);
    close(rp1_mem_fd);
    rp1_mem_fd = -1;
    return -1;
  }

  rp1_io_bank0 = (volatile uint32_t *)io_map;
  rp1_sys_rio0 = (volatile uint32_t *)rio_map;
  rp1_io_bank0_size = io_len;
  rp1_sys_rio0_size = rio_len;

  printf("RP1: mapped io_bank0 @ 0x%llx (len=0x%zx), sys_rio0 @ 0x%llx (len=0x%zx)\n",
         (unsigned long long)io_addr, io_len, (unsigned long long)rio_addr, rio_len);
  return 0;
}

static inline void rp1_dmb_oshst() {
  asm volatile("dmb oshst" ::: "memory");
}

static inline void rp1_dmb_osh() {
  asm volatile("dmb osh" ::: "memory");
}

static uint64_t rp1_now_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t rp1_txn_timeout_ns() {
  const char *env = getenv("PISTORM_TXN_TIMEOUT_US");
  uint64_t us = 500000;  // 500ms default; CPLD transactions should be far faster.
  if (env && *env) {
    char *end = NULL;
    unsigned long long v = strtoull(env, &end, 10);
    if (end != env && v > 0) {
      us = (uint64_t)v;
    }
  }
  return us * 1000ull;
}

// RP1 SYS_RIO0 register offsets (see RP1 peripherals doc section 3.3 + atomic alias scheme section 2.4).
// Layout matches the RP1 datasheet description: OUT, OE, NOSYNC_IN, SYNC_IN (4 registers).
#define RP1_RIO_OUT_OFF 0x000
#define RP1_RIO_OE_OFF 0x004
#define RP1_RIO_NOSYNC_IN_OFF 0x008
#define RP1_RIO_SYNC_IN_OFF 0x00c

#define RP1_ATOMIC_XOR 0x1000
#define RP1_ATOMIC_SET 0x2000
#define RP1_ATOMIC_CLR 0x3000

static inline void rp1_rio_out_set(uint32_t mask) {
  rp1_sys_rio0[(RP1_ATOMIC_SET + RP1_RIO_OUT_OFF) / 4] = mask;
}
static inline void rp1_rio_out_clr(uint32_t mask) {
  rp1_sys_rio0[(RP1_ATOMIC_CLR + RP1_RIO_OUT_OFF) / 4] = mask;
}
static inline void rp1_rio_oe_set(uint32_t mask) {
  rp1_sys_rio0[(RP1_ATOMIC_SET + RP1_RIO_OE_OFF) / 4] = mask;
}
static inline void rp1_rio_oe_clr(uint32_t mask) {
  rp1_sys_rio0[(RP1_ATOMIC_CLR + RP1_RIO_OE_OFF) / 4] = mask;
}
static inline uint32_t rp1_rio_sync_in() {
  // Ensure posted writes have time to reach RP1 before sampling.
  rp1_dmb_osh();
  return rp1_sys_rio0[RP1_RIO_SYNC_IN_OFF / 4];
}

static int rp1_wait_txn_clear(uint32_t *final_sync_in) {
  const uint64_t start = rp1_now_ns();
  const uint64_t deadline = start + rp1_txn_timeout_ns();
  uint32_t v = 0;
  for (;;) {
    v = rp1_rio_sync_in();
    if ((v & (1u << PIN_TXN_IN_PROGRESS)) == 0) {
      if (final_sync_in) {
        *final_sync_in = v;
      }
      return 0;
    }
    if (rp1_now_ns() > deadline) {
      if (final_sync_in) {
        *final_sync_in = v;
      }
      return -1;
    }
    asm volatile("yield");
  }
}

static void rp1_txn_timeout_fatal(const char *where) {
  printf("RP1: timeout waiting for CPLD transaction to complete (%s).\n", where ? where : "unknown");

  // Best-effort release of the protocol lines before exiting.
  const uint32_t ctrl_mask =
      (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RESET) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_out_clr(0xFFFFECu);
  rp1_rio_oe_clr(ctrl_mask | data_mask);

  ps_dump_protocol_state("txn-timeout");
  exit(1);
}

static inline uint32_t rp1_gpio_ctrl_index(unsigned int gpio_n) {
  return (0x004u + (gpio_n * 8u)) / 4u;
}

static void rp1_configure_sys_rio_funcsel() {
  // FUNCSEL encodes the 'aN' columns in the RP1 GPIO function table.
  // - a5 == SYS_RIO[n] (fast GPIO via SYS_RIO0)
  // - a0 == GPCLK[0] on GPIO4 (PiStorm clock output)
  const uint32_t funcsel_sys_rio = 5u;
  const uint32_t funcsel_gpclk0 = 0u;
  const uint32_t funcsel_mask = 0x1fu;
  for (unsigned int i = 0; i < 28; i++) {
    uint32_t idx = rp1_gpio_ctrl_index(i);
    uint32_t v = rp1_io_bank0[idx];
    const uint32_t funcsel = (i == PIN_CLK) ? funcsel_gpclk0 : funcsel_sys_rio;
    v = (v & ~funcsel_mask) | funcsel;
    rp1_io_bank0[idx] = v;
  }
  rp1_dmb_oshst();
}

static void setup_io_rp1() {
  if (rp1_map_blocks() != 0) {
    printf("RP1: GPIO MMIO setup failed.\n");
    exit(-1);
  }
  rp1_configure_sys_rio_funcsel();
}
#endif

static void setup_io() {
#if defined(PISTORM_RP1)
  setup_io_rp1();
  gpio = NULL;
  gpclk = NULL;
  return;
#else
  int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd < 0) {
    printf("Unable to open /dev/mem. Run as root using sudo?\n");
    exit(-1);
  }

  void *gpio_map = mmap(
      NULL,                    // Any address in our space will do
      BCM2708_PERI_SIZE,       // Map length
      PROT_READ | PROT_WRITE,  // Enable reading & writing to mapped memory
      MAP_SHARED,              // Shared with other processes
      fd,                      // File to map
      BCM2708_PERI_BASE        // Offset to GPIO peripheral
  );

  close(fd);

  if (gpio_map == MAP_FAILED) {
    printf("mmap failed, errno = %d\n", errno);
    exit(-1);
  }

  gpio = ((volatile unsigned *)gpio_map) + GPIO_ADDR / 4;
  gpclk = ((volatile unsigned *)gpio_map) + GPCLK_ADDR / 4;
#endif
}

static void setup_gpclk() {
#if defined(PISTORM_RP1)
  // TODO: PiStorm on Pi 5 needs an RP1/BCM2712 clock implementation for GPCLK on GPIO4.
  // For now, leave clock configuration to firmware/OS and continue without touching it.
  return;
#else
  // Enable 200MHz CLK output on GPIO4, adjust divider and pll source depending
  // on pi model
  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | (1 << 5);
  usleep(10);
  while ((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7))
    ;
  usleep(100);
  *(gpclk + (CLK_GP0_DIV / 4)) =
      CLK_PASSWD | (6 << 12);  // divider , 6=200MHz on pi3
  usleep(10);
  *(gpclk + (CLK_GP0_CTL / 4)) =
      CLK_PASSWD | 5 | (1 << 4);  // pll? 6=plld, 5=pllc
  usleep(10);
  while (((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7)) == 0)
    ;
  usleep(100);

  SET_GPIO_ALT(PIN_CLK, 0);  // gpclk0
#endif
}

int ps_probe_protocol() {
#ifdef HAVE_LIBGPIOD
  const char *chip_path = getenv("PISTORM_GPIOCHIP");
  if (!chip_path || !chip_path[0]) {
    chip_path = "/dev/gpiochip0";
  }

  struct gpiod_chip *chip = gpiod_chip_open(chip_path);
  if (!chip) {
    perror("gpiod_chip_open");
    return -1;
  }

  struct gpiod_chip_info *cinfo = gpiod_chip_get_info(chip);
  if (!cinfo) {
    perror("gpiod_chip_get_info");
    gpiod_chip_close(chip);
    return -1;
  }

  printf("GPIO probe: opened %s (%s, %zu lines)\n",
         gpiod_chip_get_path(chip),
         gpiod_chip_info_get_name(cinfo),
         gpiod_chip_info_get_num_lines(cinfo));

  unsigned int offsets[24];
  for (unsigned int i = 0; i < 24; i++) {
    offsets[i] = i;
  }

  struct gpiod_line_settings *settings = gpiod_line_settings_new();
  struct gpiod_line_config *line_cfg = gpiod_line_config_new();
  struct gpiod_request_config *req_cfg = gpiod_request_config_new();
  if (!settings || !line_cfg || !req_cfg) {
    printf("GPIO probe: failed to allocate libgpiod config objects.\n");
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    gpiod_chip_info_free(cinfo);
    gpiod_chip_close(chip);
    return -1;
  }

  (void)gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_AS_IS);
  gpiod_request_config_set_consumer(req_cfg, "pistorm-probe");

  if (gpiod_line_config_add_line_settings(line_cfg, offsets, 24, settings) != 0) {
    perror("gpiod_line_config_add_line_settings");
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    gpiod_chip_info_free(cinfo);
    gpiod_chip_close(chip);
    return -1;
  }

  struct gpiod_line_request *req = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
  if (!req) {
    perror("gpiod_chip_request_lines");
    printf("GPIO probe: could not request lines 0-23 on %s.\n", chip_path);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    gpiod_chip_info_free(cinfo);
    gpiod_chip_close(chip);
    return -1;
  }

  enum gpiod_line_value values[24];
  if (gpiod_line_request_get_values(req, values) != 0) {
    perror("gpiod_line_request_get_values");
    gpiod_line_request_release(req);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    gpiod_chip_info_free(cinfo);
    gpiod_chip_close(chip);
    return -1;
  }

  for (unsigned int i = 0; i < 24; i++) {
    struct gpiod_line_info *li = gpiod_chip_get_line_info(chip, i);
    const char *name = li ? gpiod_line_info_get_name(li) : NULL;
    printf("line %2u (%s): %d\n", i, name ? name : "-", values[i]);
    gpiod_line_info_free(li);
  }

  gpiod_line_request_release(req);
  gpiod_request_config_free(req_cfg);
  gpiod_line_config_free(line_cfg);
  gpiod_line_settings_free(settings);
  gpiod_chip_info_free(cinfo);
  gpiod_chip_close(chip);
  printf("GPIO probe OK (gpiochip backend).\n");
  return 0;
#else
  printf("GPIO probe unavailable: built without libgpiod (HAVE_LIBGPIOD not set).\n");
  return -1;
#endif
}

void ps_dump_protocol_state(const char *tag) {
  const char *t = tag ? tag : "state";
#if defined(PISTORM_RP1)
  if (!rp1_sys_rio0 || !rp1_io_bank0) {
    printf("[GPIO] %s: RP1 GPIO not mapped.\n", t);
    return;
  }

  const uint32_t sync = rp1_rio_sync_in();
  const uint32_t out = rp1_sys_rio0[RP1_RIO_OUT_OFF / 4];
  const uint32_t oe = rp1_sys_rio0[RP1_RIO_OE_OFF / 4];

  const unsigned int txn = (sync >> PIN_TXN_IN_PROGRESS) & 1u;
  const unsigned int ipl0 = (sync >> PIN_IPL_ZERO) & 1u;
  const unsigned int a0 = (sync >> PIN_A0) & 1u;
  const unsigned int a1 = (sync >> PIN_A1) & 1u;
  const unsigned int clk = (sync >> PIN_CLK) & 1u;
  const unsigned int rst = (sync >> PIN_RESET) & 1u;
  const unsigned int rd = (sync >> PIN_RD) & 1u;
  const unsigned int wr = (sync >> PIN_WR) & 1u;
  const unsigned int data = (sync >> 8) & 0xffffu;

  const uint32_t clk_ctrl = rp1_io_bank0[rp1_gpio_ctrl_index(PIN_CLK)];
  const unsigned int clk_funcsel = clk_ctrl & 0x1fu;

  unsigned int clk_transitions = 0;
  unsigned int last = clk;
  for (unsigned int i = 0; i < 128; i++) {
    unsigned int cur = (rp1_rio_sync_in() >> PIN_CLK) & 1u;
    if (cur != last) {
      clk_transitions++;
      last = cur;
    }
  }

  printf("[GPIO] %s: sync_in=0x%08x txn=%u ipl0=%u a0=%u a1=%u clk=%u rst=%u rd=%u wr=%u d=0x%04x | out=0x%08x oe=0x%08x | gpio4_funcsel=%u clk_transitions=%u/128\n",
         t, sync, txn, ipl0, a0, a1, clk, rst, rd, wr, data, out, oe, clk_funcsel, clk_transitions);
#else
  if (!gpio) {
    printf("[GPIO] %s: legacy GPIO not mapped.\n", t);
    return;
  }

  const unsigned int lev = *(gpio + 13);
  const unsigned int txn = (lev >> PIN_TXN_IN_PROGRESS) & 1u;
  const unsigned int ipl0 = (lev >> PIN_IPL_ZERO) & 1u;
  const unsigned int a0 = (lev >> PIN_A0) & 1u;
  const unsigned int a1 = (lev >> PIN_A1) & 1u;
  const unsigned int clk = (lev >> PIN_CLK) & 1u;
  const unsigned int rst = (lev >> PIN_RESET) & 1u;
  const unsigned int rd = (lev >> PIN_RD) & 1u;
  const unsigned int wr = (lev >> PIN_WR) & 1u;
  const unsigned int data = (lev >> 8) & 0xffffu;

  printf("[GPIO] %s: lev=0x%08x txn=%u ipl0=%u a0=%u a1=%u clk=%u rst=%u rd=%u wr=%u d=0x%04x\n",
         t, lev, txn, ipl0, a0, a1, clk, rst, rd, wr, data);
#endif
}

void ps_setup_protocol() {
  setup_io();
  setup_gpclk();

#if defined(PISTORM_RP1)
  // Inputs: PIN_TXN_IN_PROGRESS (0), PIN_IPL_ZERO (1)
  // Outputs: all other protocol pins; default to inputs on D[0..15] until needed.
  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RESET) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_oe_clr(ctrl_mask | data_mask);
  rp1_rio_out_clr(0xFFFFECu);
  rp1_rio_oe_set(ctrl_mask);
#else
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
#endif
}

void ps_write_16(unsigned int address, unsigned int data) {
#if defined(PISTORM_RP1)
  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RESET) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_oe_set(ctrl_mask | data_mask);

  rp1_rio_out_set(((data & 0xffff) << 8) | (REG_DATA << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((0x0000 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_oe_clr(data_mask);

  if (rp1_wait_txn_clear(NULL) != 0) {
    char where[64];
    snprintf(where, sizeof(where), "ps_write_16 addr=0x%08x", address);
    rp1_txn_timeout_fatal(where);
  }
#else
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
#endif
}

void ps_write_8(unsigned int address, unsigned int data) {
#if defined(PISTORM_RP1)
  if ((address & 1) == 0)
    data = data + (data << 8);  // EVEN, A0=0,UDS
  else
    data = data & 0xff;  // ODD , A0=1,LDS

  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RESET) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_oe_set(ctrl_mask | data_mask);

  rp1_rio_out_set(((data & 0xffff) << 8) | (REG_DATA << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((0x0100 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_oe_clr(data_mask);

  if (rp1_wait_txn_clear(NULL) != 0) {
    char where[64];
    snprintf(where, sizeof(where), "ps_write_8 addr=0x%08x", address);
    rp1_txn_timeout_fatal(where);
  }
#else
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
#endif
}

void ps_write_32(unsigned int address, unsigned int value) {
  ps_write_16(address, value >> 16);
  ps_write_16(address + 2, value);
}

#define NOP asm("nop"); asm("nop");

unsigned int ps_read_16(unsigned int address) {
#if defined(PISTORM_RP1)
  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RESET) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_oe_set(ctrl_mask | data_mask);

  rp1_rio_out_set(((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((0x0200 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_oe_clr(data_mask);

  rp1_rio_out_set(REG_DATA << PIN_A0);
  rp1_rio_out_set(1u << PIN_RD);

  uint32_t value = 0;
  if (rp1_wait_txn_clear(&value) != 0) {
    char where[64];
    snprintf(where, sizeof(where), "ps_read_16 addr=0x%08x", address);
    rp1_txn_timeout_fatal(where);
  }

  rp1_rio_out_clr(0xFFFFECu);

  return (unsigned int)((value >> 8) & 0xffffu);
#else
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
#endif
}

unsigned int ps_read_8(unsigned int address) {
#if defined(PISTORM_RP1)
  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RESET) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_oe_set(ctrl_mask | data_mask);

  rp1_rio_out_set(((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_oe_clr(data_mask);

  rp1_rio_out_set(REG_DATA << PIN_A0);
  rp1_rio_out_set(1u << PIN_RD);

  uint32_t value = 0;
  if (rp1_wait_txn_clear(&value) != 0) {
    char where[64];
    snprintf(where, sizeof(where), "ps_read_8 addr=0x%08x", address);
    rp1_txn_timeout_fatal(where);
  }

  rp1_rio_out_clr(0xFFFFECu);

  value = (value >> 8) & 0xffff;
  if ((address & 1) == 0)
    return (value >> 8) & 0xff;  // EVEN, A0=0,UDS
  else
    return value & 0xff;  // ODD , A0=1,LDS
#else
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
#endif
}

unsigned int ps_read_32(unsigned int address) {
  return (ps_read_16(address) << 16) | ps_read_16(address + 2);
}

void ps_write_status_reg(unsigned int value) {
#if defined(PISTORM_RP1)
  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RESET) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_oe_set(ctrl_mask | data_mask);

  rp1_rio_out_set(((value & 0xffff) << 8) | (REG_STATUS << PIN_A0));
  rp1_rio_out_set(1u << PIN_WR);
  rp1_rio_out_set(1u << PIN_WR);
#ifdef CHIP_FASTPATH
  rp1_rio_out_set(1u << PIN_WR);
#endif
  rp1_rio_out_clr(1u << PIN_WR);
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_oe_clr(data_mask);
#else
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
#endif
}

unsigned int ps_read_status_reg() {
#if defined(PISTORM_RP1)
  rp1_rio_out_set(REG_STATUS << PIN_A0);
  rp1_rio_out_set(1u << PIN_RD);
  rp1_rio_out_set(1u << PIN_RD);
  rp1_rio_out_set(1u << PIN_RD);
  rp1_rio_out_set(1u << PIN_RD);
#ifdef CHIP_FASTPATH
  rp1_rio_out_set(1u << PIN_RD);
  rp1_rio_out_set(1u << PIN_RD);
#endif

  uint32_t value = 0;
  if (rp1_wait_txn_clear(&value) != 0) {
    rp1_txn_timeout_fatal("ps_read_status_reg");
  }

  rp1_rio_out_clr(0xFFFFECu);

  return (unsigned int)((value >> 8) & 0xffffu);
#else
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
#endif
}

void ps_reset_state_machine() {
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
#if defined(PISTORM_RP1)
  uint32_t value = 0;
  if (rp1_wait_txn_clear(&value) != 0) {
    rp1_txn_timeout_fatal("ps_get_ipl_zero");
  }
  return (unsigned int)(value & (1u << PIN_IPL_ZERO));
#else
  unsigned int value = *(gpio + 13);
  while ((value=*(gpio + 13)) & (1 << PIN_TXN_IN_PROGRESS)) {}
  return value & (1 << PIN_IPL_ZERO);
#endif
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
