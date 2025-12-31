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
#include <string.h>
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

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | ((uint32_t)p[3] << 0);
}

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

static int rp1_load_gpio_bases_from_iomem(uint64_t *io_bank0_addr, size_t *io_bank0_len, uint64_t *sys_rio0_addr,
                                         size_t *sys_rio0_len) {
  FILE *f = fopen("/proc/iomem", "r");
  if (!f) {
    return -1;
  }

  // On Pi 5, the RP1 GPIO block is typically exposed as:
  //   1f000d0000-1f000dbfff : ... gpio@d0000
  //   1f000e0000-1f000ebfff : ... gpio@d0000
  //   1f000f0000-1f000fbfff : ... gpio@d0000
  // where the first range is IO_BANK0 and the second is SYS_RIO0.
  uint64_t starts[3] = {0, 0, 0};
  uint64_t ends[3] = {0, 0, 0};
  unsigned int found = 0;

  char line[512];
  while (fgets(line, sizeof(line), f)) {
    if (!strstr(line, "gpio@d0000")) {
      continue;
    }
    unsigned long long start = 0, end = 0;
    if (sscanf(line, "%llx-%llx", &start, &end) != 2) {
      continue;
    }
    if (found < 3) {
      starts[found] = (uint64_t)start;
      ends[found] = (uint64_t)end;
      found++;
    }
  }
  fclose(f);

  if (found < 2 || starts[0] == 0 || starts[1] == 0 || ends[0] < starts[0] || ends[1] < starts[1]) {
    return -1;
  }

  *io_bank0_addr = starts[0];
  *io_bank0_len = (size_t)(ends[0] - starts[0] + 1);
  *sys_rio0_addr = starts[1];
  *sys_rio0_len = (size_t)(ends[1] - starts[1] + 1);
  return 0;
}

