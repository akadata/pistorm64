// SPDX-License-Identifier: MIT

#include "pistorm/backend.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

#define STATUS_BIT_ARB_EN 0x0100u
#define STATUS_BIT_BGACK 0x0200u
#define STATUS_BIT_BG 0x0400u
#define STATUS_BIT_BR 0x0800u
#define STATUS_BIT_ARB_REL 0x1000u
#define STATUS_MASK_IPL 0xE000u
#define STATUS_SHIFT_IPL 13u

#ifndef PISTORM_ENABLE_BATCH
#define PISTORM_ENABLE_BATCH 1
#endif

#ifndef PISTORM_ENABLE_QUEUE
#define PISTORM_ENABLE_QUEUE 1
#endif

#if PISTORM_ENABLE_BATCH
#ifndef PISTORM_BATCH_MAX
#define PISTORM_BATCH_MAX 256
#endif
#endif

#ifndef PS_QUEUE_BACKOFF_NS
#define PS_QUEUE_BACKOFF_NS 200000
#endif

#ifndef PS_QUEUE_MAX_RETRIES
#define PS_QUEUE_MAX_RETRIES 200
#endif

struct ps_kmod_state {
  int fd;
  int backend_logged;

  volatile uint32_t gpio_shadow[32];

  bool queue_enabled;
  bool queue_mode_initialized;
  bool queue_error_logged;
  bool fc_v2_unavailable_logged;
  uint64_t queue_full_events;
  uint64_t queue_full_fallbacks;

  uint16_t last_status_debug;
  bool status_debug_valid;

#if PISTORM_ENABLE_BATCH
  unsigned int batch_max_ops;
  uint8_t batch_last_fc;
  struct pistorm_busop opsq[PISTORM_BATCH_MAX];
  uint32_t opsq_n;
#endif
};

static struct ps_kmod_state gk = {
  .fd = -1,
  .queue_enabled = (PISTORM_ENABLE_QUEUE != 0),
#if PISTORM_ENABLE_BATCH
  .batch_max_ops = PISTORM_BATCH_MAX,
  .batch_last_fc = 0xFF,
#endif
};

#if PISTORM_ENABLE_BATCH
static void kmod_batch_init(void) {
  const char* ops_env = getenv("PISTORM_BATCH_OPS");
  if (ops_env && *ops_env) {
    char* end = NULL;
    unsigned long ops = strtoul(ops_env, &end, 10);
    if (end && *end == '\0' && ops > 0) {
      if (ops > PISTORM_BATCH_MAX) {
        ops = PISTORM_BATCH_MAX;
      }
      gk.batch_max_ops = (unsigned int)ops;
      return;
    }
  }

  const char* bits_env = getenv("PISTORM_BATCH_BITS");
  if (bits_env && *bits_env) {
    char* end = NULL;
    unsigned long bits = strtoul(bits_env, &end, 10);
    if (end && *end == '\0' && bits >= 64) {
      if (bits > 2560) {
        bits = 2560;
      }
      unsigned long ops = bits / 32;
      if (ops == 0) {
        ops = 1;
      }
      if (ops > PISTORM_BATCH_MAX) {
        ops = PISTORM_BATCH_MAX;
      }
      gk.batch_max_ops = (unsigned int)ops;
    }
  }
}

static inline int kmod_busop_batch(int ps_fd, struct pistorm_busop* ops, uint32_t count) {
  struct pistorm_batch b = {
      .ops_count = count,
      .ops_ptr = (uint64_t)(uintptr_t)ops,
      .reserved = 0,
  };
  return ioctl(ps_fd, PISTORM_IOC_BATCH, &b);
}

static inline int kmod_busopq_flush(int ps_fd) {
  int rc;

  if (!gk.opsq_n) {
    return 0;
  }

  rc = kmod_busop_batch(ps_fd, gk.opsq, gk.opsq_n);
  gk.opsq_n = 0;
  return rc;
}

static inline int kmod_busopq_push(int ps_fd, const struct pistorm_busop* op) {
  gk.opsq[gk.opsq_n++] = *op;
  if (gk.opsq_n >= gk.batch_max_ops) {
    return kmod_busopq_flush(ps_fd);
  }
  return 0;
}
#endif

