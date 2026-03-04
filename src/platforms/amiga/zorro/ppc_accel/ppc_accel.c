// SPDX-License-Identifier: MIT
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/amiga_zorro.h"
#include "ppc/ppc_mailbox.h"
#include "ppc/qemu_uae_loader.h"
#include "ppc_accel.h"
#include "ppc_accel_regs.h"
#include "log.h"

static const uint32_t PPC_ACCEL_RESET_WINDOW_BASE = 0xFFF00000u;
static const uint32_t PPC_ACCEL_FIRMWARE_ENTRY_PRIMARY = 0x00000000u;
static const uint32_t PPC_ACCEL_FIRMWARE_ENTRY_SECONDARY = 0x00000100u;

_Static_assert(PPC_ACCEL_MB_OFF_MAGIC == PPC_MAILBOX_OFF_MAGIC,
               "mailbox magic offset mismatch");
_Static_assert(PPC_ACCEL_MB_OFF_VERSION == PPC_MAILBOX_OFF_ABI_VERSION,
               "mailbox abi offset mismatch");
_Static_assert(PPC_ACCEL_MB_OFF_SEQ == PPC_MAILBOX_OFF_SEQ,
               "mailbox seq offset mismatch");
_Static_assert(PPC_ACCEL_MB_OFF_ACK_SEQ == PPC_MAILBOX_OFF_ACK_SEQ,
               "mailbox ack_seq offset mismatch");
_Static_assert(PPC_ACCEL_MB_OFF_CMD == PPC_MAILBOX_OFF_CMD,
               "mailbox cmd offset mismatch");
_Static_assert(PPC_ACCEL_MB_OFF_STATUS == PPC_MAILBOX_OFF_STATUS,
               "mailbox status offset mismatch");
_Static_assert(PPC_ACCEL_MB_OFF_ARG0 == PPC_MAILBOX_OFF_ARG0,
               "mailbox arg0 offset mismatch");
_Static_assert(PPC_ACCEL_MB_OFF_ARG1 == PPC_MAILBOX_OFF_ARG1,
               "mailbox arg1 offset mismatch");
_Static_assert(PPC_ACCEL_MB_OFF_RESULT0 == PPC_MAILBOX_OFF_RESULT0,
               "mailbox result0 offset mismatch");
_Static_assert(PPC_ACCEL_MB_OFF_RESULT1 == PPC_MAILBOX_OFF_RESULT1,
               "mailbox result1 offset mismatch");
_Static_assert(PPC_ACCEL_MB_MAGIC == PPC_MAILBOX_MAGIC,
               "mailbox magic value mismatch");
_Static_assert(PPC_ACCEL_MB_VERSION == PPC_MAILBOX_VERSION,
               "mailbox version value mismatch");
_Static_assert(PPC_ACCEL_MAILBOX_SIZE == PPC_MAILBOX_SIZE,
               "mailbox size mismatch");
_Static_assert(PPC_ACCEL_REG_WINDOW_SIZE == 0x1000u,
               "register window size changed");
_Static_assert(PPC_ACCEL_MAILBOX_OFFSET == PPC_ACCEL_REG_WINDOW_SIZE,
               "mailbox offset no longer follows register window");
_Static_assert(PPC_ACCEL_SHARED_OFFSET == (PPC_ACCEL_MAILBOX_OFFSET + PPC_ACCEL_MAILBOX_SIZE),
               "shared offset no longer follows mailbox page");
_Static_assert((PPC_ACCEL_SHARED_OFFSET + PPC_ACCEL_SHARED_SIZE) == PPC_ACCEL_Z2_SIZE,
               "device shared window no longer reaches end of Z2 aperture");
_Static_assert(PPC_ACCEL_SHARED_INFO_OFFSET == PPC_ACCEL_SHARED_OFFSET,
               "shared info block must start at shared window base");
_Static_assert(PPC_ACCEL_SHARED_INFO_SIZE == 0x20u,
               "shared info block size changed");
_Static_assert(PPC_ACCEL_SHARED_INFO_OFF_SIGNATURE == 0x00u,
               "shared info signature offset changed");
_Static_assert(PPC_ACCEL_SHARED_INFO_OFF_ABI_VERSION == 0x04u,
               "shared info abi offset changed");
_Static_assert(PPC_ACCEL_SHARED_INFO_OFF_MB_OFFSET == 0x08u,
               "shared info mailbox offset changed");
_Static_assert(PPC_ACCEL_SHARED_INFO_OFF_MB_SIZE == 0x0cu,
               "shared info mailbox size offset changed");
_Static_assert(PPC_ACCEL_SHARED_INFO_OFF_DB_REG == 0x10u,
               "shared info doorbell offset changed");
_Static_assert(PPC_ACCEL_SHARED_INFO_OFF_FEATURES == 0x14u,
               "shared info feature offset changed");
_Static_assert(PPC_ACCEL_SHARED_INFO_OFF_RESERVED0 == 0x18u,
               "shared info reserved0 offset changed");
_Static_assert(PPC_ACCEL_SHARED_INFO_OFF_RESERVED1 == 0x1cu,
               "shared info reserved1 offset changed");
_Static_assert(PPC_ACCEL_CTRL_START == 0x00000001u,
               "control start bit changed");
_Static_assert(PPC_ACCEL_CTRL_RESET == 0x00000002u,
               "control reset bit changed");
_Static_assert(PPC_ACCEL_CTRL_IRQ_ENABLE == 0x00000004u,
               "control irq-enable bit changed");
_Static_assert(PPC_ACCEL_IRQ_CMD_DONE == 0x00000001u,
               "cmd_done irq bit changed");
_Static_assert(PPC_ACCEL_IRQ_HOST_DOORBELL == 0x00000002u,
               "host doorbell irq bit changed");

typedef struct ppc_accel_host_service_context {
  uint8_t *mailbox;
  uint8_t *ram;
  uint32_t ram_size;
  int idle_sleep_ms;
  bool doorbell_enabled;
  bool verbose;
  qemu_uae_external_interrupt_function external_interrupt;
  volatile bool stop;
} ppc_accel_host_service_context_t;

typedef enum ppc_accel_runtime_state {
  PPC_ACCEL_RUNTIME_STOPPED = 0,
  PPC_ACCEL_RUNTIME_STARTING = 1,
  PPC_ACCEL_RUNTIME_PAUSED = 2,
  PPC_ACCEL_RUNTIME_RUNNING = 3
} ppc_accel_runtime_state_t;

