// SPDX-License-Identifier: MIT

#include "pistorm/backend.h"
#include "pistorm/backend_userspace_mmio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifndef MAP_SHARED
#define MAP_SHARED 0x01
#endif

/* GPIO register offsets */
#define GPIO_GPFSEL0 0x00u
#define GPIO_GPFSEL1 0x04u
#define GPIO_GPFSEL2 0x08u
#define GPIO_GPSET0 0x1Cu
#define GPIO_GPCLR0 0x28u
#define GPIO_GPLEV0 0x34u
#define GPIO_GPLEV1 0x38u

/* CPRMAN offsets */
#define CPRMAN_GP0CTL 0x70u
#define CPRMAN_GP0DIV 0x74u
#define CPRMAN_PASSWD 0x5A000000u

#define GPCLK_CTL_ENAB (1u << 4)
#define GPCLK_CTL_KILL (1u << 5)
#define GPCLK_CTL_BUSY (1u << 7)

#define GPIO_FSEL_INPUT 0u
#define GPIO_FSEL_OUTPUT 1u
#define GPIO_FSEL_ALT0 4u

/* PiStorm pin map */
#define PIN_TXN_IN_PROGRESS 0u
#define PIN_IPL_ZERO 1u
#define PIN_A0 2u
#define PIN_A1 3u
#define PIN_CLK 4u
#define PIN_RESET 5u
#define PIN_RD 6u
#define PIN_WR 7u
#define PIN_D(x) (8u + (x))

#define REG_DATA 0u
#define REG_ADDR_LO 1u
#define REG_ADDR_HI 2u
#define REG_STATUS 3u

#define STATUS_BIT_INIT 0x0001u
#define STATUS_BIT_RESET 0x0002u
#define STATUS_BIT_BUS_ARB 0x0004u

#define UMIO_TIMEOUT_US 500000u
#define UMIO_GPCLK_SRC_DEFAULT 5u
#define UMIO_GPCLK_DIV_DEFAULT 6u
#define UMIO_WR_STRETCH_DEFAULT 2u
#define UMIO_RD_STRETCH_DEFAULT 2u
#define UMIO_DEADLINE_CHECK_ITERS 256u
#define UMIO_CHIP_RAM_LIMIT 0x00200000u

struct ps_userspace_mmio_cfg {
  uint32_t gpclk_src;
  uint32_t gpclk_div;
  uint32_t wr_stretch;
  uint32_t rd_stretch;
  bool lwpair_enable;
  bool r32pair_enable;
  bool ramseq_enable;
  bool wpipe_enable;
  int gpclk_src_overridden;
  int gpclk_div_overridden;
  int wr_stretch_overridden;
  int rd_stretch_overridden;
  int lwpair_overridden;
  int r32pair_overridden;
  int ramseq_overridden;
  int wpipe_overridden;
};

struct ps_userspace_mmio_state {
  int gpio_fd;
  int mem_fd;
  int backend_logged;

  void* gpio_map_base;
  size_t gpio_map_len;
  volatile uint32_t* gpio_base;

  void* cprman_map_base;
  size_t cprman_map_len;
  volatile uint32_t* cprman_base;

  uint32_t gpio_phys;
  uint32_t cprman_phys;

  bool data_out;
  bool gpclk_ready;

  bool berr_reset_input;
  bool bus_arb_release;
  bool setup_gpclk;
  bool lwpair_enable;
  bool lrpair_enable;
  bool ramseq_enable;
  bool wpipe_enable;
  bool write_pending;
  bool trace_txn;
  bool trace_txn_only_nonzero_fc;
  bool trace_txn_limit_reported;
  bool force_fc0;
  uint32_t trace_txn_limit;
  uint32_t trace_txn_count;
  uint32_t wr_stretch;
  uint32_t rd_stretch;
  const char* last_txn_op;
  uint32_t last_txn_addr;
  uint8_t last_txn_fc;
  uint16_t last_txn_opbits;
  bool last_txn_is_read;

  uint32_t fsel_input[3];
  uint32_t fsel_output[3];
};

static struct ps_userspace_mmio_state gu = {
  .gpio_fd = -1,
  .mem_fd = -1,
};

static struct ps_userspace_mmio_cfg gu_cfg = {
  .gpclk_src = UMIO_GPCLK_SRC_DEFAULT,
  .gpclk_div = UMIO_GPCLK_DIV_DEFAULT,
  .wr_stretch = UMIO_WR_STRETCH_DEFAULT,
  .rd_stretch = UMIO_RD_STRETCH_DEFAULT,
  .lwpair_enable = true,
  .r32pair_enable = true,
  .ramseq_enable = true,
  .wpipe_enable = false,
};

int ps_userspace_mmio_set_gpclk_src(uint32_t src) {
  if (src > 0xFu) {
    return -EINVAL;
  }
  gu_cfg.gpclk_src = src;
  gu_cfg.gpclk_src_overridden = 1;
  return 0;
}

int ps_userspace_mmio_set_gpclk_div(uint32_t div) {
  if (div == 0 || div > 0xFFFu) {
    return -EINVAL;
  }
  gu_cfg.gpclk_div = div;
  gu_cfg.gpclk_div_overridden = 1;
  return 0;
}

int ps_userspace_mmio_set_wr_stretch(uint32_t count) {
  if (count > 64u) {
    return -EINVAL;
  }
  gu_cfg.wr_stretch = count;
  gu_cfg.wr_stretch_overridden = 1;
  return 0;
}

int ps_userspace_mmio_set_rd_stretch(uint32_t count) {
  if (count > 64u) {
    return -EINVAL;
  }
  gu_cfg.rd_stretch = count;
  gu_cfg.rd_stretch_overridden = 1;
  return 0;
}

int ps_userspace_mmio_set_lwpair(uint32_t enabled) {
  if (enabled > 1u) {
    return -EINVAL;
  }
  gu_cfg.lwpair_enable = enabled != 0u;
  gu_cfg.lwpair_overridden = 1;
  return 0;
}

int ps_userspace_mmio_set_r32pair(uint32_t enabled) {
  if (enabled > 1u) {
    return -EINVAL;
  }
  gu_cfg.r32pair_enable = enabled != 0u;
  gu_cfg.r32pair_overridden = 1;
  return 0;
}

