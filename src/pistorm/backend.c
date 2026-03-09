// SPDX-License-Identifier: MIT

#include "pistorm/backend.h"
#include "pistorm/backend_userspace_mmio.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef enum ps_backend_kind {
  PS_BACKEND_KIND_KMOD = 0,
  PS_BACKEND_KIND_USERSPACE_MMIO,
} ps_backend_kind_t;

static ps_backend_kind_t g_selected_kind = PS_BACKEND_KIND_KMOD;
static struct ps_ctx g_ctx;
static int g_setup_done;

static int ps_backend_name_to_kind(const char* name, ps_backend_kind_t* kind_out) {
  if (!name || !name[0] || !kind_out) {
    return -EINVAL;
  }

  if (strcasecmp(name, "kmod") == 0 ||
      strcasecmp(name, "kernel") == 0 ||
      strcasecmp(name, "kerrnel") == 0) {
    *kind_out = PS_BACKEND_KIND_KMOD;
    return 0;
  }

  if (strcasecmp(name, "userspace") == 0 ||
      strcasecmp(name, "userspace-mmio") == 0 ||
      strcasecmp(name, "usermmio") == 0) {
    *kind_out = PS_BACKEND_KIND_USERSPACE_MMIO;
    return 0;
  }

  return -EINVAL;
}

static const char* ps_backend_kind_name(ps_backend_kind_t kind) {
  switch (kind) {
  case PS_BACKEND_KIND_KMOD:
    return "kmod";
  case PS_BACKEND_KIND_USERSPACE_MMIO:
    return "userspace-mmio";
  default:
    return "kmod";
  }
}

static int ps_backend_ensure_init(void) {
  int ret = 0;

  if (g_ctx.ops) {
    return 0;
  }

  memset(&g_ctx, 0x00, sizeof(g_ctx));

  switch (g_selected_kind) {
  case PS_BACKEND_KIND_KMOD:
    g_ctx.ops = &ps_backend_kmod_ops;
    g_ctx.backend_name = "kmod";
    break;
  case PS_BACKEND_KIND_USERSPACE_MMIO:
    g_ctx.ops = &ps_backend_userspace_mmio_ops;
    g_ctx.backend_name = "userspace-mmio";
    break;
  default:
    return -EINVAL;
  }

  if (!g_ctx.ops || !g_ctx.ops->init) {
    return -EINVAL;
  }

  printf("[ps_backend] selected=%s\n", g_ctx.backend_name);
  ret = g_ctx.ops->init(&g_ctx);
  if (ret < 0) {
    fprintf(stderr, "[ps_backend] init failed for backend=%s (%d)\n", g_ctx.backend_name, ret);
    memset(&g_ctx, 0x00, sizeof(g_ctx));
    return ret;
  }

  return 0;
}

static int ps_backend_ensure_ready(void) {
  int ret = ps_backend_ensure_init();
  if (ret < 0) {
    return ret;
  }

  if (g_setup_done) {
    return 0;
  }

  if (g_ctx.ops && g_ctx.ops->setup) {
    ret = g_ctx.ops->setup(&g_ctx);
    if (ret < 0) {
      fprintf(stderr, "[ps_backend] setup failed for backend=%s (%d)\n", g_ctx.backend_name, ret);
      return ret;
    }
  }

  g_setup_done = 1;
  return 0;
}

int ps_backend_select(const char* name, int from_config) {
  (void)from_config;
  ps_backend_kind_t kind;

  if (!name || !name[0]) {
    return -EINVAL;
  }

  if (ps_backend_name_to_kind(name, &kind) < 0) {
    fprintf(stderr, "[ps_backend] unknown backend '%s' (valid: kernel|userspace)\n", name);
    return -EINVAL;
  }

  if (g_ctx.ops) {
    if (kind != g_selected_kind) {
      fprintf(stderr,
              "[ps_backend] backend already initialized as %s; runtime switch to %s denied\n",
              ps_backend_kind_name(g_selected_kind),
              ps_backend_kind_name(kind));
      return -EBUSY;
    }
    return 0;
  }

  g_selected_kind = kind;
  return 0;
}

int ps_backend_userspace_set_gpclk_src(uint32_t src) {
  if (g_ctx.ops) {
    fprintf(stderr, "[ps_backend] userspace gpclk_src change denied after backend init\n");
    return -EBUSY;
  }
  return ps_userspace_mmio_set_gpclk_src(src);
}

int ps_backend_userspace_set_gpclk_div(uint32_t div) {
  if (g_ctx.ops) {
    fprintf(stderr, "[ps_backend] userspace gpclk_div change denied after backend init\n");
    return -EBUSY;
  }
  return ps_userspace_mmio_set_gpclk_div(div);
}

int ps_backend_userspace_set_wr_stretch(uint32_t count) {
  if (g_ctx.ops) {
    fprintf(stderr, "[ps_backend] userspace wr_stretch change denied after backend init\n");
    return -EBUSY;
  }
  return ps_userspace_mmio_set_wr_stretch(count);
}