typedef struct ppc_accel_state {
  uint8_t window[PPC_ACCEL_Z2_SIZE];
  qemu_uae_loader loader;
  ppc_accel_host_service_context_t host_service;
  pthread_t host_thread;
  uint32_t control;
  uint32_t status;
  uint32_t irq_status;
  uint32_t last_ack_seq;
  bool have_last_ack_seq;
  bool loader_open;
  bool runtime_started;
  bool host_thread_started;
  bool verbose;
  bool qemu_log_enabled;
  ppc_accel_runtime_state_t runtime_state;
} ppc_accel_state_t;

static ppc_accel_state_t g_ppc_accel_state;
static bool g_ppc_qemu_log_enabled;
static bool g_ppc_accel_registered;
static bool g_ppc_accel_atexit_installed;

#define PPC_ACCEL_AC_SERIAL 0x10000001u

static void ppc_accel_write_shared_info(ppc_accel_state_t *state);

static const char *ppc_accel_runtime_state_name(ppc_accel_runtime_state_t state)
{
  switch (state) {
  case PPC_ACCEL_RUNTIME_STOPPED:
    return "stopped";
  case PPC_ACCEL_RUNTIME_STARTING:
    return "starting";
  case PPC_ACCEL_RUNTIME_PAUSED:
    return "paused";
  case PPC_ACCEL_RUNTIME_RUNNING:
    return "running";
  default:
    return "unknown";
  }
}

static uint8_t ppc_accel_rom[] = {
    Z2_Z2,
    AC_MEM_SIZE_64KB,
    (uint8_t)((PPC_ACCEL_PRODUCT_ID >> 4) & 0x0Fu),
    (uint8_t)(PPC_ACCEL_PRODUCT_ID & 0x0Fu),
    0x0,
    0x0,
    0x0,
    0x0,
    (uint8_t)((PPC_ACCEL_MANUFACTURER_ID >> 12) & 0x0Fu),
    (uint8_t)((PPC_ACCEL_MANUFACTURER_ID >> 8) & 0x0Fu),
    (uint8_t)((PPC_ACCEL_MANUFACTURER_ID >> 4) & 0x0Fu),
    (uint8_t)(PPC_ACCEL_MANUFACTURER_ID & 0x0Fu),
    /* er_SerialNumber nibbles (manufacturer-defined; keep non-zero for tooling heuristics). */
    (uint8_t)((PPC_ACCEL_AC_SERIAL >> 28) & 0x0Fu),
    (uint8_t)((PPC_ACCEL_AC_SERIAL >> 24) & 0x0Fu),
    (uint8_t)((PPC_ACCEL_AC_SERIAL >> 20) & 0x0Fu),
    (uint8_t)((PPC_ACCEL_AC_SERIAL >> 16) & 0x0Fu),
    (uint8_t)((PPC_ACCEL_AC_SERIAL >> 12) & 0x0Fu),
    (uint8_t)((PPC_ACCEL_AC_SERIAL >> 8) & 0x0Fu),
    (uint8_t)((PPC_ACCEL_AC_SERIAL >> 4) & 0x0Fu),
    (uint8_t)(PPC_ACCEL_AC_SERIAL & 0x0Fu),
    0x0,
    0x0,
    0x0,
};

static void ppc_accel_qemu_log(const char *format, ...)
{
  char message[1024];
  va_list args;

  if (g_ppc_qemu_log_enabled == false) {
    return;
  }

  va_start(args, format);
  (void)vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  fprintf(stderr, "[PPC-QEMU] %s", message);
}

static uint32_t read_be32(const uint8_t *base, uint32_t offset)
{
  return ((uint32_t)base[offset + 0u] << 24u)
         | ((uint32_t)base[offset + 1u] << 16u)
         | ((uint32_t)base[offset + 2u] << 8u)
         | (uint32_t)base[offset + 3u];
}

static void write_be32(uint8_t *base, uint32_t offset, uint32_t value)
{
  base[offset + 0u] = (uint8_t)((value >> 24u) & 0xFFu);
  base[offset + 1u] = (uint8_t)((value >> 16u) & 0xFFu);
  base[offset + 2u] = (uint8_t)((value >> 8u) & 0xFFu);
  base[offset + 3u] = (uint8_t)(value & 0xFFu);
}

static void sleep_ms(int milliseconds)
{
  struct timespec request;
  struct timespec remainder;

  if (milliseconds <= 0) {
    return;
  }

  request.tv_sec = milliseconds / 1000;
  request.tv_nsec = (long)(milliseconds % 1000) * 1000000L;

  while (nanosleep(&request, &remainder) != 0) {
    if (errno != EINTR) {
      break;
    }
    request = remainder;
  }
}

static bool monotonic_ns(uint64_t *out_ns)
{
  struct timespec ts;

  if (out_ns == NULL) {
    return false;
  }
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return false;
  }

  *out_ns = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
  return true;
}

static int env_int_or_default(const char *name, int default_value)
{
  const char *value;
  char *end;
  long parsed;

  value = getenv(name);
  if ((value == NULL) || (value[0] == '\0')) {
    return default_value;
  }

  errno = 0;
  parsed = strtol(value, &end, 0);
  if ((errno != 0) || (end == value) || (*end != '\0')) {
    return default_value;
  }
  if ((parsed < 1L) || (parsed > 0x7fffffffL)) {
    return default_value;
  }

  return (int)parsed;
}

static int env_nonneg_int_or_default(const char *name, int default_value)
{
  const char *value;
  char *end;
  long parsed;

  value = getenv(name);
  if ((value == NULL) || (value[0] == '\0')) {
    return default_value;
  }

  errno = 0;
  parsed = strtol(value, &end, 0);
  if ((errno != 0) || (end == value) || (*end != '\0')) {
    return default_value;
  }
  if ((parsed < 0L) || (parsed > 0x7fffffffL)) {
    return default_value;
  }

  return (int)parsed;
}

static uint32_t env_u32_or_default(const char *name, uint32_t default_value)
{
  const char *value;
  char *end;
  unsigned long parsed;

  value = getenv(name);
  if ((value == NULL) || (value[0] == '\0')) {
    return default_value;
  }

  errno = 0;
  parsed = strtoul(value, &end, 0);
  if ((errno != 0) || (end == value) || (*end != '\0')) {
    return default_value;
  }
  if (parsed > 0xffffffffUL) {
    return default_value;
  }

  return (uint32_t)parsed;
}