static int rp1_load_gpio_bases(uint64_t *io_bank0_addr, size_t *io_bank0_len, uint64_t *sys_rio0_addr,
                               size_t *sys_rio0_len) {
  // Prefer /proc/iomem (CPU physical) when available, because DT reg values for RP1 peripherals are
  // BAR-relative (PCIe view) and not always suitable for /dev/mem mmap on every distro/kernel.
  if (rp1_load_gpio_bases_from_iomem(io_bank0_addr, io_bank0_len, sys_rio0_addr, sys_rio0_len) == 0) {
    return 0;
  }

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

typedef enum {
  RP1_PROTO_REGSEL = 0,  // PI_A[1:0] selects REG_* (as in rtl/pistorm.v in this repo)
  RP1_PROTO_SA3 = 1,     // SA0/SA1/SA2 selects op type (as in gpio/gpio_old.c and docs/RPI5_HEADER_MAP.md)
} rp1_proto_mode_t;

static rp1_proto_mode_t rp1_proto_mode = RP1_PROTO_REGSEL;

static void rp1_select_protocol_from_env() {
  const char *env = getenv("PISTORM_PROTOCOL");
  if (!env || !*env) {
    return;
  }
  if (strcmp(env, "old") == 0 || strcmp(env, "sa3") == 0 || strcmp(env, "gpio_old") == 0) {
    rp1_proto_mode = RP1_PROTO_SA3;
  } else if (strcmp(env, "new") == 0 || strcmp(env, "regsel") == 0 || strcmp(env, "pistormv") == 0) {
    rp1_proto_mode = RP1_PROTO_REGSEL;
  } else {
    printf("RP1: unknown PISTORM_PROTOCOL=%s (expected: old/sa3 or new/regsel); defaulting to regsel.\n", env);
  }
}

static unsigned int rp1_strobe_reps() {
  static unsigned int cached = 0;
  if (cached != 0) {
    return cached;
  }
  // Default: keep strobes asserted for multiple posted writes so the CPLD's
  // 200MHz synchronizer reliably sees them (RP1 writes can be extremely fast).
  unsigned int reps = 4;
  const char *env = getenv("PISTORM_RP1_STROBE_REPS");
  if (env && *env) {
    char *end = NULL;
    unsigned long v = strtoul(env, &end, 10);
    if (end != env && v >= 1 && v <= 64) {
      reps = (unsigned int)v;
    }
  }
  cached = reps;
  return cached;
}

static inline void rp1_wr_strobe() {
  const unsigned int reps = rp1_strobe_reps();
  for (unsigned int i = 0; i < reps; i++) {
    rp1_rio_out_set(1u << PIN_WR);
  }
  rp1_rio_out_clr(1u << PIN_WR);
  // Ensure the strobe edge is visible at RP1 before subsequent reads/polls.
  rp1_dmb_oshst();
  (void)rp1_rio_sync_in();  // posted write flush
  rp1_dmb_oshst();
}

static inline void rp1_rd_strobe() {
  const unsigned int reps = rp1_strobe_reps();
  for (unsigned int i = 0; i < reps; i++) {
    rp1_rio_out_set(1u << PIN_RD);
  }
  // Note: callers typically clear RD via rp1_rio_out_clr(0xFFFFECu) at end-of-transaction.
  rp1_dmb_oshst();
  (void)rp1_rio_sync_in();  // posted write flush
  rp1_dmb_oshst();
}

// Old SA0/SA1/SA2 protocol helpers (gpio/gpio_old.c).
static inline void rp1_sa_set(unsigned int sa0, unsigned int sa1, unsigned int sa2) {
  const uint32_t m0 = 1u << 5;  // SA0 on GPIO5
  const uint32_t m1 = 1u << 3;  // SA1 on GPIO3
  const uint32_t m2 = 1u << 2;  // SA2 on GPIO2

  if (sa0) rp1_rio_out_set(m0); else rp1_rio_out_clr(m0);
  if (sa1) rp1_rio_out_set(m1); else rp1_rio_out_clr(m1);
  if (sa2) rp1_rio_out_set(m2); else rp1_rio_out_clr(m2);
}

static inline void rp1_sa_mode_status() { rp1_sa_set(0, 0, 1); } // STATUSREGADDR
static inline void rp1_sa_mode_w16() { rp1_sa_set(0, 0, 0); }    // W16
static inline void rp1_sa_mode_r16() { rp1_sa_set(1, 0, 0); }    // R16
static inline void rp1_sa_mode_w8() { rp1_sa_set(0, 1, 0); }     // W8
static inline void rp1_sa_mode_r8() { rp1_sa_set(1, 1, 0); }     // R8

static inline void rp1_set_bus16(uint16_t v) {
  const uint32_t set_mask = ((uint32_t)v << 8);
  const uint32_t clr_mask = ((uint32_t)(~v) << 8) & (0xFFFFu << 8);
  rp1_rio_out_set(set_mask);
  rp1_rio_out_clr(clr_mask);
}

static inline void rp1_swe_pulse_low_high() {
  // SWE is GPIO7 in the Rev B schematic mapping; keep it high when idle, pulse low then high.
  const unsigned int reps = rp1_strobe_reps();
  rp1_rio_out_clr(1u << PIN_WR);
  for (unsigned int i = 0; i < reps; i++) {
    asm volatile("nop");
  }
  rp1_rio_out_set(1u << PIN_WR);
  rp1_dmb_oshst();
  (void)rp1_rio_sync_in();  // posted write flush
  rp1_dmb_oshst();
}

static inline void rp1_soe_assert_low() {
  // SOE is GPIO6; active-low enable.
  rp1_rio_out_clr(1u << PIN_RD);
}
static inline void rp1_soe_deassert_high() { rp1_rio_out_set(1u << PIN_RD); }

static int rp1_old_no_handshake() {
  const char *env = getenv("PISTORM_OLD_NO_HANDSHAKE");
  return (env && *env == '1') ? 1 : 0;
}

static unsigned int rp1_old_sample_nops() {
  const char *env = getenv("PISTORM_OLD_SAMPLE_NOPS");
  unsigned int n = 16;
  if (env && *env) {
    char *end = NULL;
    unsigned long v = strtoul(env, &end, 10);
    if (end != env && v <= 1000000ul) {
      n = (unsigned int)v;
    }
  }
  return n;
}

static inline unsigned int rp1_gpio_infrompad(unsigned int gpio_n);
static inline unsigned int rp1_gpio_outtopad(unsigned int gpio_n);
static inline unsigned int rp1_gpio_oetopad(unsigned int gpio_n);

static int rp1_wait_txn_clear(uint32_t *final_sync_in) {
  const uint64_t start = rp1_now_ns();
  const uint64_t deadline = start + rp1_txn_timeout_ns();
  uint32_t v = 0;
  for (;;) {
    v = rp1_rio_sync_in();
    const int txn = (v & (1u << PIN_TXN_IN_PROGRESS)) != 0;
    if (txn == 0) {
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

// Many PiStorm operations assert PI_TXN_IN_PROGRESS (GPIO0) only *after* the CPLD has
// synchronized the PI_WR strobe. If we check for "clear" too early (while txn is still low),
// we can return immediately and read stale SYNC_IN data (often all zeros).
//
// For operations that are expected to trigger a transaction (address/data reads/writes),
// first wait for txn to go high (start), then wait for it to return low (done).
static int rp1_wait_txn_start_then_clear(uint32_t *final_sync_in) {
  const uint64_t start = rp1_now_ns();
  const uint64_t deadline = start + rp1_txn_timeout_ns();
  uint32_t v = 0;

  // Wait for txn to assert.
  for (;;) {
    v = rp1_rio_sync_in();
    const int txn = (v & (1u << PIN_TXN_IN_PROGRESS)) != 0;
    if (txn != 0) {
      break;
    }
    if (rp1_now_ns() > deadline) {
      if (final_sync_in) {
        *final_sync_in = v;
      }
      return -1;
    }
    asm volatile("yield");
  }

  // Wait for txn to clear.
  for (;;) {
    v = rp1_rio_sync_in();
    const int txn = (v & (1u << PIN_TXN_IN_PROGRESS)) != 0;
    if (txn == 0) {
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

  // Dump state before we mutate OUT/OE, otherwise we lose the most useful context.
  ps_dump_protocol_state("txn-timeout-pre-release");

  // Best-effort release of the protocol lines before exiting.
  //
  // IMPORTANT: the RP1 SYS_RIO OUT/OE state is global to the SoC, not per-process. If we leave
  // any bits enabled, subsequent runs can start with pins still being actively driven.
  //
  // Release the entire PiStorm protocol pin range (GPIO0..GPIO23). GPIO4 may be muxed to GPCLK0
  // and will continue to output clock independent of SYS_RIO OE.
  const uint32_t proto_mask = 0x00FFFFFFu;
  rp1_rio_out_clr(0xFFFFECu);
  rp1_rio_oe_clr(proto_mask);

  ps_dump_protocol_state("txn-timeout-post-release");
  exit(1);
}

static inline uint32_t rp1_gpio_ctrl_index(unsigned int gpio_n) {
  return (0x004u + (gpio_n * 8u)) / 4u;
}

static inline uint32_t rp1_gpio_status_index(unsigned int gpio_n) {
  return (0x000u + (gpio_n * 8u)) / 4u;
}

static inline unsigned int rp1_gpio_infrompad(unsigned int gpio_n) {
  // IO_BANK0_GPIOx_STATUS.INFROMPAD is bit 17 (RP1 peripherals doc Table 7).
  return (rp1_io_bank0[rp1_gpio_status_index(gpio_n)] >> 17) & 1u;
}

static inline unsigned int rp1_gpio_outtopad(unsigned int gpio_n) {
  // IO_BANK0_GPIOx_STATUS.OUTTOPAD is bit 9.
  return (rp1_io_bank0[rp1_gpio_status_index(gpio_n)] >> 9) & 1u;
}

static inline unsigned int rp1_gpio_oetopad(unsigned int gpio_n) {
  // IO_BANK0_GPIOx_STATUS.OETOPAD is bit 13.
  return (rp1_io_bank0[rp1_gpio_status_index(gpio_n)] >> 13) & 1u;
}

static void rp1_configure_sys_rio_funcsel() {
  // FUNCSEL encodes the 'aN' columns in the RP1 GPIO function table.
  // - a6 == PROC_RIO[n] (fast GPIO via RIO block; writable via pinctrl on Pi 5)
  // - a5 == SYS_RIO[n] (appears to be non-selectable on some Pi 5 kernels; see STATUS.md)
  // - a0 == GPCLK[0] on GPIO4 (PiStorm clock output)
  const uint32_t funcsel_sys_rio = 5u;
  const uint32_t funcsel_proc_rio = 6u;
  const uint32_t funcsel_gpclk0 = 0u;
  const uint32_t funcsel_mask = 0x1fu;

  // Determine which RIO function is actually selectable. On Raspberry Pi 5 (RP1),
  // pinctrl commonly accepts PROC_RIO (a6) but ignores SYS_RIO (a5), leaving GPIOs as inputs.
  // Prefer an explicit override, otherwise probe by writing a candidate funcsel and reading it back.
  uint32_t funcsel_rio = funcsel_proc_rio;
  const char *rio_funcsel_env = getenv("PISTORM_RP1_RIO_FUNCSEL");
  if (rio_funcsel_env && *rio_funcsel_env) {
    char *end = NULL;
    for (unsigned int i = 0; i < 24; i++) {
      uint32_t idx = rp1_gpio_ctrl_index(i);
      uint32_t v = rp1_io_bank0[idx];
      uint32_t funcsel = funcsel_rio;
      if (i == PIN_CLK) {
        funcsel = funcsel_gpclk0; // ALT0/GPCLK0 unconditionally
      }
      v = (v & ~funcsel_mask) | funcsel;
      rp1_io_bank0[idx] = v;
    }
    rp1_dmb_oshst();

    unsigned long v = strtoul(rio_funcsel_env, &end, 0);
    if (end != rio_funcsel_env && v <= 8) {
      funcsel_rio = (uint32_t)v;
    }
  } else {
    // Probe on a protocol pin we will reconfigure anyway (GPIO2/PIN_A0).
    const uint32_t probe_gpio = PIN_A0;
    const uint32_t idx = rp1_gpio_ctrl_index(probe_gpio);
    const uint32_t orig = rp1_io_bank0[idx];
    rp1_io_bank0[idx] = (orig & ~funcsel_mask) | funcsel_sys_rio;
    rp1_dmb_oshst();
    const uint32_t rb = rp1_io_bank0[idx] & funcsel_mask;
    if (rb == funcsel_sys_rio) {
      funcsel_rio = funcsel_sys_rio;
    } else {
      funcsel_rio = funcsel_proc_rio;
    }
  }

  // By default, set GPIO4 to GPCLK[0] (a0/0). On some kernels, enabling GPCLK0
  // from userspace is blocked; in that case, you can provide the clock via a
  // kernel overlay (e.g. `pwm-pio`) and tell PiStorm to either:
  // - leave GPIO4 alone: `PISTORM_RP1_LEAVE_CLK_PIN=1`, or
  // - force a specific ALT funcsel: `PISTORM_RP1_CLK_FUNCSEL=<0..8>`
  //   (PIO is usually a7 -> funcsel 7 on RP1).
  int leave_clk_pin = 0;
  const char *leave_env = getenv("PISTORM_RP1_LEAVE_CLK_PIN");
  if (leave_env && *leave_env == '1') {
    leave_clk_pin = 1;
  }

  int have_clk_override = 0;
  uint32_t clk_override = funcsel_gpclk0;
  const char *clk_funcsel_env = getenv("PISTORM_RP1_CLK_FUNCSEL");
  if (clk_funcsel_env && *clk_funcsel_env) {
    char *end = NULL;
    unsigned long v = strtoul(clk_funcsel_env, &end, 0);
    if (end != clk_funcsel_env && v <= 8) {
      have_clk_override = 1;
      clk_override = (uint32_t)v;
      leave_clk_pin = 0;
    }
  }

  // Only touch the PiStorm protocol pin range (GPIO0..GPIO23). Avoid configuring 24..27.
  for (unsigned int i = 0; i < 24; i++) {
    uint32_t idx = rp1_gpio_ctrl_index(i);
    uint32_t v = rp1_io_bank0[idx];
    uint32_t funcsel = funcsel_rio;
    if (i == PIN_CLK) {
      if (leave_clk_pin) {
        // Force ALT0 GPCLK0 regardless of overlay state; CPLD must see a clock.
        funcsel = funcsel_gpclk0;
      } else {
        funcsel = have_clk_override ? clk_override : funcsel_gpclk0;
      }
    }
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
  rp1_select_protocol_from_env();
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
  // Attempt to configure GPCLK0 on GPIO4 on Pi 5.
  //
  // GPIO4 is muxed to GPCLK[0] via RP1 IO_BANK0 (see RP1 peripherals Table 4: GPIO4 a0 == GPCLK[0]).
  // The clock generator itself is still controlled via a SoC clock manager block; on BCM2712 this is
  // *not* at the legacy BCM283x offset (0x101000). Derive the correct MMIO base from the DT symbol
  // 'dvp' (which points at the clock manager node on Pi 5 kernels).
  // Required by the CPLD design (`rtl/pistorm.v` uses PI_CLK on GPIO4).
  //
  // Disable by setting: PISTORM_ENABLE_GPCLK=0
  const char *enable_env = getenv("PISTORM_ENABLE_GPCLK");
  if (enable_env && *enable_env == '0') {
    return;
  }

  const int gpclk_debug = (getenv("PISTORM_GPCLK_DEBUG") != NULL);

  // Derive SoC peripheral physical base from DT bus ranges:
  // `/sys/firmware/devicetree/base/soc@107c000000/ranges` maps child 0x00000000 -> parent 0x1000000000.
  uint8_t ranges[16];
  if (read_file_bytes("/sys/firmware/devicetree/base/soc@107c000000/ranges", ranges, sizeof(ranges)) != 0) {
    printf("GPCLK: failed to read DT ranges for SoC base; skipping GPCLK setup.\n");
    return;
  }
  const uint64_t soc_phys_base = be64(&ranges[4]);  // parent address (2 cells)

  // Derive clock manager child base (32-bit bus address) from DT, or allow overriding.
  uint64_t cm_phys_base = 0;
  const char *cm_phys_env = getenv("PISTORM_GPCLK_CM_PHYS");
  if (cm_phys_env && *cm_phys_env) {
    char *end = NULL;
    unsigned long long v = strtoull(cm_phys_env, &end, 0);
    if (end != cm_phys_env && v != 0) {
      cm_phys_base = (uint64_t)v;
    }
  }

  if (cm_phys_base == 0) {
    uint32_t cm_child = 0;
    const char *cm_child_env = getenv("PISTORM_GPCLK_CM_CHILD");
    if (cm_child_env && *cm_child_env) {
      char *end = NULL;
      unsigned long v = strtoul(cm_child_env, &end, 0);
      if (end != cm_child_env) {
        cm_child = (uint32_t)v;
      }
    } else {
      // DT symbol 'dvp' typically resolves to `/soc@107c000000/clock@7c700000` on Pi 5.
      char dvp_path[256];
      if (read_dt_string("/sys/firmware/devicetree/base/__symbols__/dvp", dvp_path, sizeof(dvp_path)) == 0) {
        char reg_path[512];
        snprintf(reg_path, sizeof(reg_path), "/sys/firmware/devicetree/base%s/reg", dvp_path);
        uint8_t reg[8];
        if (read_file_bytes(reg_path, reg, sizeof(reg)) == 0) {
          cm_child = be32(&reg[0]);
        }
      }
    }

    if (cm_child == 0) {
      // Fallback for older kernels/boards: legacy BCM283x clock manager offset.
      cm_child = (uint32_t)GPCLK_ADDR;
      printf("GPCLK: could not resolve DT clock base, falling back to legacy child base 0x%08x.\n", cm_child);
    }

    cm_phys_base = soc_phys_base + (uint64_t)cm_child;
  }

  const char *src_env = getenv("PISTORM_GPCLK_SRC");
  const char *div_env = getenv("PISTORM_GPCLK_DIV_INT");
  unsigned int src = 5;   // legacy default (pi3 used 5=pllc)
  unsigned int divi = 6;  // legacy default (6 -> ~200MHz on older Pis)
  if (src_env && *src_env) {
    char *end = NULL;
    unsigned long v = strtoul(src_env, &end, 0);
    if (end != src_env && v <= 15) {
      src = (unsigned int)v;
    }
  }
  if (div_env && *div_env) {
    char *end = NULL;
    unsigned long v = strtoul(div_env, &end, 0);
    if (end != div_env && v >= 1 && v <= 4095) {
      divi = (unsigned int)v;
    }
  }

  const uint64_t map_base = cm_phys_base & ~0xfffull;
  const size_t map_len = 0x1000;
  const size_t page_off = (size_t)(cm_phys_base - map_base);

  int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd < 0) {
    perror("open(/dev/mem)");
    printf("GPCLK: cannot open /dev/mem; skipping GPCLK setup.\n");
    return;
  }

  void *m = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)map_base);
  close(fd);
  if (m == MAP_FAILED) {
    const int e = errno;
    printf("GPCLK: mmap failed @ 0x%llx, errno=%d; skipping GPCLK setup.\n",
           (unsigned long long)map_base, e);
    if (e == EPERM) {
      printf("GPCLK: kernel denied /dev/mem mapping of the clock block (CONFIG_STRICT_DEVMEM or resource size too small).\n");
      printf("GPCLK: workaround: configure GPCLK0 at boot (DT overlay / firmware config), then run with PISTORM_ENABLE_GPCLK=0.\n");
      printf("GPCLK: repo helper for Pi 5: see pi5/gpclk0/README.md (or run ./tools/pi5_gpclk0_enable.sh).\n");
    }
    return;
  }

  volatile uint32_t *cm = (volatile uint32_t *)((volatile uint8_t *)m + page_off);
  const uint32_t ctl_idx = CLK_GP0_CTL / 4;
  const uint32_t div_idx = CLK_GP0_DIV / 4;

  if (gpclk_debug) {
    printf("GPCLK: pre ctl=0x%08x div=0x%08x (cm_base=0x%llx)\n",
           cm[ctl_idx], cm[div_idx], (unsigned long long)cm_phys_base);
  }

  // Disable GPCLK0.
  cm[ctl_idx] = CLK_PASSWD | (1u << 5);
  usleep(10);

  const uint64_t t0 = rp1_now_ns();
  while ((cm[ctl_idx] & (1u << 7)) != 0) {
    if (rp1_now_ns() - t0 > 200000000ull) { // 200ms
      printf("GPCLK: BUSY did not clear; skipping GPCLK setup.\n");
      munmap(m, map_len);
      return;
    }
  }
  usleep(100);

  // Set divisor (integer; fractional left at 0).
  cm[div_idx] = CLK_PASSWD | (divi << 12);
  usleep(10);

  // Enable with requested source and MASH=1 (legacy used bit4).
  cm[ctl_idx] = CLK_PASSWD | (src & 0x0fu) | (1u << 4);
  usleep(10);

  if (gpclk_debug) {
    printf("GPCLK: post ctl=0x%08x div=0x%08x\n", cm[ctl_idx], cm[div_idx]);
  }

  const uint64_t t1 = rp1_now_ns();
  while ((cm[ctl_idx] & (1u << 7)) == 0) {
    if (rp1_now_ns() - t1 > 200000000ull) { // 200ms
      printf("GPCLK: BUSY did not assert; GPCLK may not be running.\n");
      break;
    }
  }
  usleep(100);

  printf("GPCLK: configured GPCLK0 on GPIO4 (src=%u div_int=%u) using cm_base=0x%llx (soc_base=0x%llx)\n",
         src, divi, (unsigned long long)cm_phys_base, (unsigned long long)soc_phys_base);
  munmap(m, map_len);
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

  struct gpiod_line_info *line_infos[24] = {0};
  const char *names[24] = {0};
  const char *consumers[24] = {0};
  unsigned int is_used[24] = {0};

  for (unsigned int i = 0; i < 24; i++) {
    line_infos[i] = gpiod_chip_get_line_info(chip, i);
    names[i] = line_infos[i] ? gpiod_line_info_get_name(line_infos[i]) : NULL;
    is_used[i] = line_infos[i] ? (unsigned int)gpiod_line_info_is_used(line_infos[i]) : 0u;
    consumers[i] = line_infos[i] ? gpiod_line_info_get_consumer(line_infos[i]) : NULL;
  }

  struct gpiod_line_settings *settings = gpiod_line_settings_new();
  struct gpiod_line_config *line_cfg = gpiod_line_config_new();
  struct gpiod_request_config *req_cfg = gpiod_request_config_new();
  if (!settings || !line_cfg || !req_cfg) {
    printf("GPIO probe: failed to allocate libgpiod config objects.\n");
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    for (unsigned int i = 0; i < 24; i++) {
      gpiod_line_info_free(line_infos[i]);
    }
    gpiod_chip_info_free(cinfo);
    gpiod_chip_close(chip);
    return -1;
  }

  (void)gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_AS_IS);
  gpiod_request_config_set_consumer(req_cfg, "pistorm-probe");

  unsigned int free_offsets[24];
  unsigned int free_count = 0;
  for (unsigned int i = 0; i < 24; i++) {
    if (!is_used[i]) {
      free_offsets[free_count++] = i;
    }
  }

  if (free_count == 0) {
    printf("GPIO probe: lines 0-23 are all busy; printing line info only.\n");
    for (unsigned int i = 0; i < 24; i++) {
      printf("line %2u (%s): %s%s%s\n",
             i,
             names[i] ? names[i] : "-",
             is_used[i] ? "busy" : "free",
             (is_used[i] && consumers[i] && consumers[i][0]) ? " consumer=" : "",
             (is_used[i] && consumers[i] && consumers[i][0]) ? consumers[i] : "");
    }
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    for (unsigned int i = 0; i < 24; i++) {
      gpiod_line_info_free(line_infos[i]);
    }
    gpiod_chip_info_free(cinfo);
    gpiod_chip_close(chip);
    return 0;
  }

  if (gpiod_line_config_add_line_settings(line_cfg, free_offsets, free_count, settings) != 0) {
    perror("gpiod_line_config_add_line_settings");
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    for (unsigned int i = 0; i < 24; i++) {
      gpiod_line_info_free(line_infos[i]);
    }
    gpiod_chip_info_free(cinfo);
    gpiod_chip_close(chip);
    return -1;
  }

  struct gpiod_line_request *req = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
  enum gpiod_line_value values_by_offset[24];
  for (unsigned int i = 0; i < 24; i++) {
    values_by_offset[i] = GPIOD_LINE_VALUE_ERROR;
  }

  if (!req) {
    perror("gpiod_chip_request_lines");
    printf("GPIO probe: could not request %u free line(s) in 0-23 on %s; printing line info only.\n",
           free_count, chip_path);
  } else {
    enum gpiod_line_value values[24];
    if (gpiod_line_request_get_values(req, values) != 0) {
      perror("gpiod_line_request_get_values");
    } else {
      for (unsigned int i = 0; i < free_count; i++) {
        const unsigned int off = free_offsets[i];
        values_by_offset[off] = values[i];
      }
    }
    gpiod_line_request_release(req);
  }

  for (unsigned int i = 0; i < 24; i++) {
    if (values_by_offset[i] == GPIOD_LINE_VALUE_ERROR) {
      if (is_used[i]) {
        printf("line %2u (%s): busy%s%s\n",
               i,
               names[i] ? names[i] : "-",
               (consumers[i] && consumers[i][0]) ? " consumer=" : "",
               (consumers[i] && consumers[i][0]) ? consumers[i] : "");
      } else {
        printf("line %2u (%s): unreadable\n", i, names[i] ? names[i] : "-");
      }
    } else {
      printf("line %2u (%s): %d\n", i, names[i] ? names[i] : "-", values_by_offset[i]);
    }
  }

  gpiod_request_config_free(req_cfg);
  gpiod_line_config_free(line_cfg);
  gpiod_line_settings_free(settings);
  for (unsigned int i = 0; i < 24; i++) {
    gpiod_line_info_free(line_infos[i]);
  }
  gpiod_chip_info_free(cinfo);
  gpiod_chip_close(chip);
  printf("GPIO probe OK (gpiochip backend).\n");
  return 0;
#else
  printf("GPIO probe unavailable: built without libgpiod (HAVE_LIBGPIOD not set).\n");
  return -1;
#endif
}

int ps_gpclk_probe() {
#if defined(PISTORM_RP1)
  // Map RP1 GPIO blocks and attempt to start the GPCLK. Do not touch protocol OE/data direction.
  setup_io();
  setup_gpclk();
  for (int i = 0; i < 5; i++) {
    ps_dump_protocol_state("gpclk-probe");
    usleep(200000);
  }
  return 0;
#else
  setup_io();
  setup_gpclk();
  ps_dump_protocol_state("gpclk-probe");
  return 0;
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
  const uint32_t out = rp1_sys_rio0[RP1_RIO_OUT_OFF / 4] & 0x0FFFFFFFul;
  const uint32_t oe = rp1_sys_rio0[RP1_RIO_OE_OFF / 4] & 0x0FFFFFFFul;

  const unsigned int data = (sync >> 8) & 0xffffu;

  const uint32_t clk_ctrl = rp1_io_bank0[rp1_gpio_ctrl_index(PIN_CLK)];
  const unsigned int clk_funcsel = clk_ctrl & 0x1fu;
  const uint32_t clk_ctrl_reg = clk_ctrl;

  const unsigned int clk_in = rp1_gpio_infrompad(PIN_CLK);
  const unsigned int clk_out = rp1_gpio_outtopad(PIN_CLK);
  const unsigned int clk_oe = rp1_gpio_oetopad(PIN_CLK);
  const char *proto = (rp1_proto_mode == RP1_PROTO_SA3) ? "sa3" : "regsel";

  unsigned int clk_transitions_out = 0;
  unsigned int clk_transitions_in = 0;
  unsigned int clk_samples = 128;
  unsigned int last_out = clk_out;
  unsigned int last_in = clk_in;
  unsigned int sample_us = 0;
  const char *sample_env = getenv("PISTORM_CLK_SAMPLE_US");
  if (sample_env && *sample_env) {
    char *end = NULL;
    unsigned long v = strtoul(sample_env, &end, 10);
    if (end != sample_env && v > 0 && v <= 1000000ul) {
      sample_us = (unsigned int)v;
      clk_samples = 64;
    }
  }
  for (unsigned int i = 0; i < clk_samples; i++) {
    unsigned int cur_in = rp1_gpio_infrompad(PIN_CLK);
    unsigned int cur_out = rp1_gpio_outtopad(PIN_CLK);
    if (cur_in != last_in) {
      clk_transitions_in++;
      last_in = cur_in;
    }
    if (cur_out != last_out) {
      clk_transitions_out++;
      last_out = cur_out;
    }
    if (sample_us) {
      usleep(sample_us);
    }
  }

  if (rp1_proto_mode == RP1_PROTO_SA3) {
    // gpio/gpio_old.c style naming (docs/RPI5_HEADER_MAP.md)
    const unsigned int busy = (sync >> 0) & 1u;
    const unsigned int irq = (sync >> 1) & 1u;
    const unsigned int sa2 = (sync >> 2) & 1u;
    const unsigned int sa1 = (sync >> 3) & 1u;
    const unsigned int clk = (sync >> 4) & 1u;
    const unsigned int sa0 = (sync >> 5) & 1u;
    const unsigned int soe = (sync >> 6) & 1u;
    const unsigned int swe = (sync >> 7) & 1u;

    const unsigned int busy_pad = rp1_gpio_infrompad(0);
    const unsigned int irq_pad = rp1_gpio_infrompad(1);

    const unsigned int sa2_funcsel = rp1_io_bank0[rp1_gpio_ctrl_index(2)] & 0x1fu;
    const unsigned int sa1_funcsel = rp1_io_bank0[rp1_gpio_ctrl_index(3)] & 0x1fu;
    const unsigned int sa0_funcsel = rp1_io_bank0[rp1_gpio_ctrl_index(5)] & 0x1fu;
    const unsigned int soe_funcsel = rp1_io_bank0[rp1_gpio_ctrl_index(6)] & 0x1fu;
    const unsigned int swe_funcsel = rp1_io_bank0[rp1_gpio_ctrl_index(7)] & 0x1fu;

    const unsigned int sa0_oetopad = rp1_gpio_oetopad(5);
    const unsigned int sa0_outtopad = rp1_gpio_outtopad(5);
    const unsigned int soe_oetopad = rp1_gpio_oetopad(6);
    const unsigned int soe_outtopad = rp1_gpio_outtopad(6);
    const unsigned int swe_oetopad = rp1_gpio_oetopad(7);
    const unsigned int swe_outtopad = rp1_gpio_outtopad(7);

    printf("[GPIO] %s: proto=%s sync_in=0x%08x busy=%u/%u irq=%u/%u sa2=%u sa1=%u sa0=%u clk=%u/%u/%u/%u soe=%u swe=%u d=0x%04x | out=0x%08x oe=0x%08x | fsel(sa2,sa1,sa0,soe,swe)=%u,%u,%u,%u,%u gpio4_funcsel=%u clk_transitions_out=%u/%u clk_transitions_in=%u/%u | gpio5(oetopad=%u outtopad=%u) gpio6(oetopad=%u outtopad=%u) gpio7(oetopad=%u outtopad=%u) | rp1_gpio4_ctrl=0x%08x rp1_gpio4_funcsel=%u\n",
           t, proto, sync,
           busy, busy_pad, irq, irq_pad,
           sa2, sa1, sa0,
           clk, clk_in, clk_out, clk_oe,
           soe, swe, data,
           out, oe,
           sa2_funcsel, sa1_funcsel, sa0_funcsel, soe_funcsel, swe_funcsel,
           clk_funcsel, clk_transitions_out, clk_samples, clk_transitions_in, clk_samples,
           sa0_oetopad, sa0_outtopad, soe_oetopad, soe_outtopad, swe_oetopad, swe_outtopad,
           clk_ctrl_reg, clk_funcsel);
  } else {
    // rtl/pistorm.v style naming (reg-select protocol)
    const unsigned int txn = (sync >> PIN_TXN_IN_PROGRESS) & 1u;
    const unsigned int ipl0 = (sync >> PIN_IPL_ZERO) & 1u;
    const unsigned int a0 = (sync >> PIN_A0) & 1u;
    const unsigned int a1 = (sync >> PIN_A1) & 1u;
    const unsigned int clk = (sync >> PIN_CLK) & 1u;
    const unsigned int rst = (sync >> PIN_RESET) & 1u;
    const unsigned int rd = (sync >> PIN_RD) & 1u;
    const unsigned int wr = (sync >> PIN_WR) & 1u;

    const unsigned int txn_pad = rp1_gpio_infrompad(PIN_TXN_IN_PROGRESS);
    const unsigned int ipl0_pad = rp1_gpio_infrompad(PIN_IPL_ZERO);
    const unsigned int rst_pad = rp1_gpio_infrompad(PIN_RESET);
    const unsigned int rst_oetopad = rp1_gpio_oetopad(PIN_RESET);
    const unsigned int rst_outtopad = rp1_gpio_outtopad(PIN_RESET);

    const unsigned int a0_funcsel = rp1_io_bank0[rp1_gpio_ctrl_index(PIN_A0)] & 0x1fu;
    const unsigned int a1_funcsel = rp1_io_bank0[rp1_gpio_ctrl_index(PIN_A1)] & 0x1fu;
    const unsigned int rd_funcsel = rp1_io_bank0[rp1_gpio_ctrl_index(PIN_RD)] & 0x1fu;
    const unsigned int wr_funcsel = rp1_io_bank0[rp1_gpio_ctrl_index(PIN_WR)] & 0x1fu;

    const unsigned int a0_oetopad = rp1_gpio_oetopad(PIN_A0);
    const unsigned int a0_outtopad = rp1_gpio_outtopad(PIN_A0);
    const unsigned int wr_oetopad = rp1_gpio_oetopad(PIN_WR);
    const unsigned int wr_outtopad = rp1_gpio_outtopad(PIN_WR);

    printf("[GPIO] %s: proto=%s sync_in=0x%08x txn=%u/%u ipl0=%u/%u regsel(a0,a1)=%u,%u clk=%u/%u/%u/%u rst=%u/%u rd=%u wr=%u d=0x%04x | out=0x%08x oe=0x%08x | fsel(gpio2,gpio3,gpio6,gpio7)=%u,%u,%u,%u gpio4_funcsel=%u clk_transitions_out=%u/%u clk_transitions_in=%u/%u | gpio2(oetopad=%u outtopad=%u) gpio7(oetopad=%u outtopad=%u) gpio5(oetopad=%u outtopad=%u) | rp1_gpio4_ctrl=0x%08x rp1_gpio4_funcsel=%u\n",
           t, proto, sync,
           txn, txn_pad, ipl0, ipl0_pad,
           a0, a1,
           clk, clk_in, clk_out, clk_oe,
           rst, rst_pad, rd, wr, data,
           out, oe,
           a0_funcsel, a1_funcsel, rd_funcsel, wr_funcsel,
           clk_funcsel, clk_transitions_out, clk_samples, clk_transitions_in, clk_samples,
           a0_oetopad, a0_outtopad, wr_oetopad, wr_outtopad, rst_oetopad, rst_outtopad,
           clk_ctrl_reg, clk_funcsel);
  }
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

uint32_t ps_read_gpio_state() {
#if defined(PISTORM_RP1)
  if (!rp1_sys_rio0) {
    return 0;
  }
  return rp1_rio_sync_in();
#else
  if (!gpio) {
    return 0;
  }
  return *(gpio + 13);
#endif
}

void ps_setup_protocol() {
  setup_io();
  setup_gpclk();

#if defined(PISTORM_RP1)
  // The RIO OE/OUT registers are global; always start by releasing the entire PiStorm pin range so
  // a previous crashed run can't leave pins actively driven (common cause: GPIO5 left as output).
  const uint32_t proto_mask = 0x00FFFFFFu;
  rp1_rio_oe_clr(proto_mask);
  rp1_rio_out_clr(proto_mask);

  if (rp1_proto_mode == RP1_PROTO_SA3) {
    // Old SA0/SA1/SA2 protocol:
    // - GPIO2/3/5 are SA2/SA1/SA0 outputs
    // - GPIO6 (SOE) + GPIO7 (SWE) are strobes
    // - GPIO0 is handshake/busy, GPIO1 is IRQ
    // Default: data bus inputs, strobes deasserted high.
    const uint32_t sa_mask = (1u << 2) | (1u << 3) | (1u << 5);
    const uint32_t strobe_mask = (1u << 6) | (1u << 7);
    const uint32_t data_mask = (0xFFFFu << 8);
    rp1_rio_oe_clr(sa_mask | strobe_mask | data_mask);
    rp1_rio_oe_set(sa_mask | strobe_mask);
    rp1_sa_mode_w16();
    rp1_soe_deassert_high();
    rp1_rio_out_set(1u << PIN_WR);
    rp1_rio_oe_clr(data_mask);
  } else {
    // New reg-select protocol (rtl/pistorm.v in this repo):
    // Inputs: PIN_TXN_IN_PROGRESS (0), PIN_IPL_ZERO (1)
    // Outputs: all other protocol pins; default to inputs on D[0..15] until needed.
    // NOTE: GPIO5 (PIN_RESET) is driven by the CPLD in this RTL, so do not enable output drive on it from the Pi side.
    const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RD) | (1u << PIN_WR);
    const uint32_t data_mask = (0xFFFFu << 8);
    rp1_rio_oe_clr(ctrl_mask | data_mask);
    rp1_rio_out_clr(0xFFFFECu);
    rp1_rio_oe_set(ctrl_mask);
  }
#else
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
#endif
}

void ps_write_16(unsigned int address, unsigned int data) {
#if defined(PISTORM_RP1)
  if (rp1_proto_mode == RP1_PROTO_SA3) {
    // gpio/gpio_old.c write16
    const uint32_t sa_mask = (1u << 2) | (1u << 3) | (1u << 5);
    const uint32_t strobe_mask = (1u << 6) | (1u << 7);
    const uint32_t data_mask = (0xFFFFu << 8);
    rp1_sa_mode_w16();
    rp1_rio_oe_set(sa_mask | strobe_mask | data_mask);

    /* Working order for CPLD: data -> addr_lo -> addr_hi (txn). */
    rp1_set_bus16((uint16_t)(data & 0xffffu));
    rp1_swe_pulse_low_high();

    rp1_set_bus16((uint16_t)(address & 0xffffu));
    rp1_swe_pulse_low_high();

    rp1_set_bus16((uint16_t)((address >> 16) & 0xffffu));
    rp1_swe_pulse_low_high();

    /* Hold data driven until the transaction (if any) completes. */
    rp1_rio_oe_clr(data_mask);
    // Wait for txn to complete if it asserts; avoid hanging if it doesn't.
    if (rp1_gpio_infrompad(PIN_TXN_IN_PROGRESS)) {
      (void)rp1_wait_txn_start_then_clear(NULL);
    }
    return;
  }

  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_oe_set(ctrl_mask | data_mask);

  /* Put data on the bus before the high address word (which starts the txn). */
  rp1_rio_out_set(((data & 0xffff) << 8) | (REG_DATA << PIN_A0));
  rp1_wr_strobe();
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0));
  rp1_wr_strobe();
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((0x0000 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0));
  rp1_wr_strobe();
  rp1_rio_out_clr(0xFFFFECu);

  if (rp1_wait_txn_start_then_clear(NULL) != 0) {
    char where[64];
    snprintf(where, sizeof(where), "ps_write_16 addr=0x%08x", address);
    rp1_txn_timeout_fatal(where);
  }
  rp1_rio_oe_clr(data_mask);
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
  if (rp1_proto_mode == RP1_PROTO_SA3) {
    // gpio/gpio_old.c write8
    if ((address & 1) == 0)
      data = data + (data << 8);  // EVEN, A0=0,UDS
    else
      data = data & 0xff;  // ODD , A0=1,LDS

    const uint32_t sa_mask = (1u << 2) | (1u << 3) | (1u << 5);
    const uint32_t strobe_mask = (1u << 6) | (1u << 7);
    const uint32_t data_mask = (0xFFFFu << 8);
    rp1_sa_mode_w8();
    rp1_rio_oe_set(sa_mask | strobe_mask | data_mask);

    /* Working order for CPLD: data -> addr_lo -> addr_hi (txn). */
    rp1_set_bus16((uint16_t)(data & 0xffffu));
    rp1_swe_pulse_low_high();

    rp1_set_bus16((uint16_t)(address & 0xffffu));
    rp1_swe_pulse_low_high();

    rp1_set_bus16((uint16_t)((address >> 16) & 0xffffu));
    rp1_swe_pulse_low_high();

    /* Hold data OE until the transaction (if any) completes. */
    if (rp1_gpio_infrompad(PIN_TXN_IN_PROGRESS)) {
      (void)rp1_wait_txn_start_then_clear(NULL);
    }
    rp1_rio_oe_clr(data_mask);
    return;
  }

  if ((address & 1) == 0)
    data = data + (data << 8);  // EVEN, A0=0,UDS
  else
    data = data & 0xff;  // ODD , A0=1,LDS

  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_oe_set(ctrl_mask | data_mask);

  rp1_rio_out_set(((data & 0xffff) << 8) | (REG_DATA << PIN_A0));
  rp1_wr_strobe();
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0));
  rp1_wr_strobe();
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((0x0100 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0));
  rp1_wr_strobe();
  rp1_rio_out_clr(0xFFFFECu);

  /* Keep data driven until the CPLD latches/clears the txn. */
  if (rp1_wait_txn_start_then_clear(NULL) != 0) {
    char where[64];
    snprintf(where, sizeof(where), "ps_write_8 addr=0x%08x", address);
    rp1_txn_timeout_fatal(where);
  }
  rp1_rio_oe_clr(data_mask);
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
  if (rp1_proto_mode == RP1_PROTO_SA3) {
    // gpio/gpio_old.c read16
    const uint32_t sa_mask = (1u << 2) | (1u << 3) | (1u << 5);
    const uint32_t strobe_mask = (1u << 6) | (1u << 7);
    const uint32_t data_mask = (0xFFFFu << 8);
    rp1_sa_mode_r16();
    rp1_rio_oe_set(sa_mask | strobe_mask | data_mask);

    rp1_set_bus16((uint16_t)(address & 0xffffu));
    rp1_swe_pulse_low_high();

    rp1_set_bus16((uint16_t)((address >> 16) & 0xffffu));
    rp1_swe_pulse_low_high();

    rp1_rio_oe_clr(data_mask);
    rp1_soe_assert_low();

    // Some SA0/SA1/SA2 variants do not provide a usable handshake on GPIO0.
    // Allow bypassing the wait to see whether the data bus is being driven at all.
    if (!rp1_old_no_handshake()) {
      if (rp1_wait_txn_start_then_clear(NULL) != 0) {
        char where[64];
        snprintf(where, sizeof(where), "old_read_16 addr=0x%08x (no txn)", address);
        rp1_txn_timeout_fatal(where);
      }
    } else {
      const unsigned int nops = rp1_old_sample_nops();
      for (unsigned int i = 0; i < nops; i++) {
        asm volatile("nop");
      }
    }
    const uint32_t v = rp1_rio_sync_in();
    rp1_soe_deassert_high();
    return (unsigned int)((v >> 8) & 0xffffu);
  }

  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_oe_set(ctrl_mask | data_mask);

  rp1_rio_out_set(((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0));
  rp1_wr_strobe();
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((0x0200 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0));
  rp1_wr_strobe();
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_oe_clr(data_mask);

  rp1_rio_out_set(REG_DATA << PIN_A0);
  rp1_rd_strobe();

  uint32_t value = 0;
  if (rp1_wait_txn_start_then_clear(&value) != 0) {
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
  if (rp1_proto_mode == RP1_PROTO_SA3) {
    // gpio/gpio_old.c read8
    const uint32_t sa_mask = (1u << 2) | (1u << 3) | (1u << 5);
    const uint32_t strobe_mask = (1u << 6) | (1u << 7);
    const uint32_t data_mask = (0xFFFFu << 8);
    rp1_sa_mode_r8();
    rp1_rio_oe_set(sa_mask | strobe_mask | data_mask);

    rp1_set_bus16((uint16_t)(address & 0xffffu));
    rp1_swe_pulse_low_high();

    rp1_set_bus16((uint16_t)((address >> 16) & 0xffffu));
    rp1_swe_pulse_low_high();

    rp1_rio_oe_clr(data_mask);
    rp1_soe_assert_low();

    if (!rp1_old_no_handshake()) {
      if (rp1_wait_txn_start_then_clear(NULL) != 0) {
        char where[64];
        snprintf(where, sizeof(where), "old_read_8 addr=0x%08x (no txn)", address);
        rp1_txn_timeout_fatal(where);
      }
    } else {
      const unsigned int nops = rp1_old_sample_nops();
      for (unsigned int i = 0; i < nops; i++) {
        asm volatile("nop");
      }
    }
    uint32_t v = rp1_rio_sync_in();
    rp1_soe_deassert_high();

    v = (v >> 8) & 0xffffu;
    if ((address & 1) == 0)
      return (v >> 8) & 0xff;  // EVEN
    else
      return v & 0xff;  // ODD
  }

  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RD) | (1u << PIN_WR);
  const uint32_t data_mask = (0xFFFFu << 8);
  rp1_rio_oe_set(ctrl_mask | data_mask);

  rp1_rio_out_set(((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0));
  rp1_wr_strobe();
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_out_set(((0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0));
  rp1_wr_strobe();
  rp1_rio_out_clr(0xFFFFECu);

  rp1_rio_oe_clr(data_mask);

  rp1_rio_out_set(REG_DATA << PIN_A0);
  rp1_rd_strobe();

  uint32_t value = 0;
  if (rp1_wait_txn_start_then_clear(&value) != 0) {
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
  if (rp1_proto_mode == RP1_PROTO_SA3) {
    const uint32_t sa_mask = (1u << 2) | (1u << 3) | (1u << 5);
    const uint32_t strobe_mask = (1u << 6) | (1u << 7);
    const uint32_t data_mask = (0xFFFFu << 8);
    rp1_sa_mode_status();
    rp1_rio_oe_set(sa_mask | strobe_mask | data_mask);
    rp1_set_bus16((uint16_t)(value & 0xffffu));
    rp1_swe_pulse_low_high();
    rp1_rio_oe_clr(data_mask);
    return;
  }

  const uint32_t ctrl_mask = (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RD) | (1u << PIN_WR);
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
  if (rp1_proto_mode == RP1_PROTO_SA3) {
    const uint32_t sa_mask = (1u << 2) | (1u << 3) | (1u << 5);
    const uint32_t strobe_mask = (1u << 6) | (1u << 7);
    const uint32_t data_mask = (0xFFFFu << 8);
    rp1_sa_mode_status();
    rp1_rio_oe_set(sa_mask | strobe_mask);
    rp1_rio_oe_clr(data_mask);
    rp1_soe_assert_low();
    // No handshake wait here (matches gpio_old.c read_reg).
    const uint32_t v = rp1_rio_sync_in();
    rp1_soe_deassert_high();
    return (unsigned int)((v >> 8) & 0xffffu);
  }

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