static void kmod_queue_init_from_env(void) {
  const char* env;

  if (gk.queue_mode_initialized) {
    return;
  }

  gk.queue_mode_initialized = true;
  env = getenv("PISTORM_ENABLE_QUEUE");

  if (!env || !*env) {
    return;
  }

  if (strcmp(env, "0") == 0 || strcasecmp(env, "false") == 0 || strcasecmp(env, "off") == 0 ||
      strcasecmp(env, "no") == 0) {
    gk.queue_enabled = false;
  } else {
    gk.queue_enabled = true;
  }
}

static int kmod_open_dev(void) {
  kmod_queue_init_from_env();

  if (gk.fd >= 0) {
    return 0;
  }

  gk.fd = open("/dev/pistorm", O_RDWR | O_CLOEXEC);
  if (gk.fd < 0) {
    if (!gk.backend_logged) {
      fprintf(stderr, "[ps_backend:kmod] /dev/pistorm missing (%s)\n", strerror(errno));
      gk.backend_logged = 1;
    }
    return -1;
  }

  if (!gk.backend_logged) {
    printf("[ps_backend] backend=kmod (/dev/pistorm)\n");
    gk.backend_logged = 1;
  }

#if PISTORM_ENABLE_BATCH
  kmod_batch_init();
#endif
  return 0;
}

static int kmod_init(struct ps_ctx* ctx) {
  (void)ctx;
  return kmod_open_dev();
}

static void kmod_shutdown(struct ps_ctx* ctx) {
  (void)ctx;

#if PISTORM_ENABLE_BATCH
  if (gk.fd >= 0) {
    (void)kmod_busopq_flush(gk.fd);
  }
#endif

  if (gk.fd >= 0) {
    close(gk.fd);
    gk.fd = -1;
  }
}

static int kmod_setup(struct ps_ctx* ctx) {
  (void)ctx;

  if (kmod_open_dev() < 0) {
    return -1;
  }
  return ioctl(gk.fd, PISTORM_IOC_SETUP);
}

static void kmod_queue_disable(const char* reason, int err) {
  if (!gk.queue_enabled) {
    return;
  }

  gk.queue_enabled = false;
  if (!gk.queue_error_logged) {
    fprintf(stderr, "[ps_backend:kmod] queue disabled (%s: %s)\n", reason, strerror(err));
    gk.queue_error_logged = true;
  }
}

static void kmod_queue_log_full_event(void) {
  struct pistorm_queue_stats stats;

  if (gk.queue_full_events != 1 && (gk.queue_full_events & 0xFF) != 0) {
    return;
  }

  if (ioctl(gk.fd, PISTORM_IOC_QUEUE_STATS, &stats) == 0) {
    LOG_VERBOSE("[PS_QUEUE] full_events=%" PRIu64 " depth=%u max=%u\n", gk.queue_full_events,
                stats.current_depth, stats.max_depth);
    return;
  }

  LOG_VERBOSE("[PS_QUEUE] full_events=%" PRIu64 " (stats unavailable: %s)\n", gk.queue_full_events,
              strerror(errno));
}

static void kmod_flush_queue_before_read(void) {
  if (!gk.queue_enabled) {
    return;
  }

  if (kmod_open_dev() < 0) {
    return;
  }

  if (ioctl(gk.fd, PISTORM_IOC_QUEUE_FLUSH) < 0) {
    kmod_queue_disable("queue flush", errno);
  }
}

static int kmod_queue_enqueue_backpressure(const struct pistorm_busop* op) {
  int tries = 0;

  for (;;) {
    if (ioctl(gk.fd, PISTORM_IOC_QUEUE_ENQUEUE, op) == 0) {
      return 0;
    }

    if (errno != ENOSPC) {
      kmod_queue_disable("queue enqueue", errno);
      return -1;
    }

    gk.queue_full_events++;
    kmod_queue_log_full_event();

    if (ioctl(gk.fd, PISTORM_IOC_QUEUE_FLUSH) < 0) {
      kmod_queue_disable("queue flush", errno);
      return -1;
    }

    if (++tries >= PS_QUEUE_MAX_RETRIES) {
      return -2;
    }

    {
      struct timespec ts = {
          .tv_sec = 0,
          .tv_nsec = PS_QUEUE_BACKOFF_NS,
      };
      nanosleep(&ts, NULL);
    }
  }
}