int ps_userspace_mmio_set_ramseq(uint32_t enabled) {
  if (enabled > 1u) {
    return -EINVAL;
  }
  gu_cfg.ramseq_enable = enabled != 0u;
  gu_cfg.ramseq_overridden = 1;
  return 0;
}

int ps_userspace_mmio_set_wpipe(uint32_t enabled) {
  if (enabled > 1u) {
    return -EINVAL;
  }
  gu_cfg.wpipe_enable = enabled != 0u;
  gu_cfg.wpipe_overridden = 1;
  return 0;
}

static uint32_t read_be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int parse_bool_env(const char* name, int default_value) {
  const char* env = getenv(name);

  if (!env || !*env) {
    return default_value;
  }

  if (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0 || strcasecmp(env, "yes") == 0 ||
      strcasecmp(env, "on") == 0) {
    return 1;
  }

  if (strcmp(env, "0") == 0 || strcasecmp(env, "false") == 0 || strcasecmp(env, "no") == 0 ||
      strcasecmp(env, "off") == 0) {
    return 0;
  }

  return default_value;
}

static int parse_bool_env_value(const char* name, bool* out) {
  const char* env = getenv(name);

  if (!env || !*env || !out) {
    return -1;
  }

  if (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0 || strcasecmp(env, "yes") == 0 ||
      strcasecmp(env, "on") == 0) {
    *out = true;
    return 0;
  }

  if (strcmp(env, "0") == 0 || strcasecmp(env, "false") == 0 || strcasecmp(env, "no") == 0 ||
      strcasecmp(env, "off") == 0) {
    *out = false;
    return 0;
  }

  return -1;
}

static int parse_u32_env(const char* name, uint32_t* out) {
  const char* env = getenv(name);
  char* end = NULL;
  unsigned long parsed;

  if (!env || !*env || !out) {
    return -1;
  }

  parsed = strtoul(env, &end, 0);
  if (!end || *end != '\0' || parsed > 0xFFFFFFFFul) {
    return -1;
  }

  *out = (uint32_t)parsed;
  return 0;
}

static uint32_t default_periph_base(void) {
#if defined(__aarch64__)
  return 0xFE000000u;
#else
  return 0x3F000000u;
#endif
}

static int detect_peripheral_base(uint32_t* out_base) {
  FILE* in = NULL;
  uint8_t buf[256];
  size_t n = 0;
  size_t words = 0;

  if (!out_base) {
    return -1;
  }

  in = fopen("/proc/device-tree/soc/ranges", "rb");
  if (!in) {
    return -1;
  }

  n = fread(buf, 1, sizeof(buf), in);
  fclose(in);
  in = NULL;

  if (n < 12 || (n % 4) != 0) {
    return -1;
  }

  words = n / 4;
  for (size_t i = 0; i + 2 < words; i++) {
    uint32_t child = read_be32(&buf[i * 4]);
    if (child != 0x7E000000u) {
      continue;
    }

    /* #address-cells parent can be 1 or 2 depending on DT. */
    if (i + 3 < words) {
      uint32_t parent_hi = read_be32(&buf[(i + 1) * 4]);
      uint32_t parent_lo = read_be32(&buf[(i + 2) * 4]);
      if (parent_hi == 0 && (parent_lo & 0xF0000000u)) {
        *out_base = parent_lo;
        return 0;
      }
    }

    {
      uint32_t parent = read_be32(&buf[(i + 1) * 4]);
      if (parent & 0xF0000000u) {
        *out_base = parent;
        return 0;
      }
    }
  }

  return -1;
}

static inline uint32_t um_readl_gpio(uint32_t off) {
  return gu.gpio_base[off / 4u];
}

static inline void um_writel_gpio(uint32_t off, uint32_t val) {
  gu.gpio_base[off / 4u] = val;
}

static inline uint32_t um_readl_cprman(uint32_t off) {
  return gu.cprman_base[off / 4u];
}

static inline void um_writel_cprman(uint32_t off, uint32_t val) {
  gu.cprman_base[off / 4u] = val;
}

static inline void um_write_set(uint32_t mask) {
  um_writel_gpio(GPIO_GPSET0, mask);
}

static inline void um_write_clr(uint32_t mask) {
  um_writel_gpio(GPIO_GPCLR0, mask);
}

static inline void um_mmio_barrier(void) {
  /*
   * Force posted GPIO writes to become visible before the next edge-sensitive
   * transition. This trades a bit of speed for deterministic strobe timing.
   */
  (void)um_readl_gpio(GPIO_GPLEV0);
}

static inline uint32_t um_effective_wr_stretch(void) {
  return gu.wr_stretch == 0u ? 1u : gu.wr_stretch;
}

static inline uint32_t um_effective_rd_stretch(void) {
  return gu.rd_stretch == 0u ? 1u : gu.rd_stretch;
}

static inline uint8_t um_effective_fc(uint8_t fc) {
  uint8_t fc7 = (uint8_t)(fc & 0x7u);
  if (gu.force_fc0) {
    return 0u;
  }
  return fc7;
}

static inline void um_note_txn(const char* op_name, uint32_t addr, uint8_t fc, uint16_t opbits,
                               bool is_read) {
  bool should_print;
  gu.last_txn_op = op_name;
  gu.last_txn_addr = addr;
  gu.last_txn_fc = (uint8_t)(fc & 0x7u);
  gu.last_txn_opbits = opbits;
  gu.last_txn_is_read = is_read;
  should_print = gu.trace_txn;
  if (should_print && gu.trace_txn_only_nonzero_fc && gu.last_txn_fc == 0u) {
    should_print = false;
  }
  if (should_print && gu.trace_txn_limit != 0u && gu.trace_txn_count >= gu.trace_txn_limit) {
    should_print = false;
    if (!gu.trace_txn_limit_reported) {
      fprintf(stderr,
              "[ps_backend:userspace-mmio] txn trace limit reached (%u); suppressing further txn logs\n",
              gu.trace_txn_limit);
      gu.trace_txn_limit_reported = true;
    }
  }
  if (should_print) {
    gu.trace_txn_count++;
    fprintf(stderr,
            "[ps_backend:userspace-mmio] txn %s addr=%08X fc=%u opbits=%03X read=%u pending=%u out=%u\n",
            op_name ? op_name : "?", addr, (unsigned int)gu.last_txn_fc, (unsigned int)opbits,
            is_read ? 1u : 0u, gu.write_pending ? 1u : 0u, gu.data_out ? 1u : 0u);
  }
}