int ps_backend_userspace_set_rd_stretch(uint32_t count) {
  if (g_ctx.ops) {
    fprintf(stderr, "[ps_backend] userspace rd_stretch change denied after backend init\n");
    return -EBUSY;
  }
  return ps_userspace_mmio_set_rd_stretch(count);
}

int ps_backend_userspace_set_lwpair(uint32_t enabled) {
  if (g_ctx.ops) {
    fprintf(stderr, "[ps_backend] userspace lwpair change denied after backend init\n");
    return -EBUSY;
  }
  return ps_userspace_mmio_set_lwpair(enabled);
}

int ps_backend_userspace_set_r32pair(uint32_t enabled) {
  if (g_ctx.ops) {
    fprintf(stderr, "[ps_backend] userspace r32pair change denied after backend init\n");
    return -EBUSY;
  }
  return ps_userspace_mmio_set_r32pair(enabled);
}

int ps_backend_userspace_set_ramseq(uint32_t enabled) {
  if (g_ctx.ops) {
    fprintf(stderr, "[ps_backend] userspace ramseq change denied after backend init\n");
    return -EBUSY;
  }
  return ps_userspace_mmio_set_ramseq(enabled);
}

int ps_backend_userspace_set_wpipe(uint32_t enabled) {
  if (g_ctx.ops) {
    fprintf(stderr, "[ps_backend] userspace wpipe change denied after backend init\n");
    return -EBUSY;
  }
  return ps_userspace_mmio_set_wpipe(enabled);
}

const char* ps_backend_selected_name(void) {
  return ps_backend_kind_name(g_selected_kind);
}

const char* ps_backend_active_name(void) {
  if (g_ctx.backend_name && g_ctx.backend_name[0]) {
    return g_ctx.backend_name;
  }
  return ps_backend_kind_name(g_selected_kind);
}

int ps_backend_is_initialized(void) {
  return g_ctx.ops ? 1 : 0;
}

int ps_backend_setup_protocol(void) {
  return ps_backend_ensure_ready();
}

void ps_backend_shutdown(void) {
  if (g_ctx.ops && g_ctx.ops->shutdown) {
    g_ctx.ops->shutdown(&g_ctx);
  }
  memset(&g_ctx, 0x00, sizeof(g_ctx));
  g_setup_done = 0;
}

int ps_backend_reset_sm(void) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->reset_sm) {
    return -1;
  }
  return g_ctx.ops->reset_sm(&g_ctx);
}

int ps_backend_pulse_reset(void) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->pulse_reset) {
    return -1;
  }
  return g_ctx.ops->pulse_reset(&g_ctx);
}

int ps_backend_read8(uint32_t addr, uint8_t fc, uint8_t* out) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->read8) {
    return -1;
  }
  return g_ctx.ops->read8(&g_ctx, addr, fc, out);
}

int ps_backend_read16(uint32_t addr, uint8_t fc, uint16_t* out) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->read16) {
    return -1;
  }
  return g_ctx.ops->read16(&g_ctx, addr, fc, out);
}

int ps_backend_read32(uint32_t addr, uint8_t fc, uint32_t* out) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->read32) {
    return -1;
  }
  return g_ctx.ops->read32(&g_ctx, addr, fc, out);
}

int ps_backend_write8(uint32_t addr, uint8_t value, uint8_t fc) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->write8) {
    return -1;
  }
  return g_ctx.ops->write8(&g_ctx, addr, value, fc);
}

int ps_backend_write16(uint32_t addr, uint16_t value, uint8_t fc) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->write16) {
    return -1;
  }
  return g_ctx.ops->write16(&g_ctx, addr, value, fc);
}

int ps_backend_write32(uint32_t addr, uint32_t value, uint8_t fc) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->write32) {
    return -1;
  }
  return g_ctx.ops->write32(&g_ctx, addr, value, fc);
}

uint16_t ps_backend_read_status(void) {
  uint32_t status = 0;

  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->get_status) {
    return 0;
  }

  status = g_ctx.ops->get_status(&g_ctx);
  return (uint16_t)(status & 0xFFFFu);
}

int ps_backend_write_status(uint16_t value) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->set_status) {
    return -1;
  }
  return g_ctx.ops->set_status(&g_ctx, value);
}

int ps_backend_get_pins(struct pistorm_pins* pins) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->get_pins) {
    return -1;
  }
  return g_ctx.ops->get_pins(&g_ctx, pins);
}

int ps_backend_run_batch(struct pistorm_busop_v2* ops, size_t count, uint32_t flags) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->run_batch) {
    return -1;
  }
  return g_ctx.ops->run_batch(&g_ctx, ops, count, flags);
}

int ps_backend_flush(void) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->flush) {
    return -1;
  }
  return g_ctx.ops->flush(&g_ctx);
}

void ps_backend_dump_stats(void) {
  if (ps_backend_ensure_init() < 0 || !g_ctx.ops || !g_ctx.ops->dump_stats) {
    return;
  }
  g_ctx.ops->dump_stats(&g_ctx);
}

volatile uint32_t* ps_backend_gpio_regs(void) {
  if (ps_backend_ensure_ready() < 0 || !g_ctx.ops || !g_ctx.ops->gpio_regs) {
    return NULL;
  }
  return g_ctx.ops->gpio_regs(&g_ctx);
}