static bool env_bool_or_default(const char *name, bool default_value)
{
  const char *value;

  value = getenv(name);
  if ((value == NULL) || (value[0] == '\0')) {
    return default_value;
  }

  if ((strcmp(value, "1") == 0) || (strcmp(value, "true") == 0)
      || (strcmp(value, "TRUE") == 0) || (strcmp(value, "yes") == 0)
      || (strcmp(value, "YES") == 0) || (strcmp(value, "on") == 0)
      || (strcmp(value, "ON") == 0)) {
    return true;
  }
  if ((strcmp(value, "0") == 0) || (strcmp(value, "false") == 0)
      || (strcmp(value, "FALSE") == 0) || (strcmp(value, "no") == 0)
      || (strcmp(value, "NO") == 0) || (strcmp(value, "off") == 0)
      || (strcmp(value, "OFF") == 0)) {
    return false;
  }

  return default_value;
}

static bool io_read32(uint32_t addr, uint32_t *data, int size)
{
  (void)addr;
  (void)size;
  if (data != NULL) {
    *data = 0xDEADBEEFu;
  }
  return true;
}

static bool io_write32(uint32_t addr, uint32_t data, int size)
{
  (void)addr;
  (void)data;
  (void)size;
  return true;
}

static bool io_read64(uint32_t addr, uint64_t *data)
{
  (void)addr;
  if (data != NULL) {
    *data = 0xDEADBEEFDEADBEEFULL;
  }
  return true;
}

static bool io_write64(uint32_t addr, uint64_t data)
{
  (void)addr;
  (void)data;
  return true;
}

static uint32_t crc32_ieee(const uint8_t *data, uint32_t len)
{
  uint32_t crc;
  uint32_t i;
  uint32_t j;

  crc = 0xffffffffU;
  for (i = 0u; i < len; i++) {
    crc ^= (uint32_t)data[i];
    for (j = 0u; j < 8u; j++) {
      if ((crc & 1u) != 0u) {
        crc = (crc >> 1u) ^ 0xedb88320u;
      } else {
        crc >>= 1u;
      }
    }
  }

  return crc ^ 0xffffffffu;
}

static void ppc_accel_hostsvc_ring_doorbell(const ppc_accel_host_service_context_t *ctx)
{
  if ((ctx->doorbell_enabled == false) || (ctx->external_interrupt == NULL)) {
    return;
  }

  ctx->external_interrupt(true);
  ctx->external_interrupt(false);
}

static bool ppc_accel_hostsvc_handle_request(
    ppc_accel_host_service_context_t *ctx,
    uint32_t req_seq)
{
  uint32_t cmd;
  uint32_t arg0;
  uint32_t arg1;
  uint32_t status;
  uint32_t result0;
  uint32_t result1;

  cmd = read_be32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_CMD);
  arg0 = read_be32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_ARG0);
  arg1 = read_be32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_ARG1);

  status = PPC_MAILBOX_STATUS_ERR;
  result0 = 0u;
  result1 = 0u;

  if (cmd == PPC_MAILBOX_HOST_CMD_TIME32) {
    uint64_t ns;

    if (monotonic_ns(&ns) == true) {
      result0 = (uint32_t)(ns & 0xffffffffu);
      status = PPC_MAILBOX_STATUS_DONE;
    }
  } else if (cmd == PPC_MAILBOX_HOST_CMD_MEM_CRC32) {
    uint64_t end_addr;

    end_addr = (uint64_t)arg0 + (uint64_t)arg1;
    if ((arg0 >= ctx->ram_size) || (end_addr > ctx->ram_size)) {
      status = PPC_MAILBOX_STATUS_RANGE;
    } else {
      result0 = crc32_ieee(ctx->ram + arg0, arg1);
      status = PPC_MAILBOX_STATUS_DONE;
    }
  }

  write_be32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_RESULT0, result0);
  write_be32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_RESULT1, result1);
  write_be32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_STATUS, status);
  PPC_MAILBOX_MEMORY_BARRIER();
  write_be32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_ACK_SEQ, req_seq);
  PPC_MAILBOX_MEMORY_BARRIER();
  ppc_accel_hostsvc_ring_doorbell(ctx);

  if (ctx->verbose == true) {
    LOG_INFO("[PPC-ACCEL] hostsvc req_seq=%" PRIu32 " cmd=%" PRIu32
             " status=%" PRIu32 " result0=0x%08" PRIx32 "\n",
             req_seq, cmd, status, result0);
  }

  return status == PPC_MAILBOX_STATUS_DONE;
}

static void *ppc_accel_hostsvc_thread_main(void *opaque)
{
  ppc_accel_host_service_context_t *ctx;

  ctx = (ppc_accel_host_service_context_t *)opaque;
  if (ctx == NULL) {
    return NULL;
  }

  while (ctx->stop == false) {
    uint32_t req_seq;
    uint32_t ack_seq;

    req_seq = read_be32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_REQ_SEQ);
    ack_seq = read_be32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_ACK_SEQ);
    if (req_seq == ack_seq) {
      if (ctx->idle_sleep_ms > 0) {
        sleep_ms(ctx->idle_sleep_ms);
      } else {
        sched_yield();
      }
      continue;
    }

    (void)ppc_accel_hostsvc_handle_request(ctx, req_seq);
  }

  return NULL;
}

typedef struct wait_started_context {
  qemu_uae_loader *loader;
} wait_started_context;

static void *wait_started_thread_main(void *opaque)
{
  wait_started_context *context;

  context = (wait_started_context *)opaque;
  if (context == NULL) {
    return NULL;
  }

  if ((context->loader->qemu_uae_mutex_lock != NULL)
      && (context->loader->qemu_uae_mutex_unlock != NULL)) {
    context->loader->qemu_uae_mutex_lock();
    context->loader->qemu_uae_wait_until_started();
    context->loader->qemu_uae_mutex_unlock();
  } else {
    context->loader->qemu_uae_wait_until_started();
  }

  return NULL;
}

static bool wait_until_started_with_timeout(qemu_uae_loader *loader, int timeout_ms)
{
  wait_started_context context;
  pthread_t wait_thread;
  struct timespec deadline;
  int rc;

  if (timeout_ms <= 0) {
    timeout_ms = 2000;
  }

  context.loader = loader;
  rc = pthread_create(&wait_thread, NULL, wait_started_thread_main, &context);
  if (rc != 0) {
    LOG_WARN("[PPC-ACCEL] pthread_create(wait_until_started) failed: %s\n", strerror(rc));
    return false;
  }

  rc = clock_gettime(CLOCK_REALTIME, &deadline);
  if (rc != 0) {
    LOG_WARN("[PPC-ACCEL] clock_gettime failed while waiting for start: %s\n", strerror(errno));
    (void)pthread_cancel(wait_thread);
    (void)pthread_join(wait_thread, NULL);
    return false;
  }

  deadline.tv_sec += timeout_ms / 1000;
  deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec += 1;
    deadline.tv_nsec -= 1000000000L;
  }

  rc = pthread_timedjoin_np(wait_thread, NULL, &deadline);
  if (rc == 0) {
    return true;
  }
  if (rc == ETIMEDOUT) {
    LOG_WARN("[PPC-ACCEL] timeout waiting for qemu_uae_wait_until_started (%d ms)\n", timeout_ms);
    (void)pthread_cancel(wait_thread);
    (void)pthread_join(wait_thread, NULL);
    return false;
  }

  LOG_WARN("[PPC-ACCEL] pthread_timedjoin_np(wait_until_started) failed: %s\n", strerror(rc));
  (void)pthread_cancel(wait_thread);
  (void)pthread_join(wait_thread, NULL);
  return false;
}