static inline bool um_addr_is_chip_ram(uint32_t addr, uint32_t bytes) {
  if (bytes == 0u) {
    return false;
  }
  if (addr >= UMIO_CHIP_RAM_LIMIT) {
    return false;
  }
  return addr <= (UMIO_CHIP_RAM_LIMIT - bytes);
}

static inline bool um_wpipe_allowed(uint32_t addr, uint32_t bytes, uint8_t fc) {
  /* FC-safe mode: pipeline only non-FC (legacy fc=0) chip-ram writes. */
  if (!gu.wpipe_enable) {
    return false;
  }
  if ((fc & 0x7u) != 0u) {
    return false;
  }
  return um_addr_is_chip_ram(addr, bytes);
}

static uint32_t um_set_fsel(uint32_t fsel, unsigned int pin, unsigned int func) {
  unsigned int shift = (pin % 10u) * 3u;
  uint32_t mask = 0x7u << shift;

  fsel &= ~mask;
  fsel |= ((func & 0x7u) << shift);
  return fsel;
}

static void um_prepare_fsel(void) {
  uint32_t fsel0 = um_readl_gpio(GPIO_GPFSEL0);
  uint32_t fsel1 = um_readl_gpio(GPIO_GPFSEL1);
  uint32_t fsel2 = um_readl_gpio(GPIO_GPFSEL2);

  for (unsigned int pin = 0; pin <= 9; pin++) {
    fsel0 = um_set_fsel(fsel0, pin, GPIO_FSEL_INPUT);
  }
  for (unsigned int pin = 10; pin <= 19; pin++) {
    fsel1 = um_set_fsel(fsel1, pin, GPIO_FSEL_INPUT);
  }
  for (unsigned int pin = 20; pin <= 23; pin++) {
    fsel2 = um_set_fsel(fsel2, pin, GPIO_FSEL_INPUT);
  }

  fsel0 = um_set_fsel(fsel0, PIN_TXN_IN_PROGRESS, GPIO_FSEL_INPUT);
  fsel0 = um_set_fsel(fsel0, PIN_IPL_ZERO, GPIO_FSEL_INPUT);
  fsel0 = um_set_fsel(fsel0, PIN_A0, GPIO_FSEL_OUTPUT);
  fsel0 = um_set_fsel(fsel0, PIN_A1, GPIO_FSEL_OUTPUT);
  fsel0 = um_set_fsel(fsel0, PIN_CLK, GPIO_FSEL_ALT0);
  fsel0 = um_set_fsel(fsel0, PIN_RESET, gu.berr_reset_input ? GPIO_FSEL_INPUT : GPIO_FSEL_OUTPUT);
  fsel0 = um_set_fsel(fsel0, PIN_RD, GPIO_FSEL_OUTPUT);
  fsel0 = um_set_fsel(fsel0, PIN_WR, GPIO_FSEL_OUTPUT);

  gu.fsel_input[0] = fsel0;
  gu.fsel_input[1] = fsel1;
  gu.fsel_input[2] = fsel2;

  gu.fsel_output[0] = um_set_fsel(fsel0, PIN_D(0), GPIO_FSEL_OUTPUT);
  gu.fsel_output[0] = um_set_fsel(gu.fsel_output[0], PIN_D(1), GPIO_FSEL_OUTPUT);

  gu.fsel_output[1] = fsel1;
  for (unsigned int pin = 10; pin <= 19; pin++) {
    gu.fsel_output[1] = um_set_fsel(gu.fsel_output[1], pin, GPIO_FSEL_OUTPUT);
  }

  gu.fsel_output[2] = fsel2;
  for (unsigned int pin = 20; pin <= 23; pin++) {
    gu.fsel_output[2] = um_set_fsel(gu.fsel_output[2], pin, GPIO_FSEL_OUTPUT);
  }
}

static void um_set_bus_dir(bool data_out) {
  if (gu.data_out == data_out) {
    return;
  }

  gu.data_out = data_out;
  um_writel_gpio(GPIO_GPFSEL0, data_out ? gu.fsel_output[0] : gu.fsel_input[0]);
  um_writel_gpio(GPIO_GPFSEL1, data_out ? gu.fsel_output[1] : gu.fsel_input[1]);
  um_writel_gpio(GPIO_GPFSEL2, data_out ? gu.fsel_output[2] : gu.fsel_input[2]);
}

static void um_clear_lines(void) {
  uint32_t mask = 0x00FFFF00u | (1u << PIN_A0) | (1u << PIN_A1) | (1u << PIN_RD) | (1u << PIN_WR);
  if (!gu.berr_reset_input) {
    mask |= (1u << PIN_RESET);
  }
  um_write_clr(mask);
}

static uint64_t now_us_monotonic(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000ull);
}

static int um_wait_for_txn(const char* op_name) {
  uint64_t deadline = now_us_monotonic() + (uint64_t)UMIO_TIMEOUT_US;
  uint32_t spins = 0;

  while (um_readl_gpio(GPIO_GPLEV0) & (1u << PIN_TXN_IN_PROGRESS)) {
    spins++;
    if ((spins & (UMIO_DEADLINE_CHECK_ITERS - 1u)) == 0u && now_us_monotonic() > deadline) {
      uint32_t gplev0 = um_readl_gpio(GPIO_GPLEV0);
      uint32_t gplev1 = um_readl_gpio(GPIO_GPLEV1);
      fprintf(stderr,
              "[ps_backend:userspace-mmio] txn timeout waiting for %s "
              "(last=%s addr=%08X fc=%u opbits=%03X read=%u pending=%u out=%u gplev0=%08X gplev1=%08X)\n",
              op_name, gu.last_txn_op ? gu.last_txn_op : "?", gu.last_txn_addr,
              (unsigned int)gu.last_txn_fc, (unsigned int)gu.last_txn_opbits,
              gu.last_txn_is_read ? 1u : 0u, gu.write_pending ? 1u : 0u, gu.data_out ? 1u : 0u,
              gplev0, gplev1);
      return -ETIMEDOUT;
    }
  }

  return 0;
}

