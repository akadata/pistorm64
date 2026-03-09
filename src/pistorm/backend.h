// SPDX-License-Identifier: MIT

#ifndef PISTORM_BACKEND_H
#define PISTORM_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include "include/uapi/linux/pistorm.h"

struct ps_ctx;

struct ps_backend_ops {
  int (*init)(struct ps_ctx* ctx);
  void (*shutdown)(struct ps_ctx* ctx);
  int (*setup)(struct ps_ctx* ctx);

  uint32_t (*get_status)(struct ps_ctx* ctx);
  int (*set_status)(struct ps_ctx* ctx, uint16_t value);

  int (*reset_sm)(struct ps_ctx* ctx);
  int (*pulse_reset)(struct ps_ctx* ctx);

  int (*read8)(struct ps_ctx* ctx, uint32_t addr, uint8_t fc, uint8_t* out);
  int (*read16)(struct ps_ctx* ctx, uint32_t addr, uint8_t fc, uint16_t* out);
  int (*read32)(struct ps_ctx* ctx, uint32_t addr, uint8_t fc, uint32_t* out);

  int (*write8)(struct ps_ctx* ctx, uint32_t addr, uint8_t value, uint8_t fc);
  int (*write16)(struct ps_ctx* ctx, uint32_t addr, uint16_t value, uint8_t fc);
  int (*write32)(struct ps_ctx* ctx, uint32_t addr, uint32_t value, uint8_t fc);

  int (*get_pins)(struct ps_ctx* ctx, struct pistorm_pins* pins);
  int (*run_batch)(struct ps_ctx* ctx, struct pistorm_busop_v2* ops, size_t count, uint32_t flags);
  int (*flush)(struct ps_ctx* ctx);

  void (*dump_stats)(struct ps_ctx* ctx);
  volatile uint32_t* (*gpio_regs)(struct ps_ctx* ctx);
};

struct ps_ctx {
  const struct ps_backend_ops* ops;
  const char* backend_name;
  void* impl;
};

extern const struct ps_backend_ops ps_backend_kmod_ops;
extern const struct ps_backend_ops ps_backend_userspace_mmio_ops;

int ps_backend_select(const char* name, int from_config);
const char* ps_backend_selected_name(void);
const char* ps_backend_active_name(void);
int ps_backend_is_initialized(void);
int ps_backend_userspace_set_gpclk_src(uint32_t src);
int ps_backend_userspace_set_gpclk_div(uint32_t div);
int ps_backend_userspace_set_wr_stretch(uint32_t count);
int ps_backend_userspace_set_rd_stretch(uint32_t count);
int ps_backend_userspace_set_lwpair(uint32_t enabled);
int ps_backend_userspace_set_r32pair(uint32_t enabled);
int ps_backend_userspace_set_ramseq(uint32_t enabled);
int ps_backend_userspace_set_wpipe(uint32_t enabled);

int ps_backend_setup_protocol(void);
void ps_backend_shutdown(void);

int ps_backend_reset_sm(void);
int ps_backend_pulse_reset(void);

int ps_backend_read8(uint32_t addr, uint8_t fc, uint8_t* out);
int ps_backend_read16(uint32_t addr, uint8_t fc, uint16_t* out);
int ps_backend_read32(uint32_t addr, uint8_t fc, uint32_t* out);

int ps_backend_write8(uint32_t addr, uint8_t value, uint8_t fc);
int ps_backend_write16(uint32_t addr, uint16_t value, uint8_t fc);
int ps_backend_write32(uint32_t addr, uint32_t value, uint8_t fc);

uint16_t ps_backend_read_status(void);
int ps_backend_write_status(uint16_t value);

int ps_backend_get_pins(struct pistorm_pins* pins);
int ps_backend_run_batch(struct pistorm_busop_v2* ops, size_t count, uint32_t flags);
int ps_backend_flush(void);

void ps_backend_dump_stats(void);
volatile uint32_t* ps_backend_gpio_regs(void);

#endif