static bool install_mailbox_firmware(uint8_t *ram, uint32_t ram_size, uint32_t entry_offset,
                                     uint32_t mailbox_base)
{
  static const uint32_t program_words[] = {
      0x3c600000u, 0x60632000u, 0x80830008u, 0x80a3000cu, 0x7c042800u, 0x4182fff4u,
      0x38c00001u, 0x90c30014u, 0x80e30010u, 0x2c070001u, 0x4182002cu, 0x2c070002u,
      0x41820040u, 0x2c070003u, 0x41820088u, 0x2c070004u, 0x418200e0u, 0x38c00003u,
      0x90c30014u, 0x9083000cu, 0x4bffffb8u, 0x81030018u, 0x7d0940f8u, 0x91230020u,
      0x39400002u, 0x91430014u, 0x9083000cu, 0x4bffff9cu, 0x81030018u, 0x39200040u,
      0x7c084800u, 0x40810008u, 0x7d284b78u, 0x39430040u, 0x39630140u, 0x2c080000u,
      0x4182001cu, 0x7d0903a6u, 0x818a0000u, 0x918b0000u, 0x394a0004u, 0x396b0004u,
      0x4200fff0u, 0x91030020u, 0x38c00002u, 0x90c30014u, 0x9083000cu, 0x4bffff4cu,
      0x82230240u, 0x3a310001u, 0x3a400001u, 0x92430248u, 0x3a600000u, 0x92630250u,
      0x92630254u, 0x9263024cu, 0x7c0004acu, 0x92230240u, 0x82830244u, 0x7c148800u,
      0x4082fff8u, 0x82a3024cu, 0x82c30258u, 0x82e3025cu, 0x92c30020u, 0x92a30024u,
      0x2c150002u, 0x40820078u, 0x38c00002u, 0x90c30014u, 0x9083000cu, 0x4bfffeecu,
      0x82230240u, 0x3a310001u, 0x3a400002u, 0x92430248u, 0x82630018u, 0x8283001cu,
      0x92630250u, 0x92830254u, 0x3aa00000u, 0x92a3024cu, 0x7c0004acu, 0x92230240u,
      0x82c30244u, 0x7c168800u, 0x4082fff8u, 0x82e3024cu, 0x83030258u, 0x93030020u,
      0x92e30024u, 0x2c170002u, 0x40820014u, 0x38c00002u, 0x90c30014u, 0x9083000cu,
      0x4bfffe88u, 0x38c00003u, 0x90c30014u, 0x9083000cu, 0x4bfffe78u};
  uint32_t word_count;
  uint32_t program_size;
  uint32_t i;

  word_count = (uint32_t)(sizeof(program_words) / sizeof(program_words[0]));
  program_size = word_count * 4u;

  if ((entry_offset + program_size) > ram_size) {
    return false;
  }

  for (i = 0u; i < word_count; i++) {
    uint32_t value;

    value = program_words[i];
    if (i == 0u) {
      value = 0x3c600000u | ((mailbox_base >> 16u) & 0xffffu);
    } else if (i == 1u) {
      value = 0x60630000u | (mailbox_base & 0xffffu);
    }

    write_be32(ram, entry_offset + (i * 4u), value);
  }

  return true;
}

static void ppc_accel_mailbox_reset(ppc_accel_state_t *state)
{
  memset(state->window + PPC_ACCEL_MAILBOX_OFFSET, 0, PPC_ACCEL_MAILBOX_SIZE);
  memset(state->window + PPC_ACCEL_SHARED_OFFSET, 0, PPC_ACCEL_SHARED_SIZE);

  write_be32(state->window, PPC_ACCEL_MAILBOX_OFFSET + PPC_MAILBOX_OFF_MAGIC, PPC_MAILBOX_MAGIC);
  write_be32(
      state->window,
      PPC_ACCEL_MAILBOX_OFFSET + PPC_MAILBOX_OFF_ABI_VERSION,
      PPC_MAILBOX_VERSION);
  write_be32(
      state->window,
      PPC_ACCEL_MAILBOX_OFFSET + PPC_MAILBOX_OFF_STATUS,
      PPC_MAILBOX_STATUS_IDLE);
  write_be32(
      state->window,
      PPC_ACCEL_MAILBOX_OFFSET + PPC_MAILBOX_OFF_HOST_STATUS,
      PPC_MAILBOX_STATUS_IDLE);
  ppc_accel_write_shared_info(state);

  state->have_last_ack_seq = false;
  state->last_ack_seq = 0u;
}

static void ppc_accel_poll_mailbox(ppc_accel_state_t *state)
{
  uint32_t mailbox_base;
  uint32_t seq;
  uint32_t ack_seq;
  uint32_t status;

  if ((state->status & PPC_ACCEL_STATUS_RUNNING) == 0u) {
    return;
  }

  mailbox_base = PPC_ACCEL_MAILBOX_OFFSET;
  seq = read_be32(state->window, mailbox_base + PPC_MAILBOX_OFF_SEQ);
  ack_seq = read_be32(state->window, mailbox_base + PPC_MAILBOX_OFF_ACK_SEQ);
  status = read_be32(state->window, mailbox_base + PPC_MAILBOX_OFF_STATUS);

  if (state->have_last_ack_seq == false) {
    state->last_ack_seq = ack_seq;
    state->have_last_ack_seq = true;
    return;
  }

  if (ack_seq != state->last_ack_seq) {
    state->last_ack_seq = ack_seq;
    if (((state->control & PPC_ACCEL_CTRL_IRQ_ENABLE) != 0u)
        && (ack_seq == seq)
        && ((status == PPC_MAILBOX_STATUS_DONE)
            || (status == PPC_MAILBOX_STATUS_ERR)
            || (status == PPC_MAILBOX_STATUS_RANGE))) {
      state->irq_status |= PPC_ACCEL_IRQ_CMD_DONE;
    }
  }
}