static int um_drain_pending_write(const char* op_name) {
  int ret;

  if (!gu.write_pending) {
    return 0;
  }

  ret = um_wait_for_txn(op_name);
  gu.write_pending = false;
  return ret;
}

static void um_write_payload(uint32_t payload, uint32_t reg_sel) {
  uint32_t pins = (payload & 0x00FFFF00u) | ((reg_sel & 0x3u) << PIN_A0);
  uint32_t stretch = um_effective_wr_stretch();

  um_write_set(pins);
  um_mmio_barrier();
  for (uint32_t i = 0; i < stretch; i++) {
    um_write_set(1u << PIN_WR);
    um_mmio_barrier();
  }
  um_write_clr(1u << PIN_WR);
  um_mmio_barrier();
  um_clear_lines();
  um_mmio_barrier();
}

static int um_setup_gpclk(void) {
  uint32_t src = gu_cfg.gpclk_src;
  uint32_t div = gu_cfg.gpclk_div;
  uint32_t ctl;

  if (!gu.cprman_base || !gu.setup_gpclk) {
    return 0;
  }

  if (!gu_cfg.gpclk_src_overridden) {
    (void)parse_u32_env("PISTORM_GPCLK_SRC", &src);
  }
  if (!gu_cfg.gpclk_div_overridden) {
    (void)parse_u32_env("PISTORM_GPCLK_DIV", &div);
  }

  if (div == 0 || div > 0xFFFu) {
    fprintf(stderr, "[ps_backend:userspace-mmio] invalid PISTORM_GPCLK_DIV=%u (valid 1..4095)\n", div);
    return -EINVAL;
  }

  um_writel_cprman(CPRMAN_GP0CTL, CPRMAN_PASSWD | GPCLK_CTL_KILL);
  usleep(10);

  for (unsigned int i = 0; i < 1000; i++) {
    if (!(um_readl_cprman(CPRMAN_GP0CTL) & GPCLK_CTL_BUSY)) {
      break;
    }
    usleep(1);
  }

  um_writel_cprman(CPRMAN_GP0DIV, CPRMAN_PASSWD | (div << 12));
  usleep(10);

  um_writel_cprman(CPRMAN_GP0CTL, CPRMAN_PASSWD | (src & 0xFu) | GPCLK_CTL_ENAB);

  for (unsigned int i = 0; i < 1000; i++) {
    ctl = um_readl_cprman(CPRMAN_GP0CTL);
    if (ctl & GPCLK_CTL_BUSY) {
      gu.gpclk_ready = true;
      printf("[ps_backend:userspace-mmio] gpclk0 configured (src=%u div=%u)\n", src, div);
      return 0;
    }
    usleep(1);
  }

  fprintf(stderr, "[ps_backend:userspace-mmio] gpclk0 failed to start (src=%u div=%u)\n", src, div);
  return -ETIMEDOUT;
}

static int um_setup_protocol(void) {
  um_prepare_fsel();
  gu.data_out = true;
  um_set_bus_dir(false);
  um_clear_lines();
  return um_setup_gpclk();
}

static inline uint32_t um_addr_hi_payload(uint32_t addr, uint16_t opbits, uint8_t fc) {
  /*
   * Hardware truth source: rtl.fc/pistorm_fc.v
   * PI_D[15:13]=FC, PI_D[9]=RW, PI_D[8]=byte/word, PI_D[7:0]=A23..A16.
   */
  return ((uint32_t)((fc & 0x7u) << 13) | opbits | (addr >> 16)) & 0xFFFFu;
}

static int um_write16_fc(uint32_t addr, uint16_t data, uint8_t fc) {
  int ret;
  uint8_t fc7 = um_effective_fc(fc);
  bool sticky_ram = gu.ramseq_enable && um_addr_is_chip_ram(addr, 2u);
  bool pipe_ram = um_wpipe_allowed(addr, 2u, fc7);

  ret = um_drain_pending_write(pipe_ram ? "write16_fc[pending]" : "write16_fc[drain]");
  if (ret < 0) {
    um_set_bus_dir(false);
    return ret;
  }

  um_set_bus_dir(true);
  um_write_payload(((uint32_t)data & 0xFFFFu) << 8, REG_DATA);
  um_write_payload((addr & 0xFFFFu) << 8, REG_ADDR_LO);
  um_note_txn("write16_fc", addr, fc7, 0x0000u, false);

  if (pipe_ram) {
    um_write_payload(um_addr_hi_payload(addr, 0x0000u, fc7) << 8, REG_ADDR_HI);
    gu.write_pending = true;
    if (!sticky_ram) {
      um_set_bus_dir(false);
    }
    return 0;
  }
  um_write_payload(um_addr_hi_payload(addr, 0x0000u, fc7) << 8, REG_ADDR_HI);
  if (!sticky_ram) {
    um_set_bus_dir(false);
  }
  return um_wait_for_txn("write16_fc");
}

static int um_write8_fc(uint32_t addr, uint8_t data, uint8_t fc) {
  int ret;
  uint8_t fc7 = um_effective_fc(fc);
  bool sticky_ram = gu.ramseq_enable && um_addr_is_chip_ram(addr, 1u);
  bool pipe_ram = um_wpipe_allowed(addr, 1u, fc7);
  uint16_t payload = (addr & 1u) ? (uint16_t)data : (uint16_t)(data | (uint16_t)(data << 8));

  ret = um_drain_pending_write(pipe_ram ? "write8_fc[pending]" : "write8_fc[drain]");
  if (ret < 0) {
    um_set_bus_dir(false);
    return ret;
  }

  um_set_bus_dir(true);
  um_write_payload(((uint32_t)payload & 0xFFFFu) << 8, REG_DATA);
  um_write_payload((addr & 0xFFFFu) << 8, REG_ADDR_LO);
  um_note_txn("write8_fc", addr, fc7, 0x0100u, false);

  if (pipe_ram) {
    um_write_payload(um_addr_hi_payload(addr, 0x0100u, fc7) << 8, REG_ADDR_HI);
    gu.write_pending = true;
    if (!sticky_ram) {
      um_set_bus_dir(false);
    }
    return 0;
  }
  um_write_payload(um_addr_hi_payload(addr, 0x0100u, fc7) << 8, REG_ADDR_HI);
  if (!sticky_ram) {
    um_set_bus_dir(false);
  }
  return um_wait_for_txn("write8_fc");
}