static int kmod_busop(int is_read, int width, uint32_t addr, uint32_t* val, uint16_t flags) {
  struct pistorm_busop op;
  int rc;

  if (kmod_open_dev() < 0) {
    return -1;
  }

#if PISTORM_ENABLE_BATCH
  if (is_read && gk.opsq_n > 0) {
    (void)kmod_busopq_flush(gk.fd);
  }

  if (!is_read) {
    op.addr = addr;
    op.value = val ? *val : 0;
    op.width = (unsigned char)width;
    op.is_read = (unsigned char)is_read;
    op.flags = flags;
    op.status = 0;
    return kmod_busopq_push(gk.fd, &op);
  }
#endif

  op.addr = addr;
  op.value = val ? *val : 0;
  op.width = (unsigned char)width;
  op.is_read = (unsigned char)is_read;
  op.flags = flags;
  op.status = 0;

  rc = ioctl(gk.fd, PISTORM_IOC_BUSOP, &op);
  if (rc == 0) {
    if (is_read && val) {
      *val = op.value;
    }
    if (op.status & PISTORM_BUSOP_ST_BERR) {
      LOG_VERBOSE("[BERR] bus error observed addr=0x%08x\n", addr);
    }
  }

  return rc;
}

static int kmod_busop_fc(int is_read, int width, uint32_t addr, uint32_t* val, uint8_t fc) {
  const uint8_t fc7 = (uint8_t)(fc & 0x7u);

  if (fc7 == 0u) {
    return kmod_busop(is_read, width, addr, val, 0);
  }

  if (kmod_open_dev() < 0) {
    return -1;
  }

  struct pistorm_busop_v2 op = {
      .op = (uint8_t)(is_read ? 0u : 1u),
      .width = (uint8_t)width,
      .fc = fc7,
      .flags = 0,
      .addr = addr,
      .value = val ? *val : 0u,
      .status = 0,
  };
  struct pistorm_run_batch batch = {
      .count = 1u,
      .flags = 0u,
      .ops_ptr = (uint64_t)(uintptr_t)&op,
  };

  if (ioctl(gk.fd, PISTORM_IOC_RUN_BATCH, &batch) < 0) {
    if (!gk.fc_v2_unavailable_logged && (errno == ENOTTY || errno == EINVAL)) {
      LOG_ERROR("[FC] kmod FC path unavailable (RUN_BATCH disabled). Enable run_batch_enable=1.\n");
      gk.fc_v2_unavailable_logged = true;
    }
    return -1;
  }

  if (op.status < 0) {
    errno = (int)-op.status;
    return -1;
  }

  if (is_read && val) {
    *val = op.value;
  }

  if ((uint32_t)op.status & PISTORM_BUSOP_ST_BERR) {
    LOG_VERBOSE("[BERR] bus error observed addr=0x%08x fc=%u\n", addr, (unsigned)fc7);
  }

  return 0;
}

static void kmod_queue_write(uint32_t addr, unsigned int width, uint32_t value) {
  struct pistorm_busop op;
  uint32_t temp = value;
  int rc;

  if (!gk.queue_enabled || kmod_open_dev() < 0) {
    (void)kmod_busop(0, (int)width, addr, &temp, 0);
    return;
  }

  op.addr = addr;
  op.value = value;
  op.width = (unsigned char)width;
  op.is_read = 0;
  op.flags = 0;
  op.status = 0;

  rc = kmod_queue_enqueue_backpressure(&op);
  if (rc == 0) {
    return;
  }

  if (rc == -2) {
    gk.queue_full_fallbacks++;
    LOG_VERBOSE("[PS_QUEUE] fallback sync write (full_events=%" PRIu64 " fallbacks=%" PRIu64
                ")\n",
                gk.queue_full_events, gk.queue_full_fallbacks);
  }

  (void)kmod_busop(0, (int)width, addr, &temp, 0);
}

static int kmod_reset_sm(struct ps_ctx* ctx) {
  (void)ctx;

  if (kmod_open_dev() < 0) {
    return -1;
  }

  return ioctl(gk.fd, PISTORM_IOC_RESET_SM);
}