static bool ppc_accel_mailbox_command_in_flight(const ppc_accel_state_t *state)
{
  uint32_t mailbox_base;
  uint32_t seq;
  uint32_t ack_seq;

  mailbox_base = PPC_ACCEL_MAILBOX_OFFSET;
  seq = read_be32(state->window, mailbox_base + PPC_MAILBOX_OFF_SEQ);
  ack_seq = read_be32(state->window, mailbox_base + PPC_MAILBOX_OFF_ACK_SEQ);

  if (seq != ack_seq) {
    return true;
  }
  return false;
}

static bool ppc_accel_mailbox_is_command_lane_offset(uint32_t absolute_offset)
{
  uint32_t relative;

  if (absolute_offset < PPC_ACCEL_MAILBOX_OFFSET) {
    return false;
  }
  if (absolute_offset >= (PPC_ACCEL_MAILBOX_OFFSET + PPC_ACCEL_MAILBOX_SIZE)) {
    return false;
  }

  relative = absolute_offset - PPC_ACCEL_MAILBOX_OFFSET;
  if ((relative >= PPC_MAILBOX_OFF_SEQ) && (relative < (PPC_MAILBOX_OFF_RESULT1 + 4u))) {
    return true;
  }
  return false;
}

static bool ppc_accel_allow_host_mailbox_write(ppc_accel_state_t *state, uint32_t offset, uint32_t size)
{
  uint32_t i;

  for (i = 0u; i < size; i++) {
    if (ppc_accel_mailbox_is_command_lane_offset(offset + i) == true) {
      if (ppc_accel_mailbox_command_in_flight(state) == true) {
        return false;
      }
    }
  }
  return true;
}

static uint32_t ppc_accel_shared_info_features(const ppc_accel_state_t *state)
{
  uint32_t features;

  features = PPC_ACCEL_SHARED_INFO_FEAT_HOSTSVC | PPC_ACCEL_SHARED_INFO_FEAT_IRQ;
  if ((state->runtime_started == true) && (state->loader.qemu_uae_ppc_external_interrupt != NULL)) {
    features |= PPC_ACCEL_SHARED_INFO_FEAT_DOORBELL;
  }
  return features;
}

static void ppc_accel_write_shared_info(ppc_accel_state_t *state)
{
  uint32_t base;

  base = PPC_ACCEL_SHARED_INFO_OFFSET;
  write_be32(state->window, base + PPC_ACCEL_SHARED_INFO_OFF_SIGNATURE, PPC_ACCEL_SHARED_INFO_SIGNATURE);
  write_be32(state->window, base + PPC_ACCEL_SHARED_INFO_OFF_ABI_VERSION,
             PPC_ACCEL_SHARED_INFO_ABI_VERSION);
  write_be32(state->window, base + PPC_ACCEL_SHARED_INFO_OFF_MB_OFFSET, PPC_ACCEL_MAILBOX_OFFSET);
  write_be32(state->window, base + PPC_ACCEL_SHARED_INFO_OFF_MB_SIZE, PPC_ACCEL_MAILBOX_SIZE);
  write_be32(state->window, base + PPC_ACCEL_SHARED_INFO_OFF_DB_REG, PPC_ACCEL_REG_DOORBELL);
  write_be32(state->window, base + PPC_ACCEL_SHARED_INFO_OFF_FEATURES,
             ppc_accel_shared_info_features(state));
  write_be32(state->window, base + PPC_ACCEL_SHARED_INFO_OFF_RESERVED0, 0u);
  write_be32(state->window, base + PPC_ACCEL_SHARED_INFO_OFF_RESERVED1, 0u);
}

static bool ppc_accel_shared_info_write_protected(uint32_t offset, uint32_t size)
{
  uint32_t info_start;
  uint32_t info_end;
  uint32_t write_start;
  uint32_t write_end;

  info_start = PPC_ACCEL_SHARED_INFO_OFFSET;
  info_end = info_start + PPC_ACCEL_SHARED_INFO_SIZE;
  write_start = offset;
  write_end = offset + size;

  if (write_end <= info_start) {
    return false;
  }
  if (write_start >= info_end) {
    return false;
  }
  return true;
}

static void ppc_accel_stop_host_thread(ppc_accel_state_t *state)
{
  if (state->host_thread_started == false) {
    return;
  }

  state->host_service.stop = true;
  (void)pthread_join(state->host_thread, NULL);
  state->host_thread_started = false;
}

static void ppc_accel_cleanup_at_exit(void)
{
  ppc_accel_state_t *state;

  state = &g_ppc_accel_state;
  if (state->runtime_started == true) {
    state->loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_PAUSED);
    state->runtime_state = PPC_ACCEL_RUNTIME_PAUSED;
  }
  ppc_accel_stop_host_thread(state);
}

static bool ppc_accel_start_host_thread(ppc_accel_state_t *state)
{
  int thread_rc;

  if (state->host_thread_started == true) {
    return true;
  }

  memset(&state->host_service, 0, sizeof(state->host_service));
  state->host_service.mailbox = state->window + PPC_ACCEL_MAILBOX_OFFSET;
  state->host_service.ram = state->window;
  state->host_service.ram_size = PPC_ACCEL_Z2_SIZE;
  state->host_service.idle_sleep_ms = env_nonneg_int_or_default("PPC_HOST_SERVICE_IDLE_MS", 1);
  state->host_service.doorbell_enabled = env_bool_or_default("PPC_HOSTSVC_DOORBELL", false);
  state->host_service.verbose = state->verbose;
  state->host_service.external_interrupt = state->loader.qemu_uae_ppc_external_interrupt;
  state->host_service.stop = false;

  if ((state->host_service.doorbell_enabled == true)
      && (state->host_service.external_interrupt == NULL)) {
    LOG_WARN("[PPC-ACCEL] PPC_HOSTSVC_DOORBELL=1 requested but external interrupt is unavailable\n");
    state->host_service.doorbell_enabled = false;
  }

  thread_rc = pthread_create(&state->host_thread, NULL, ppc_accel_hostsvc_thread_main,
                             &state->host_service);
  if (thread_rc != 0) {
    LOG_WARN("[PPC-ACCEL] failed to start host service thread: %s\n", strerror(thread_rc));
    return false;
  }

  state->host_thread_started = true;
  return true;
}