static int um_read16_fc(uint32_t addr, uint16_t* out, uint8_t fc) {
  int ret;
  uint32_t value;
  uint8_t fc7 = um_effective_fc(fc);
  uint32_t stretch = um_effective_rd_stretch();

  if (!out) {
    return -EINVAL;
  }

  ret = um_drain_pending_write("read16_fc[drain]");
  if (ret < 0) {
    return ret;
  }

  um_set_bus_dir(true);
  um_write_payload((addr & 0xFFFFu) << 8, REG_ADDR_LO);
  um_note_txn("read16_fc", addr, fc7, 0x0200u, true);
  um_write_payload(um_addr_hi_payload(addr, 0x0200u, fc7) << 8, REG_ADDR_HI);

  um_set_bus_dir(false);
  um_write_set(REG_DATA << PIN_A0);
  um_mmio_barrier();
  for (uint32_t i = 0; i < stretch; i++) {
    um_write_set(1u << PIN_RD);
    um_mmio_barrier();
  }

  ret = um_wait_for_txn("read16_fc");
  value = um_readl_gpio(GPIO_GPLEV0);
  um_clear_lines();

  if (ret < 0) {
    return ret;
  }

  *out = (uint16_t)((value >> 8) & 0xFFFFu);
  return 0;
}

/*
 * Helper for a 32-bit read as two legal 16-bit cycles.
 * This keeps semantics identical while reducing software-side call overhead.
 */
static int um_read32_fc_pair(uint32_t addr, uint32_t* out, uint8_t fc) {
  uint32_t stretch = um_effective_rd_stretch();
  uint16_t hi = 0;
  uint16_t lo = 0;
  uint32_t value;
  uint8_t fc7 = um_effective_fc(fc);
  int ret;
  uint32_t addr1 = addr + 2u;

  if (!out) {
    return -EINVAL;
  }

  ret = um_drain_pending_write("read32_fc_pair[drain]");
  if (ret < 0) {
    return ret;
  }

  um_set_bus_dir(true);
  um_write_payload((addr & 0xFFFFu) << 8, REG_ADDR_LO);
  um_note_txn("read32_fc[0]", addr, fc7, 0x0200u, true);
  um_write_payload(um_addr_hi_payload(addr, 0x0200u, fc7) << 8, REG_ADDR_HI);

  um_set_bus_dir(false);
  um_mmio_barrier();
  for (uint32_t i = 0; i < stretch; i++) {
    um_write_set(1u << PIN_RD);
    um_mmio_barrier();
  }

  ret = um_wait_for_txn("read32_fc[0]");
  value = um_readl_gpio(GPIO_GPLEV0);
  um_clear_lines();
  if (ret < 0) {
    return ret;
  }
  hi = (uint16_t)((value >> 8) & 0xFFFFu);

  um_set_bus_dir(true);
  um_write_payload((addr1 & 0xFFFFu) << 8, REG_ADDR_LO);
  um_note_txn("read32_fc[1]", addr1, fc7, 0x0200u, true);
  um_write_payload(um_addr_hi_payload(addr1, 0x0200u, fc7) << 8, REG_ADDR_HI);

  um_set_bus_dir(false);
  um_mmio_barrier();
  for (uint32_t i = 0; i < stretch; i++) {
    um_write_set(1u << PIN_RD);
    um_mmio_barrier();
  }

  ret = um_wait_for_txn("read32_fc[1]");
  value = um_readl_gpio(GPIO_GPLEV0);
  um_clear_lines();
  if (ret < 0) {
    return ret;
  }
  lo = (uint16_t)((value >> 8) & 0xFFFFu);

  *out = ((uint32_t)hi << 16) | (uint32_t)lo;
  return 0;
}

static int um_read8_fc(uint32_t addr, uint8_t* out, uint8_t fc) {
  uint16_t value = 0;
  int ret;

  if (!out) {
    return -EINVAL;
  }

  ret = um_read16_fc(addr, &value, fc);
  if (ret < 0) {
    return ret;
  }

  *out = (addr & 1u) ? (uint8_t)(value & 0xFFu) : (uint8_t)((value >> 8) & 0xFFu);
  return 0;
}

/*
 * Helper for a 32-bit write as two legal 16-bit cycles while keeping GPIO
 * direction stable for the pair. This reduces Pi-side direction churn without
 * changing Amiga-visible bus semantics.
 */
static int um_write32_fc_pair(uint32_t addr, uint32_t value, uint8_t fc) {
  bool sticky_ram = gu.ramseq_enable && um_addr_is_chip_ram(addr, 4u);
  uint32_t addr1 = addr + 2u;
  uint8_t fc7 = um_effective_fc(fc);
  uint16_t hi = (uint16_t)(value >> 16);
  uint16_t lo = (uint16_t)(value & 0xFFFFu);
  int ret;

  ret = um_drain_pending_write("write32_fc_pair[drain]");
  if (ret < 0) {
    return ret;
  }

  um_set_bus_dir(true);

  um_write_payload(((uint32_t)hi & 0xFFFFu) << 8, REG_DATA);
  um_write_payload((addr & 0xFFFFu) << 8, REG_ADDR_LO);
  um_note_txn("write32_fc[0]", addr, fc7, 0x0000u, false);
  um_write_payload(um_addr_hi_payload(addr, 0x0000u, fc7) << 8, REG_ADDR_HI);
  ret = um_wait_for_txn("write32_fc[0]");
  if (ret < 0) {
    um_set_bus_dir(false);
    return ret;
  }

  um_write_payload(((uint32_t)lo & 0xFFFFu) << 8, REG_DATA);
  um_write_payload((addr1 & 0xFFFFu) << 8, REG_ADDR_LO);
  um_note_txn("write32_fc[1]", addr1, fc7, 0x0000u, false);
  um_write_payload(um_addr_hi_payload(addr1, 0x0000u, fc7) << 8, REG_ADDR_HI);
  ret = um_wait_for_txn("write32_fc[1]");

  if (!sticky_ram || ret < 0) {
    um_set_bus_dir(false);
  }
  return ret;
}