static int kmod_pulse_reset(struct ps_ctx* ctx) {
  (void)ctx;

  if (kmod_open_dev() < 0) {
    return -1;
  }

  return ioctl(gk.fd, PISTORM_IOC_PULSE_RESET);
}

static int kmod_read8(struct ps_ctx* ctx, uint32_t addr, uint8_t fc, uint8_t* out) {
  uint32_t v = 0;
  (void)ctx;

  kmod_flush_queue_before_read();
  if (kmod_busop_fc(1, PISTORM_W8, addr, &v, fc) < 0) {
    return -1;
  }
  if (out) {
    *out = (uint8_t)(v & 0xFFu);
  }
  return 0;
}

static int kmod_read16(struct ps_ctx* ctx, uint32_t addr, uint8_t fc, uint16_t* out) {
  uint32_t v = 0;
  (void)ctx;

  kmod_flush_queue_before_read();
  if (kmod_busop_fc(1, PISTORM_W16, addr, &v, fc) < 0) {
    return -1;
  }
  if (out) {
    *out = (uint16_t)(v & 0xFFFFu);
  }
  return 0;
}

static int kmod_read32(struct ps_ctx* ctx, uint32_t addr, uint8_t fc, uint32_t* out) {
  uint32_t v = 0;
  (void)ctx;

  kmod_flush_queue_before_read();
  if (kmod_busop_fc(1, PISTORM_W32, addr, &v, fc) < 0) {
    return -1;
  }
  if (out) {
    *out = v;
  }
  return 0;
}

static int kmod_write8(struct ps_ctx* ctx, uint32_t addr, uint8_t value, uint8_t fc) {
  uint32_t v = (uint32_t)value;
  (void)ctx;

  if ((fc & 0x7u) == 0u) {
    kmod_queue_write(addr, PISTORM_W8, v);
    return 0;
  }

  kmod_flush_queue_before_read();
  if (kmod_busop_fc(0, PISTORM_W8, addr, &v, fc) < 0) {
    return -1;
  }
  return 0;
}

static int kmod_write16(struct ps_ctx* ctx, uint32_t addr, uint16_t value, uint8_t fc) {
  uint32_t v = (uint32_t)value;
  (void)ctx;

  if ((fc & 0x7u) == 0u) {
    kmod_queue_write(addr, PISTORM_W16, v);
    return 0;
  }

  kmod_flush_queue_before_read();
  if (kmod_busop_fc(0, PISTORM_W16, addr, &v, fc) < 0) {
    return -1;
  }
  return 0;
}

static int kmod_write32(struct ps_ctx* ctx, uint32_t addr, uint32_t value, uint8_t fc) {
  uint32_t v = value;
  (void)ctx;

  if ((fc & 0x7u) == 0u) {
    kmod_queue_write(addr, PISTORM_W32, v);
    return 0;
  }

  kmod_flush_queue_before_read();
  if (kmod_busop_fc(0, PISTORM_W32, addr, &v, fc) < 0) {
    return -1;
  }
  return 0;
}

static uint32_t kmod_get_status(struct ps_ctx* ctx) {
  uint32_t value = 0;
  (void)ctx;

  kmod_flush_queue_before_read();
  if (kmod_busop(1, PISTORM_W16, 0, &value, PISTORM_BUSOP_F_STATUS) < 0) {
    return 0;
  }

  if (log_get_level() >= LOG_LEVEL_DEBUG) {
    uint16_t status = (uint16_t)(value & 0xFFFFu);
    uint16_t prev = gk.last_status_debug;

    if (!gk.status_debug_valid ||
        ((status ^ prev) &
         (STATUS_BIT_ARB_REL | STATUS_BIT_BR | STATUS_BIT_BG | STATUS_BIT_BGACK | STATUS_BIT_ARB_EN)) != 0) {
      LOG_DEBUG("[BUS_ARB] status=0x%04X ipl=%u arb_en=%u br=%u bg=%u bgack=%u relinquished=%u\n", status,
                (unsigned)((status & STATUS_MASK_IPL) >> STATUS_SHIFT_IPL), !!(status & STATUS_BIT_ARB_EN),
                !!(status & STATUS_BIT_BR), !!(status & STATUS_BIT_BG), !!(status & STATUS_BIT_BGACK),
                !!(status & STATUS_BIT_ARB_REL));
    }

    gk.last_status_debug = status;
    gk.status_debug_valid = true;
  }

  return value;
}