static bool ppc_accel_backend_bootstrap(ppc_accel_state_t *state)
{
  const char *so_path;
  const char *model;
  uint32_t hid1;
  bool ppc_ok;
  PPCMemoryRegion regions[2];
  int start_timeout_ms;
  bool qemu_started;

  if (state->runtime_started == true) {
    return true;
  }
  if (state->loader_open == true) {
    LOG_WARN("[PPC-ACCEL] runtime bootstrap cannot retry after a partial failure; restart emulator\n");
    return false;
  }

  state->runtime_state = PPC_ACCEL_RUNTIME_STARTING;
  qemu_started = false;

  qemu_uae_loader_warn_if_bad_ld_library_path(stderr);

  qemu_uae_loader_init(&state->loader);
  so_path = getenv("QEMU_UAE_SO");
  if ((so_path == NULL) || (so_path[0] == '\0')) {
    so_path = "/usr/local/lib/qemu-uae.so";
  }

  if (qemu_uae_loader_open(&state->loader, so_path) == false) {
    LOG_WARN("[PPC-ACCEL] loader open failed: %s\n", qemu_uae_loader_error(&state->loader));
    goto fail;
  }
  state->loader_open = true;

  if (qemu_uae_loader_install_io_callbacks(&state->loader, io_read32, io_write32, io_read64,
                                           io_write64)
      == false) {
    LOG_WARN("[PPC-ACCEL] install IO callbacks failed: %s\n",
             qemu_uae_loader_error(&state->loader));
    goto fail;
  }

  g_ppc_qemu_log_enabled = state->qemu_log_enabled;
  if (qemu_uae_loader_install_log_callback(&state->loader, ppc_accel_qemu_log) == false) {
    LOG_WARN("[PPC-ACCEL] install log callback failed: %s\n",
             qemu_uae_loader_error(&state->loader));
    goto fail;
  }

  state->loader.qemu_uae_init();

  model = getenv("PPC_MODEL");
  if ((model == NULL) || (model[0] == '\0')) {
    model = "603e";
  }
  hid1 = env_u32_or_default("PPC_HID1", 0u);

  ppc_ok = false;
  if (state->loader.qemu_uae_ppc_init != NULL) {
    ppc_ok = state->loader.qemu_uae_ppc_init(model, hid1);
  }
  if ((ppc_ok == false) && (state->loader.ppc_cpu_init != NULL)) {
    ppc_ok = state->loader.ppc_cpu_init(model, hid1);
  }
  if (ppc_ok == false) {
    LOG_WARN("[PPC-ACCEL] PPC init failed (model=%s hid1=0x%08" PRIx32 ")\n", model, hid1);
    goto fail;
  }

  if (install_mailbox_firmware(
          state->window,
          PPC_ACCEL_Z2_SIZE,
          PPC_ACCEL_FIRMWARE_ENTRY_PRIMARY,
          PPC_ACCEL_MAILBOX_OFFSET)
      == false) {
    LOG_WARN("[PPC-ACCEL] failed to install primary mailbox firmware\n");
    goto fail;
  }
  if (install_mailbox_firmware(
          state->window,
          PPC_ACCEL_Z2_SIZE,
          PPC_ACCEL_FIRMWARE_ENTRY_SECONDARY,
          PPC_ACCEL_MAILBOX_OFFSET)
      == false) {
    LOG_WARN("[PPC-ACCEL] failed to install secondary mailbox firmware\n");
    goto fail;
  }

  memset(regions, 0, sizeof(regions));
  regions[0].start = 0u;
  regions[0].size = PPC_ACCEL_Z2_SIZE;
  regions[0].memory = state->window;
  regions[0].name = (char *)"ppc-accel-z2-window";
  regions[0].alias = 0u;

  regions[1].start = PPC_ACCEL_RESET_WINDOW_BASE;
  regions[1].size = PPC_ACCEL_Z2_SIZE;
  regions[1].memory = state->window;
  regions[1].name = (char *)"ppc-accel-reset-window";
  regions[1].alias = 0u;

  state->loader.ppc_cpu_map_memory(regions, 2);
  state->loader.ppc_cpu_reset();
  if (state->loader.ppc_cpu_set_pc != NULL) {
    state->loader.ppc_cpu_set_pc(0, PPC_ACCEL_FIRMWARE_ENTRY_PRIMARY);
  }

  state->loader.qemu_uae_start();
  qemu_started = true;
  start_timeout_ms = env_int_or_default("PPC_START_TIMEOUT_MS", 2000);
  if (wait_until_started_with_timeout(&state->loader, start_timeout_ms) == false) {
    goto fail;
  }

  if (ppc_accel_start_host_thread(state) == false) {
    goto fail;
  }

  state->runtime_started = true;
  state->runtime_state = PPC_ACCEL_RUNTIME_PAUSED;
  ppc_accel_write_shared_info(state);
  LOG_INFO("[PPC-ACCEL] runtime started using %s\n", state->loader.loaded_path);
  return true;

fail:
  state->runtime_state = PPC_ACCEL_RUNTIME_STOPPED;
  if (qemu_started == false) {
    if (state->loader_open == true) {
      qemu_uae_loader_close(&state->loader);
      state->loader_open = false;
    }
  }
  return false;
}

static void ppc_accel_backend_reset_cpu(ppc_accel_state_t *state)
{
  if (state->runtime_started == true) {
    state->loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_PAUSED);
    state->loader.ppc_cpu_reset();
    if (state->loader.ppc_cpu_set_pc != NULL) {
      state->loader.ppc_cpu_set_pc(0, PPC_ACCEL_FIRMWARE_ENTRY_PRIMARY);
    }
    state->runtime_state = PPC_ACCEL_RUNTIME_PAUSED;
  }
}

static bool ppc_accel_set_running(ppc_accel_state_t *state, bool running)
{
  if (running == true) {
    if (state->runtime_state == PPC_ACCEL_RUNTIME_RUNNING) {
      state->status |= PPC_ACCEL_STATUS_RUNNING;
      state->status &= ~PPC_ACCEL_STATUS_FAULT;
      return true;
    }
    if (ppc_accel_backend_bootstrap(state) == false) {
      state->status |= PPC_ACCEL_STATUS_FAULT;
      state->status &= ~PPC_ACCEL_STATUS_RUNNING;
      state->runtime_state = PPC_ACCEL_RUNTIME_STOPPED;
      return false;
    }

    state->loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_RUNNING);
    state->status |= PPC_ACCEL_STATUS_RUNNING;
    state->status &= ~PPC_ACCEL_STATUS_FAULT;
    state->runtime_state = PPC_ACCEL_RUNTIME_RUNNING;
    if (state->verbose == true) {
      LOG_INFO("[PPC-ACCEL] runtime state -> %s\n",
               ppc_accel_runtime_state_name(state->runtime_state));
    }
    return true;
  }

  if ((state->runtime_state == PPC_ACCEL_RUNTIME_STOPPED) && (state->runtime_started == false)) {
    state->status &= ~PPC_ACCEL_STATUS_RUNNING;
    return true;
  }
  if (state->runtime_state == PPC_ACCEL_RUNTIME_PAUSED) {
    state->status &= ~PPC_ACCEL_STATUS_RUNNING;
    return true;
  }
  if (state->runtime_started == true) {
    state->loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_PAUSED);
    state->runtime_state = PPC_ACCEL_RUNTIME_PAUSED;
  } else {
    state->runtime_state = PPC_ACCEL_RUNTIME_STOPPED;
  }
  state->status &= ~PPC_ACCEL_STATUS_RUNNING;
  if (state->verbose == true) {
    LOG_INFO("[PPC-ACCEL] runtime state -> %s\n",
             ppc_accel_runtime_state_name(state->runtime_state));
  }
  return true;
}