static int um_write_status(uint16_t value) {
  int ret;

  ret = um_drain_pending_write("write_status[drain]");
  if (ret < 0) {
    return ret;
  }

  if (gu.bus_arb_release) {
    value |= STATUS_BIT_BUS_ARB;
  } else {
    value &= (uint16_t)~STATUS_BIT_BUS_ARB;
  }

  um_set_bus_dir(true);
  um_write_payload(((uint32_t)value & 0xFFFFu) << 8, REG_STATUS);
  um_set_bus_dir(false);
  return 0;
}

static int um_read_status(uint16_t* out) {
  int ret;
  uint32_t value;
  uint32_t stretch = um_effective_rd_stretch();

  if (!out) {
    return -EINVAL;
  }

  ret = um_drain_pending_write("read_status[drain]");
  if (ret < 0) {
    return ret;
  }

  um_set_bus_dir(false);
  um_write_set(REG_STATUS << PIN_A0);
  um_mmio_barrier();
  for (uint32_t i = 0; i < stretch; i++) {
    um_write_set(1u << PIN_RD);
    um_mmio_barrier();
  }

  value = um_readl_gpio(GPIO_GPLEV0);
  um_clear_lines();

  *out = (uint16_t)((value >> 8) & 0xFFFFu);
  return 0;
}

static int um_map_region(int fd, uint32_t phys, size_t span, void** map_base_out, size_t* map_len_out,
                         volatile uint32_t** reg_base_out) {
  long page_size = sysconf(_SC_PAGESIZE);
  size_t page;
  off_t aligned;
  off_t delta;
  size_t map_len;
  void* mapped;

  if (page_size <= 0) {
    page_size = 4096;
  }
  page = (size_t)page_size;

  aligned = (off_t)(phys & ~(uint32_t)(page - 1u));
  delta = (off_t)(phys - (uint32_t)aligned);
  map_len = (size_t)delta + span;
  map_len = (map_len + page - 1u) & ~(page - 1u);

  mapped = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, aligned);
  if (mapped == MAP_FAILED) {
    return -1;
  }

  *map_base_out = mapped;
  *map_len_out = map_len;
  *reg_base_out = (volatile uint32_t*)((uint8_t*)mapped + delta);
  return 0;
}