static int kmod_set_status(struct ps_ctx* ctx, uint16_t value) {
  uint32_t v = value;
  (void)ctx;
  return kmod_busop(0, PISTORM_W16, 0, &v, PISTORM_BUSOP_F_STATUS);
}

static int kmod_get_pins(struct ps_ctx* ctx, struct pistorm_pins* pins) {
  (void)ctx;

  if (!pins) {
    return -EINVAL;
  }

  if (kmod_open_dev() < 0) {
    pins->gplev0 = gk.gpio_shadow[13];
    pins->gplev1 = gk.gpio_shadow[14];
    return -1;
  }

  if (ioctl(gk.fd, PISTORM_IOC_GET_PINS, pins) < 0) {
    return -1;
  }

  gk.gpio_shadow[13] = pins->gplev0;
  gk.gpio_shadow[14] = pins->gplev1;
  return 0;
}

static int kmod_run_batch(struct ps_ctx* ctx, struct pistorm_busop_v2* ops, size_t count,
                          uint32_t flags) {
  struct pistorm_run_batch batch;
  (void)ctx;

  if (!ops || !count || count > UINT32_MAX) {
    return -EINVAL;
  }

  if (kmod_open_dev() < 0) {
    return -1;
  }

  batch.count = (uint32_t)count;
  batch.flags = flags;
  batch.ops_ptr = (uint64_t)(uintptr_t)ops;
  return ioctl(gk.fd, PISTORM_IOC_RUN_BATCH, &batch);
}

static int kmod_flush(struct ps_ctx* ctx) {
  int rc = 0;
  (void)ctx;

  if (kmod_open_dev() < 0) {
    return -1;
  }

  if (gk.queue_enabled) {
    if (ioctl(gk.fd, PISTORM_IOC_QUEUE_FLUSH) < 0) {
      kmod_queue_disable("queue flush", errno);
      rc = -1;
    }
  }

#if PISTORM_ENABLE_BATCH
  if (kmod_busopq_flush(gk.fd) < 0) {
    rc = -1;
  }
#endif

  return rc;
}

static void kmod_dump_stats(struct ps_ctx* ctx) {
  struct pistorm_queue_stats stats;
  (void)ctx;

  if (!gk.queue_enabled) {
    fprintf(stderr, "[PS_PROTO] queue disabled (PISTORM_ENABLE_QUEUE=0)\n");
    return;
  }

  if (kmod_open_dev() < 0) {
    fprintf(stderr, "[PS_PROTO] queue stats unavailable (device offline)\n");
    return;
  }

  if (ioctl(gk.fd, PISTORM_IOC_QUEUE_STATS, &stats) < 0) {
    fprintf(stderr, "[PS_PROTO] queue stats unavailable (%s)\n", strerror(errno));
    return;
  }

  fprintf(stderr,
          "[PS_PROTO] queue: enqueued=%llu drained=%llu depth=%u max=%u full_events=%" PRIu64
          " fallbacks=%" PRIu64 "\n",
          (unsigned long long)stats.enqueued, (unsigned long long)stats.drained, stats.current_depth,
          stats.max_depth, gk.queue_full_events, gk.queue_full_fallbacks);
}

static volatile uint32_t* kmod_gpio_regs(struct ps_ctx* ctx) {
  (void)ctx;
  return gk.gpio_shadow;
}

const struct ps_backend_ops ps_backend_kmod_ops = {
    .init = kmod_init,
    .shutdown = kmod_shutdown,
    .setup = kmod_setup,
    .get_status = kmod_get_status,
    .set_status = kmod_set_status,
    .reset_sm = kmod_reset_sm,
    .pulse_reset = kmod_pulse_reset,
    .read8 = kmod_read8,
    .read16 = kmod_read16,
    .read32 = kmod_read32,
    .write8 = kmod_write8,
    .write16 = kmod_write16,
    .write32 = kmod_write32,
    .get_pins = kmod_get_pins,
    .run_batch = kmod_run_batch,
    .flush = kmod_flush,
    .dump_stats = kmod_dump_stats,
    .gpio_regs = kmod_gpio_regs,
};