static void ppc_accel_device_reset_state(ppc_accel_state_t *state)
{
  state->control = 0u;
  state->status = 0u;
  state->irq_status = 0u;
  ppc_accel_mailbox_reset(state);
  ppc_accel_backend_reset_cpu(state);
  if (state->runtime_started == true) {
    state->runtime_state = PPC_ACCEL_RUNTIME_PAUSED;
  } else {
    state->runtime_state = PPC_ACCEL_RUNTIME_STOPPED;
  }
}

static uint32_t ppc_accel_reg_read32(ppc_accel_state_t *state, uint32_t offset)
{
  switch (offset) {
  case PPC_ACCEL_REG_MAGIC:
    return PPC_ACCEL_MAGIC;
  case PPC_ACCEL_REG_ABI_VERSION:
    return PPC_ACCEL_ABI_VERSION;
  case PPC_ACCEL_REG_CONTROL:
    return state->control;
  case PPC_ACCEL_REG_STATUS:
    return state->status;
  case PPC_ACCEL_REG_DOORBELL:
    return 0u;
  case PPC_ACCEL_REG_IRQ_STATUS:
    return state->irq_status;
  case PPC_ACCEL_REG_IRQ_ACK:
    return 0u;
  case PPC_ACCEL_REG_MAILBOX_OFFSET:
    return PPC_ACCEL_MAILBOX_OFFSET;
  case PPC_ACCEL_REG_MAILBOX_SIZE:
    return PPC_ACCEL_MAILBOX_SIZE;
  case PPC_ACCEL_REG_SHARED_OFFSET:
    return PPC_ACCEL_SHARED_OFFSET;
  case PPC_ACCEL_REG_SHARED_SIZE:
    return PPC_ACCEL_SHARED_SIZE;
  default:
    return read_be32(state->window, offset);
  }
}

static void ppc_accel_reg_write32(ppc_accel_state_t *state, uint32_t offset, uint32_t value)
{
  switch (offset) {
  case PPC_ACCEL_REG_CONTROL: {
    uint32_t new_control;

    new_control = value & (PPC_ACCEL_CTRL_START | PPC_ACCEL_CTRL_RESET | PPC_ACCEL_CTRL_IRQ_ENABLE);
    if ((new_control & PPC_ACCEL_CTRL_RESET) != 0u) {
      uint32_t keep_irq;

      keep_irq = new_control & PPC_ACCEL_CTRL_IRQ_ENABLE;
      ppc_accel_device_reset_state(state);
      state->control = keep_irq;
      state->status &= ~PPC_ACCEL_STATUS_FAULT;
    } else {
      state->control = new_control;
      if ((state->control & PPC_ACCEL_CTRL_START) != 0u) {
        (void)ppc_accel_set_running(state, true);
      } else {
        (void)ppc_accel_set_running(state, false);
      }
    }
    break;
  }
  case PPC_ACCEL_REG_DOORBELL:
    state->irq_status |= PPC_ACCEL_IRQ_HOST_DOORBELL;
    break;
  case PPC_ACCEL_REG_IRQ_ACK:
    state->irq_status &= ~value;
    break;
  case PPC_ACCEL_REG_STATUS:
    break;
  case PPC_ACCEL_REG_MAGIC:
  case PPC_ACCEL_REG_ABI_VERSION:
  case PPC_ACCEL_REG_MAILBOX_OFFSET:
  case PPC_ACCEL_REG_MAILBOX_SIZE:
  case PPC_ACCEL_REG_SHARED_OFFSET:
  case PPC_ACCEL_REG_SHARED_SIZE:
    break;
  default:
    write_be32(state->window, offset, value);
    break;
  }
}

static uint8_t ppc_accel_read8(zorro_device_t *dev, uint32_t offset)
{
  ppc_accel_state_t *state;
  uint32_t reg_base;
  uint32_t reg_value;
  uint32_t shift;

  state = (ppc_accel_state_t *)dev->priv;
  if (offset >= PPC_ACCEL_Z2_SIZE) {
    return 0xFFu;
  }

  ppc_accel_poll_mailbox(state);

  if (offset < PPC_ACCEL_REG_WINDOW_SIZE) {
    reg_base = offset & ~0x3u;
    reg_value = ppc_accel_reg_read32(state, reg_base);
    shift = (3u - (offset & 0x3u)) * 8u;
    return (uint8_t)((reg_value >> shift) & 0xFFu);
  }

  return state->window[offset];
}

static uint16_t ppc_accel_read16(zorro_device_t *dev, uint32_t offset)
{
  uint16_t hi;
  uint16_t lo;

  hi = (uint16_t)ppc_accel_read8(dev, offset + 0u);
  lo = (uint16_t)ppc_accel_read8(dev, offset + 1u);
  return (uint16_t)((hi << 8) | lo);
}

static uint32_t ppc_accel_read32(zorro_device_t *dev, uint32_t offset)
{
  ppc_accel_state_t *state;

  state = (ppc_accel_state_t *)dev->priv;
  if ((offset + 3u) >= PPC_ACCEL_Z2_SIZE) {
    return 0xFFFFFFFFu;
  }

  ppc_accel_poll_mailbox(state);

  if ((offset < PPC_ACCEL_REG_WINDOW_SIZE) && ((offset & 0x3u) == 0u)) {
    return ppc_accel_reg_read32(state, offset);
  }

  return ((uint32_t)ppc_accel_read8(dev, offset + 0u) << 24)
         | ((uint32_t)ppc_accel_read8(dev, offset + 1u) << 16)
         | ((uint32_t)ppc_accel_read8(dev, offset + 2u) << 8)
         | (uint32_t)ppc_accel_read8(dev, offset + 3u);
}