static int um_init(struct ps_ctx* ctx) {
  uint32_t periph_base;
  int using_gpiomem = 0;
  (void)ctx;

  if (access("/sys/module/pistorm", F_OK) == 0 || access("/dev/pistorm", F_OK) == 0) {
    fprintf(stderr, "[ps_backend:userspace-mmio] pistorm.ko appears loaded; unload it first "
                    "(sudo rmmod pistorm)\n");
    return -EBUSY;
  }

  if (gu.gpio_fd >= 0 || gu.mem_fd >= 0) {
    return 0;
  }

  gu.berr_reset_input = (parse_bool_env("PISTORM_MMIO_BERR_RESET_INPUT", 1) != 0);
  gu.bus_arb_release = (parse_bool_env("PISTORM_MMIO_BUS_ARB_RELEASE", 0) != 0);
  gu.setup_gpclk = (parse_bool_env("PISTORM_MMIO_SETUP_GPCLK", 1) != 0);
  gu.trace_txn = (parse_bool_env("PISTORM_MMIO_TRACE_TXN", 0) != 0);
  gu.trace_txn_only_nonzero_fc = (parse_bool_env("PISTORM_MMIO_TRACE_TXN_NONZERO_FC", 0) != 0);
  gu.trace_txn_limit_reported = false;
  gu.trace_txn_count = 0u;
  gu.trace_txn_limit = 0u;
  (void)parse_u32_env("PISTORM_MMIO_TRACE_TXN_LIMIT", &gu.trace_txn_limit);
  gu.force_fc0 = (parse_bool_env("PISTORM_MMIO_FORCE_FC0", 0) != 0);
  gu.lwpair_enable = gu_cfg.lwpair_enable;
  gu.lrpair_enable = gu_cfg.r32pair_enable;
  gu.ramseq_enable = gu_cfg.ramseq_enable;
  gu.wpipe_enable = gu_cfg.wpipe_enable;
  gu.write_pending = false;

  if (!gu_cfg.lwpair_overridden) {
    bool v = false;
    if (parse_bool_env_value("PISTORM_MMIO_LWPAIR", &v) == 0) {
      gu.lwpair_enable = v;
    }
  }
  if (!gu_cfg.r32pair_overridden) {
    bool v = false;
    if (parse_bool_env_value("PISTORM_MMIO_R32PAIR", &v) == 0) {
      gu.lrpair_enable = v;
    }
  }
  if (!gu_cfg.ramseq_overridden) {
    bool v = false;
    if (parse_bool_env_value("PISTORM_MMIO_RAMSEQ", &v) == 0) {
      gu.ramseq_enable = v;
    }
  }
  if (!gu_cfg.wpipe_overridden) {
    bool v = false;
    if (parse_bool_env_value("PISTORM_MMIO_WPIPE", &v) == 0) {
      gu.wpipe_enable = v;
    }
  }

  if (gu.wpipe_enable) {
    fprintf(stderr, "[ps_backend:userspace-mmio] WPIPE enabled in conservative mode "
                    "(non-FC fc=0 chip-ram writes only)\n");
  }
  if (gu.force_fc0) {
    fprintf(stderr, "[ps_backend:userspace-mmio] FC override active: all transactions use fc=0\n");
  }
  if (gu.trace_txn) {
    fprintf(stderr,
            "[ps_backend:userspace-mmio] txn trace enabled (limit=%u nonzero_fc_only=%u)\n",
            gu.trace_txn_limit, gu.trace_txn_only_nonzero_fc ? 1u : 0u);
  }

  gu.wr_stretch = gu_cfg.wr_stretch;
  gu.rd_stretch = gu_cfg.rd_stretch;

  if (!gu_cfg.wr_stretch_overridden) {
    (void)parse_u32_env("PISTORM_MMIO_WR_STRETCH", &gu.wr_stretch);
  }
  if (!gu_cfg.rd_stretch_overridden) {
    (void)parse_u32_env("PISTORM_MMIO_RD_STRETCH", &gu.rd_stretch);
  }
  if (gu.wr_stretch > 64u) {
    fprintf(stderr, "[ps_backend:userspace-mmio] invalid PISTORM_MMIO_WR_STRETCH=%u (valid 0..64)\n",
            gu.wr_stretch);
    gu.wr_stretch = UMIO_WR_STRETCH_DEFAULT;
  }
  if (gu.rd_stretch > 64u) {
    fprintf(stderr, "[ps_backend:userspace-mmio] invalid PISTORM_MMIO_RD_STRETCH=%u (valid 0..64)\n",
            gu.rd_stretch);
    gu.rd_stretch = UMIO_RD_STRETCH_DEFAULT;
  }

  if (parse_u32_env("PISTORM_MMIO_GPIO_PHYS", &gu.gpio_phys) < 0 || parse_u32_env("PISTORM_MMIO_CPRMAN_PHYS", &gu.cprman_phys) < 0) {
    periph_base = default_periph_base();
    (void)detect_peripheral_base(&periph_base);
    gu.gpio_phys = periph_base + 0x00200000u;
    gu.cprman_phys = periph_base + 0x00101000u;
  }

  /*
   * Prefer /dev/gpiomem for GPIO ownership minimization.
   * /dev/mem is only needed for CPRMAN (GPCLK), which is not exposed via /dev/gpiomem.
   */
  gu.gpio_fd = open("/dev/gpiomem", O_RDWR | O_SYNC | O_CLOEXEC);
  if (gu.gpio_fd >= 0) {
    void* mapped = mmap(NULL, 0x1000u, PROT_READ | PROT_WRITE, MAP_SHARED, gu.gpio_fd, 0);
    if (mapped != MAP_FAILED) {
      gu.gpio_map_base = mapped;
      gu.gpio_map_len = 0x1000u;
      gu.gpio_base = (volatile uint32_t*)mapped;
      using_gpiomem = 1;
    } else {
      close(gu.gpio_fd);
      gu.gpio_fd = -1;
    }
  }

  if (!using_gpiomem) {
    gu.mem_fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (gu.mem_fd < 0) {
      fprintf(stderr, "[ps_backend:userspace-mmio] failed to open /dev/mem for GPIO fallback (%s)\n",
              strerror(errno));
      return -errno;
    }

    if (um_map_region(gu.mem_fd, gu.gpio_phys, 0x1000u, &gu.gpio_map_base, &gu.gpio_map_len,
                      &gu.gpio_base) < 0) {
      fprintf(stderr, "[ps_backend:userspace-mmio] failed to map GPIO @0x%08X (%s)\n", gu.gpio_phys,
              strerror(errno));
      close(gu.mem_fd);
      gu.mem_fd = -1;
      return -errno;
    }
  }

  if (gu.setup_gpclk) {
    if (gu.mem_fd < 0) {
      gu.mem_fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
      if (gu.mem_fd < 0) {
        fprintf(stderr,
                "[ps_backend:userspace-mmio] failed to open /dev/mem for CPRMAN/GPCLK (%s)\n",
                strerror(errno));
        munmap(gu.gpio_map_base, gu.gpio_map_len);
        gu.gpio_map_base = NULL;
        gu.gpio_base = NULL;
        if (gu.gpio_fd >= 0) {
          close(gu.gpio_fd);
          gu.gpio_fd = -1;
        }
        return -errno;
      }
    }

    if (um_map_region(gu.mem_fd, gu.cprman_phys, 0x1000u, &gu.cprman_map_base, &gu.cprman_map_len,
                      &gu.cprman_base) < 0) {
      fprintf(stderr, "[ps_backend:userspace-mmio] failed to map CPRMAN @0x%08X (%s)\n", gu.cprman_phys,
              strerror(errno));
      munmap(gu.gpio_map_base, gu.gpio_map_len);
      gu.gpio_map_base = NULL;
      gu.gpio_base = NULL;
      if (gu.gpio_fd >= 0) {
        close(gu.gpio_fd);
        gu.gpio_fd = -1;
      }
      close(gu.mem_fd);
      gu.mem_fd = -1;
      return -errno;
    }
  }

  if (!gu.backend_logged) {
    printf("[ps_backend] backend=userspace-mmio (gpio=%s, cprman=%s, wr_stretch=%u, rd_stretch=%u, lwpair=%u, r32pair=%u, ramseq=%u, wpipe=%u)\n",
           using_gpiomem ? "/dev/gpiomem" : "/dev/mem",
           gu.setup_gpclk ? "/dev/mem" : "not-mapped", gu.wr_stretch, gu.rd_stretch,
           gu.lwpair_enable ? 1u : 0u, gu.lrpair_enable ? 1u : 0u, gu.ramseq_enable ? 1u : 0u,
           gu.wpipe_enable ? 1u : 0u);
    gu.backend_logged = 1;
  }

  return 0;
}

static void um_shutdown(struct ps_ctx* ctx) {
  (void)ctx;
  (void)um_drain_pending_write("shutdown[drain]");

  if (gu.cprman_map_base && gu.setup_gpclk) {
    um_writel_cprman(CPRMAN_GP0CTL, CPRMAN_PASSWD | GPCLK_CTL_KILL);
    usleep(10);
  }

  if (gu.gpio_map_base) {
    munmap(gu.gpio_map_base, gu.gpio_map_len);
    gu.gpio_map_base = NULL;
    gu.gpio_base = NULL;
  }

  if (gu.cprman_map_base) {
    munmap(gu.cprman_map_base, gu.cprman_map_len);
    gu.cprman_map_base = NULL;
    gu.cprman_base = NULL;
  }

  if (gu.mem_fd >= 0) {
    close(gu.mem_fd);
    gu.mem_fd = -1;
  }
  if (gu.gpio_fd >= 0) {
    close(gu.gpio_fd);
    gu.gpio_fd = -1;
  }

  gu.data_out = false;
}

static int um_setup(struct ps_ctx* ctx) {
  (void)ctx;
  if (!gu.gpio_base) {
    return -ENODEV;
  }
  return um_setup_protocol();
}

static uint32_t um_get_status(struct ps_ctx* ctx) {
  uint16_t status = 0;
  (void)ctx;
  if (um_read_status(&status) < 0) {
    return 0;
  }
  return (uint32_t)status;
}

static int um_set_status(struct ps_ctx* ctx, uint16_t value) {
  (void)ctx;
  return um_write_status(value);
}

static int um_reset_sm(struct ps_ctx* ctx) {
  (void)ctx;
  if (um_write_status(STATUS_BIT_INIT) < 0) {
    return -1;
  }
  usleep(1500);
  if (um_write_status(0) < 0) {
    return -1;
  }
  usleep(100);
  return 0;
}

static int um_pulse_reset(struct ps_ctx* ctx) {
  (void)ctx;
  if (um_write_status(0) < 0) {
    return -1;
  }
  usleep(100000);
  return um_write_status(STATUS_BIT_RESET);
}

static int um_read8_op(struct ps_ctx* ctx, uint32_t addr, uint8_t fc, uint8_t* out) {
  (void)ctx;
  return um_read8_fc(addr, out, fc & 0x7u);
}

static int um_read16_op(struct ps_ctx* ctx, uint32_t addr, uint8_t fc, uint16_t* out) {
  (void)ctx;
  return um_read16_fc(addr, out, fc & 0x7u);
}

static int um_read32_op(struct ps_ctx* ctx, uint32_t addr, uint8_t fc, uint32_t* out) {
  uint16_t hi = 0;
  uint16_t lo = 0;
  uint8_t fc7 = (uint8_t)(fc & 0x7u);
  int ret;
  (void)ctx;

  if (!out) {
    return -EINVAL;
  }

  if (gu.lrpair_enable) {
    return um_read32_fc_pair(addr, out, fc7);
  }

  ret = um_read16_fc(addr, &hi, fc7);
  if (ret < 0) {
    return ret;
  }

  ret = um_read16_fc(addr + 2u, &lo, fc7);
  if (ret < 0) {
    return ret;
  }

  *out = ((uint32_t)hi << 16) | (uint32_t)lo;
  return 0;
}

static int um_write8_op(struct ps_ctx* ctx, uint32_t addr, uint8_t value, uint8_t fc) {
  (void)ctx;
  return um_write8_fc(addr, value, fc & 0x7u);
}

static int um_write16_op(struct ps_ctx* ctx, uint32_t addr, uint16_t value, uint8_t fc) {
  (void)ctx;
  return um_write16_fc(addr, value, fc & 0x7u);
}

static int um_write32_op(struct ps_ctx* ctx, uint32_t addr, uint32_t value, uint8_t fc) {
  int ret;
  (void)ctx;

  if (!gu.lwpair_enable) {
    ret = um_write16_fc(addr, (uint16_t)(value >> 16), (uint8_t)(fc & 0x7u));
    if (ret < 0) {
      return ret;
    }
    return um_write16_fc(addr + 2u, (uint16_t)(value & 0xFFFFu), (uint8_t)(fc & 0x7u));
  }

  return um_write32_fc_pair(addr, value, (uint8_t)(fc & 0x7u));
}

static int um_get_pins(struct ps_ctx* ctx, struct pistorm_pins* pins) {
  (void)ctx;
  if (!pins) {
    return -EINVAL;
  }

  pins->gplev0 = um_readl_gpio(GPIO_GPLEV0);
  pins->gplev1 = um_readl_gpio(GPIO_GPLEV1);
  return 0;
}

static int um_run_batch(struct ps_ctx* ctx, struct pistorm_busop_v2* ops, size_t count, uint32_t flags) {
  (void)ctx;
  (void)flags;

  if (!ops) {
    return -EINVAL;
  }

  for (size_t i = 0; i < count; i++) {
    struct pistorm_busop_v2* op = &ops[i];
    int ret = 0;

    op->status = 0;

    switch (op->width) {
    case PISTORM_W8:
      if (op->op == 0) {
        uint8_t v8 = 0;
        ret = um_read8_fc(op->addr, &v8, (uint8_t)(op->fc & 0x7u));
        op->value = v8;
      } else {
        ret = um_write8_fc(op->addr, (uint8_t)op->value, (uint8_t)(op->fc & 0x7u));
      }
      break;

    case PISTORM_W16:
      if (op->op == 0) {
        uint16_t v16 = 0;
        ret = um_read16_fc(op->addr, &v16, (uint8_t)(op->fc & 0x7u));
        op->value = v16;
      } else {
        ret = um_write16_fc(op->addr, (uint16_t)op->value, (uint8_t)(op->fc & 0x7u));
      }
      break;

    case PISTORM_W32:
      if (op->op == 0) {
        uint32_t v32 = 0;
        ret = um_read32_op(NULL, op->addr, (uint8_t)(op->fc & 0x7u), &v32);
        op->value = v32;
      } else {
        ret = um_write32_op(NULL, op->addr, op->value, (uint8_t)(op->fc & 0x7u));
      }
      break;

    default:
      op->status = -EINVAL;
      return 0;
    }

    if (ret < 0) {
      op->status = ret;
      return 0;
    }

    if (um_drain_pending_write("run_batch[drain]") < 0) {
      op->status = -ETIMEDOUT;
      return 0;
    }

    if (gu.berr_reset_input && !(um_readl_gpio(GPIO_GPLEV0) & (1u << PIN_RESET))) {
      op->status |= PISTORM_BUSOP_ST_BERR;
    }
  }

  return 0;
}

static int um_flush(struct ps_ctx* ctx) {
  (void)ctx;
  return um_drain_pending_write("flush[drain]");
}

static void um_dump_stats(struct ps_ctx* ctx) {
  (void)ctx;
  fprintf(stderr, "[PS_PROTO] userspace-mmio backend has no queue stats\n");
}

static volatile uint32_t* um_gpio_regs(struct ps_ctx* ctx) {
  (void)ctx;
  return gu.gpio_base;
}

const struct ps_backend_ops ps_backend_userspace_mmio_ops = {
    .init = um_init,
    .shutdown = um_shutdown,
    .setup = um_setup,
    .get_status = um_get_status,
    .set_status = um_set_status,
    .reset_sm = um_reset_sm,
    .pulse_reset = um_pulse_reset,
    .read8 = um_read8_op,
    .read16 = um_read16_op,
    .read32 = um_read32_op,
    .write8 = um_write8_op,
    .write16 = um_write16_op,
    .write32 = um_write32_op,
    .get_pins = um_get_pins,
    .run_batch = um_run_batch,
    .flush = um_flush,
    .dump_stats = um_dump_stats,
    .gpio_regs = um_gpio_regs,
};