static void ppc_accel_write8(zorro_device_t *dev, uint32_t offset, uint8_t value)
{
  ppc_accel_state_t *state;
  uint32_t reg_base;
  uint32_t reg_value;
  uint32_t shift;
  uint32_t mask;

  state = (ppc_accel_state_t *)dev->priv;
  if (offset >= PPC_ACCEL_Z2_SIZE) {
    return;
  }

  if (offset < PPC_ACCEL_REG_WINDOW_SIZE) {
    reg_base = offset & ~0x3u;
    reg_value = ppc_accel_reg_read32(state, reg_base);
    shift = (3u - (offset & 0x3u)) * 8u;
    mask = 0xFFu << shift;
    reg_value = (reg_value & ~mask) | ((uint32_t)value << shift);
    ppc_accel_reg_write32(state, reg_base, reg_value);
  } else {
    if (ppc_accel_shared_info_write_protected(offset, 1u) == true) {
      if (state->verbose == true) {
        LOG_INFO("[PPC-ACCEL] ignored write8 to shared-info +0x%04" PRIx32 "\n", offset);
      }
      return;
    }
    if (ppc_accel_allow_host_mailbox_write(state, offset, 1u) == false) {
      if (state->verbose == true) {
        LOG_INFO("[PPC-ACCEL] ignored overlapping mailbox write8 at +0x%04" PRIx32 "\n", offset);
      }
      return;
    }
    state->window[offset] = value;
  }

  ppc_accel_poll_mailbox(state);
}

static void ppc_accel_write16(zorro_device_t *dev, uint32_t offset, uint16_t value)
{
  ppc_accel_state_t *state;
  uint32_t reg_base;
  uint32_t reg_value;
  uint32_t shift;
  uint32_t mask;

  state = (ppc_accel_state_t *)dev->priv;
  if ((offset + 1u) >= PPC_ACCEL_Z2_SIZE) {
    return;
  }

  if ((offset & 0x1u) != 0u) {
    ppc_accel_write8(dev, offset + 0u, (uint8_t)((value >> 8) & 0xFFu));
    ppc_accel_write8(dev, offset + 1u, (uint8_t)(value & 0xFFu));
    return;
  }

  if (offset < PPC_ACCEL_REG_WINDOW_SIZE) {
    reg_base = offset & ~0x3u;
    reg_value = ppc_accel_reg_read32(state, reg_base);
    shift = (2u - (offset & 0x2u)) * 8u;
    mask = 0xFFFFu << shift;
    reg_value = (reg_value & ~mask) | ((uint32_t)value << shift);
    ppc_accel_reg_write32(state, reg_base, reg_value);
  } else {
    if (ppc_accel_shared_info_write_protected(offset, 2u) == true) {
      if (state->verbose == true) {
        LOG_INFO("[PPC-ACCEL] ignored write16 to shared-info +0x%04" PRIx32 "\n", offset);
      }
      return;
    }
    if (ppc_accel_allow_host_mailbox_write(state, offset, 2u) == false) {
      if (state->verbose == true) {
        LOG_INFO("[PPC-ACCEL] ignored overlapping mailbox write16 at +0x%04" PRIx32 "\n", offset);
      }
      return;
    }
    state->window[offset + 0u] = (uint8_t)((value >> 8) & 0xFFu);
    state->window[offset + 1u] = (uint8_t)(value & 0xFFu);
  }

  ppc_accel_poll_mailbox(state);
}

static void ppc_accel_write32(zorro_device_t *dev, uint32_t offset, uint32_t value)
{
  ppc_accel_state_t *state;

  state = (ppc_accel_state_t *)dev->priv;
  if ((offset + 3u) >= PPC_ACCEL_Z2_SIZE) {
    return;
  }

  if ((offset < PPC_ACCEL_REG_WINDOW_SIZE) && ((offset & 0x3u) == 0u)) {
    ppc_accel_reg_write32(state, offset, value);
  } else {
    if (ppc_accel_shared_info_write_protected(offset, 4u) == true) {
      if (state->verbose == true) {
        LOG_INFO("[PPC-ACCEL] ignored write32 to shared-info +0x%04" PRIx32 "\n", offset);
      }
      return;
    }
    if (ppc_accel_allow_host_mailbox_write(state, offset, 4u) == false) {
      if (state->verbose == true) {
        LOG_INFO("[PPC-ACCEL] ignored overlapping mailbox write32 at +0x%04" PRIx32 "\n", offset);
      }
      return;
    }
    state->window[offset + 0u] = (uint8_t)((value >> 24) & 0xFFu);
    state->window[offset + 1u] = (uint8_t)((value >> 16) & 0xFFu);
    state->window[offset + 2u] = (uint8_t)((value >> 8) & 0xFFu);
    state->window[offset + 3u] = (uint8_t)(value & 0xFFu);
  }

  ppc_accel_poll_mailbox(state);
}

static void ppc_accel_reset(zorro_device_t *dev)
{
  ppc_accel_state_t *state;

  state = (ppc_accel_state_t *)dev->priv;
  ppc_accel_device_reset_state(state);
}

static zorro_device_t z2_ppc_accel_device = {
    .name = "z2-ppc-accel",
    .bus = ZORRO_BUS_Z2,
    .size = PPC_ACCEL_Z2_SIZE,
    .manufacturer = PPC_ACCEL_MANUFACTURER_ID,
    .product = PPC_ACCEL_PRODUCT_ID,
    .flags = 0,
    .ac_rom = ppc_accel_rom,
    .ac_rom_size = sizeof(ppc_accel_rom),
    .reset = ppc_accel_reset,
    .read8 = ppc_accel_read8,
    .read16 = ppc_accel_read16,
    .read32 = ppc_accel_read32,
    .write8 = ppc_accel_write8,
    .write16 = ppc_accel_write16,
    .write32 = ppc_accel_write32,
    .priv = &g_ppc_accel_state,
};

void z2_ppc_accel_register(void)
{
  int slot;

  if (g_ppc_accel_registered == true) {
    LOG_WARN("[ZORRO] Z2 PPC accelerator already registered; ignoring duplicate setvar\n");
    return;
  }

  if (g_ppc_accel_atexit_installed == false) {
    (void)atexit(ppc_accel_cleanup_at_exit);
    g_ppc_accel_atexit_installed = true;
  }

  memset(&g_ppc_accel_state, 0, sizeof(g_ppc_accel_state));
  g_ppc_accel_state.verbose = env_bool_or_default("PPC_VERBOSE", false);
  g_ppc_accel_state.qemu_log_enabled = env_bool_or_default("PPC_ACCEL_QEMU_LOG", false);
  g_ppc_accel_state.runtime_state = PPC_ACCEL_RUNTIME_STOPPED;

  LOG_INFO("[ZORRO] Registering Z2 PPC accelerator device.\n");
  ppc_accel_device_reset_state(&g_ppc_accel_state);

  slot = zorro_register_device(&z2_ppc_accel_device);
  if (slot < 0) {
    LOG_INFO("[ZORRO] Failed to register Z2 PPC accelerator device.\n");
    return;
  }
  g_ppc_accel_registered = true;
}
