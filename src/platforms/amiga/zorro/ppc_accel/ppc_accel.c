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
static const uint32_t PPC_ACCEL_FIRMWARE_ENTRY_BOOT_TEST = 0x00000200u;
static const uint32_t PPC_ACCEL_PPC_RAM_BASE_DEFAULT = 0x08000000u;
static const uint32_t PPC_ACCEL_PPC_RAM_MB_DEFAULT = 128u;
static const uint32_t PPC_ACCEL_PPC_RAM_MB_MIN = 1u;

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
  uint8_t *window;
  uint32_t window_size;
  uint8_t *ppc_ram;
  uint32_t ppc_ram_base;
  uint32_t ppc_ram_size;
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
  uint8_t *ppc_ram;
  uint32_t ppc_ram_base;
  uint32_t ppc_ram_size;
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
  bool trace_io_enabled;
  bool trace_io_seen;
  bool trace_io_limit_noted;
  uint32_t trace_io_limit;
  uint32_t trace_io_emitted;
  bool trace_mmio_enabled;
  bool trace_mmio_limit_noted;
  uint32_t trace_mmio_limit;
  uint32_t trace_mmio_emitted;
  bool diag_trace_enabled;
  bool diag_trace_limit_noted;
  uint32_t diag_trace_limit;
  uint32_t diag_trace_emitted;
  uint8_t diag_config;
  uint16_t diag_diagpoint;
  uint16_t diag_bootpoint;
  uint16_t diag_size;
  uint16_t diag_boot_stub_offset;
  uint16_t diag_boot_stub_size;
  uint16_t diag_diag_stub_offset;
  uint16_t diag_diag_stub_size;
  uint32_t boot_marker_last;
  bool have_boot_marker_last;
  uint64_t io_read32_count;
  uint64_t io_write32_count;
  uint64_t io_read64_count;
  uint64_t io_write64_count;
  ppc_accel_runtime_state_t runtime_state;
} ppc_accel_state_t;

static ppc_accel_state_t g_ppc_accel_state;
static bool g_ppc_qemu_log_enabled;
static bool g_ppc_accel_registered;
static bool g_ppc_accel_atexit_installed;

#define PPC_ACCEL_AC_SERIAL_DEFAULT 0x00420001u
#define PPC_ACCEL_AC_SERIAL_ROM_NIBBLE_OFFSET 12u
#define PPC_ACCEL_AC_DIAG_VEC_DEFAULT 0x4000u
#define PPC_ACCEL_AC_DIAG_VEC_ROM_NIBBLE_OFFSET 20u
#define PPC_ACCEL_DIAG_CONFIG_DEFAULT 0x00u
#define PPC_ACCEL_DIAG_DIAGPOINT_DEFAULT 0x0000u

#define PPC_ACCEL_DIAG_OFFSET 0x00004000u
#define PPC_ACCEL_DIAG_AREA_SIZE 0x0040u
#define PPC_ACCEL_DIAG_NAME_OFFSET 0x0010u
#define PPC_ACCEL_DIAG_BOOT_STUB_OFFSET 0x0020u
#define PPC_ACCEL_DIAG_BOOT_STUB_SIZE 0x0012u
#define PPC_ACCEL_DIAG_DIAG_STUB_OFFSET 0x0032u
#define PPC_ACCEL_DIAG_DIAG_STUB_SIZE 0x000au
#define PPC_ACCEL_DIAG_BOOTPOINT_DEFAULT 0x0000u

static void ppc_accel_write_shared_info(ppc_accel_state_t *state);
static uint32_t env_u32_or_default(const char *name, uint32_t default_value);

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
    Z2_Z2 | Z2_BOOTROM,
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
    /* er_SerialNumber nibbles (filled at register time, default 0x00420001). */
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    /* er_InitDiagVec nibbles (filled at register time, default 0x4000). */
    0x0,
    0x0,
    0x0,
    0x0,
};

static void ppc_accel_set_autoconfig_serial(uint32_t serial)
{
  ppc_accel_rom[PPC_ACCEL_AC_SERIAL_ROM_NIBBLE_OFFSET + 0u] = (uint8_t)((serial >> 28u) & 0x0Fu);
  ppc_accel_rom[PPC_ACCEL_AC_SERIAL_ROM_NIBBLE_OFFSET + 1u] = (uint8_t)((serial >> 24u) & 0x0Fu);
  ppc_accel_rom[PPC_ACCEL_AC_SERIAL_ROM_NIBBLE_OFFSET + 2u] = (uint8_t)((serial >> 20u) & 0x0Fu);
  ppc_accel_rom[PPC_ACCEL_AC_SERIAL_ROM_NIBBLE_OFFSET + 3u] = (uint8_t)((serial >> 16u) & 0x0Fu);
  ppc_accel_rom[PPC_ACCEL_AC_SERIAL_ROM_NIBBLE_OFFSET + 4u] = (uint8_t)((serial >> 12u) & 0x0Fu);
  ppc_accel_rom[PPC_ACCEL_AC_SERIAL_ROM_NIBBLE_OFFSET + 5u] = (uint8_t)((serial >> 8u) & 0x0Fu);
  ppc_accel_rom[PPC_ACCEL_AC_SERIAL_ROM_NIBBLE_OFFSET + 6u] = (uint8_t)((serial >> 4u) & 0x0Fu);
  ppc_accel_rom[PPC_ACCEL_AC_SERIAL_ROM_NIBBLE_OFFSET + 7u] = (uint8_t)(serial & 0x0Fu);
}

static void ppc_accel_set_autoconfig_diag_vec(uint16_t diag_vec)
{
  ppc_accel_rom[PPC_ACCEL_AC_DIAG_VEC_ROM_NIBBLE_OFFSET + 0u] =
      (uint8_t)((diag_vec >> 12u) & 0x0Fu);
  ppc_accel_rom[PPC_ACCEL_AC_DIAG_VEC_ROM_NIBBLE_OFFSET + 1u] =
      (uint8_t)((diag_vec >> 8u) & 0x0Fu);
  ppc_accel_rom[PPC_ACCEL_AC_DIAG_VEC_ROM_NIBBLE_OFFSET + 2u] =
      (uint8_t)((diag_vec >> 4u) & 0x0Fu);
  ppc_accel_rom[PPC_ACCEL_AC_DIAG_VEC_ROM_NIBBLE_OFFSET + 3u] =
      (uint8_t)(diag_vec & 0x0Fu);
}

static void write_be16(uint8_t *base, uint32_t offset, uint16_t value)
{
  base[offset + 0u] = (uint8_t)((value >> 8u) & 0xFFu);
  base[offset + 1u] = (uint8_t)(value & 0xFFu);
}

static void ppc_accel_write_diag_stubs(ppc_accel_state_t *state)
{
  static const uint8_t boot_stub[PPC_ACCEL_DIAG_BOOT_STUB_SIZE] = {
      0x20u, 0x6fu, 0x00u, 0x04u, /* move.l 4(%a7), %a0        ; ConfigDev* */
      0x20u, 0x68u, 0x00u, 0x20u, /* move.l 32(%a0), %a0       ; cd_BoardAddr */
      0x70u, 0x01u,               /* moveq #1, %d0             */
      0x21u, 0x40u, 0x00u, 0x08u, /* move.l %d0, 8(%a0)        ; CONTROL=START */
      0x70u, 0x01u,               /* moveq #1, %d0             ; success/fallback */
      0x4eu, 0x75u                /* rts                       */
  };
  static const uint8_t diag_stub[PPC_ACCEL_DIAG_DIAG_STUB_SIZE] = {
      0x70u, 0x01u,               /* moveq #1, %d0             */
      0x21u, 0x40u, 0x00u, 0x08u, /* move.l %d0, 8(%a0)        ; A0 = BoardBase in DiagPoint */
      0x70u, 0x01u,               /* moveq #1, %d0             */
      0x4eu, 0x75u                /* rts                       */
  };
  uint32_t boot_offset;
  uint32_t diag_offset;

  boot_offset = PPC_ACCEL_DIAG_OFFSET + (uint32_t)PPC_ACCEL_DIAG_BOOT_STUB_OFFSET;
  diag_offset = PPC_ACCEL_DIAG_OFFSET + (uint32_t)PPC_ACCEL_DIAG_DIAG_STUB_OFFSET;
  if ((boot_offset + PPC_ACCEL_DIAG_BOOT_STUB_SIZE) > PPC_ACCEL_Z2_SIZE) {
    return;
  }
  if ((diag_offset + PPC_ACCEL_DIAG_DIAG_STUB_SIZE) > PPC_ACCEL_Z2_SIZE) {
    return;
  }

  memcpy(state->window + boot_offset, boot_stub, (size_t)PPC_ACCEL_DIAG_BOOT_STUB_SIZE);
  memcpy(state->window + diag_offset, diag_stub, (size_t)PPC_ACCEL_DIAG_DIAG_STUB_SIZE);
}

static void ppc_accel_trace_diag_read(ppc_accel_state_t *state, uint32_t offset, uint8_t value)
{
  uint32_t diag_start;
  uint32_t diag_end;
  uint32_t boot_stub_start;
  uint32_t boot_stub_end;
  uint32_t diag_stub_start;
  uint32_t diag_stub_end;
  bool watch_header;
  bool watch_boot_stub;
  bool watch_diag_stub;

  if ((state == NULL) || (state->diag_trace_enabled == false)) {
    return;
  }

  diag_start = PPC_ACCEL_DIAG_OFFSET;
  diag_end = diag_start + (uint32_t)state->diag_size;
  if ((offset < diag_start) || (offset >= diag_end)) {
    return;
  }

  boot_stub_start = diag_start + (uint32_t)state->diag_boot_stub_offset;
  boot_stub_end = boot_stub_start + (uint32_t)state->diag_boot_stub_size;
  diag_stub_start = diag_start + (uint32_t)state->diag_diag_stub_offset;
  diag_stub_end = diag_stub_start + (uint32_t)state->diag_diag_stub_size;
  watch_header = (offset < (diag_start + 0x10u));
  watch_boot_stub = (offset >= boot_stub_start) && (offset < boot_stub_end);
  watch_diag_stub = (offset >= diag_stub_start) && (offset < diag_stub_end);
  if ((watch_header == false) && (watch_boot_stub == false) && (watch_diag_stub == false)) {
    return;
  }

  if ((state->diag_trace_limit != 0u) && (state->diag_trace_emitted >= state->diag_trace_limit)) {
    if (state->diag_trace_limit_noted == false) {
      state->diag_trace_limit_noted = true;
      LOG_INFO("[PPC-ACCEL] DIAG trace limit reached (%" PRIu32 " events)\n",
               state->diag_trace_limit);
    }
    return;
  }

  state->diag_trace_emitted++;
  LOG_INFO("[PPC-ACCEL][DIAG] R8 off=0x%04" PRIx32 " val=0x%02" PRIx8 " (%s)\n",
           offset,
           value,
           watch_boot_stub ? "boot-stub" : (watch_diag_stub ? "diag-stub" : "hdr"));
}

static void ppc_accel_write_diag_area(ppc_accel_state_t *state)
{
  static const char diag_name[] = "PiStorm PPC Accelerator";
  uint32_t base;
  uint32_t name_len;
  uint8_t diag_config;
  uint16_t diag_diagpoint;
  uint16_t diag_bootpoint;

  base = PPC_ACCEL_DIAG_OFFSET;
  name_len = (uint32_t)sizeof(diag_name);
  if ((base + PPC_ACCEL_DIAG_NAME_OFFSET + name_len) > PPC_ACCEL_Z2_SIZE) {
    return;
  }

  diag_config = (uint8_t)(env_u32_or_default("PPC_ACCEL_DIAG_CONFIG",
                                             PPC_ACCEL_DIAG_CONFIG_DEFAULT) & 0xFFu);
  diag_diagpoint = (uint16_t)(env_u32_or_default("PPC_ACCEL_DIAG_DIAGPOINT",
                                                 PPC_ACCEL_DIAG_DIAGPOINT_DEFAULT) & 0xFFFFu);
  diag_bootpoint = (uint16_t)(env_u32_or_default("PPC_ACCEL_DIAG_BOOTPOINT",
                                                 PPC_ACCEL_DIAG_BOOTPOINT_DEFAULT) & 0xFFFFu);
  if (diag_diagpoint >= PPC_ACCEL_DIAG_AREA_SIZE) {
    diag_diagpoint = 0u;
  }
  if (diag_bootpoint >= PPC_ACCEL_DIAG_AREA_SIZE) {
    diag_bootpoint = 0u;
  }

  /* Read-only DiagArea with optional 68k bootpoint stub for CSPPC-style bring-up probes. */
  state->window[base + 0u] = diag_config;
  state->window[base + 1u] = 0x00u;
  write_be16(state->window, base + 2u, (uint16_t)PPC_ACCEL_DIAG_AREA_SIZE);
  write_be16(state->window, base + 4u, diag_diagpoint);
  write_be16(state->window, base + 6u, diag_bootpoint);
  write_be16(state->window, base + 8u, (uint16_t)PPC_ACCEL_DIAG_NAME_OFFSET);
  write_be16(state->window, base + 10u, 0u);
  write_be16(state->window, base + 12u, 0u);
  memcpy(state->window + base + PPC_ACCEL_DIAG_NAME_OFFSET, diag_name, name_len);
  ppc_accel_write_diag_stubs(state);

  state->diag_config = diag_config;
  state->diag_diagpoint = diag_diagpoint;
  state->diag_bootpoint = diag_bootpoint;
  state->diag_size = PPC_ACCEL_DIAG_AREA_SIZE;
  state->diag_boot_stub_offset = PPC_ACCEL_DIAG_BOOT_STUB_OFFSET;
  state->diag_boot_stub_size = PPC_ACCEL_DIAG_BOOT_STUB_SIZE;
  state->diag_diag_stub_offset = PPC_ACCEL_DIAG_DIAG_STUB_OFFSET;
  state->diag_diag_stub_size = PPC_ACCEL_DIAG_DIAG_STUB_SIZE;

  if (state->verbose == true) {
    LOG_INFO("[PPC-ACCEL] diag area config=0x%02" PRIx8 " diag=0x%04" PRIx16
             " boot=0x%04" PRIx16 " size=0x%04" PRIx16 " boot_stub=0x%04" PRIx16
             " diag_stub=0x%04" PRIx16 "\n",
             diag_config,
             diag_diagpoint,
             diag_bootpoint,
             (uint16_t)PPC_ACCEL_DIAG_AREA_SIZE,
             (uint16_t)PPC_ACCEL_DIAG_BOOT_STUB_OFFSET,
             (uint16_t)PPC_ACCEL_DIAG_DIAG_STUB_OFFSET);
  }
}

static void ppc_accel_qemu_log(const char *format, ...)
{
  char message[1024];
  size_t message_len;
  va_list args;

  if (g_ppc_qemu_log_enabled == false) {
    return;
  }

  va_start(args, format);
  (void)vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  message_len = strlen(message);
  while ((message_len > 0u)
         && ((message[message_len - 1u] == '\n') || (message[message_len - 1u] == '\r'))) {
    message[message_len - 1u] = '\0';
    message_len--;
  }
  if (message[0] == '\0') {
    return;
  }

  LOG_INFO("[PPC-QEMU] %s\n", message);
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

static uint32_t ppc_accel_env_ppc_ram_size_bytes_or_default(void)
{
  uint32_t ram_mb;
  const uint32_t mb_bytes = (1024u * 1024u);

  ram_mb = env_u32_or_default("PPC_ACCEL_PPC_RAM_MB", PPC_ACCEL_PPC_RAM_MB_DEFAULT);
  if (ram_mb < PPC_ACCEL_PPC_RAM_MB_MIN) {
    ram_mb = PPC_ACCEL_PPC_RAM_MB_DEFAULT;
  }
  if (ram_mb > (0xffffffffu / mb_bytes)) {
    ram_mb = PPC_ACCEL_PPC_RAM_MB_DEFAULT;
  }

  return ram_mb * mb_bytes;
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

static uint32_t ppc_accel_boot_desc_default_stack(const ppc_accel_state_t *state)
{
  uint64_t stack_top;

  if ((state == NULL) || (state->ppc_ram_size == 0u)) {
    return 0u;
  }

  stack_top = (uint64_t)state->ppc_ram_base + (uint64_t)state->ppc_ram_size;
  if (stack_top > 0x100000000ULL) {
    return 0u;
  }
  if (stack_top < 0x1000ULL) {
    return 0u;
  }

  return (uint32_t)(stack_top - 0x1000ULL);
}

static void ppc_accel_write_boot_descriptor(ppc_accel_state_t *state)
{
  uint32_t base;
  uint32_t default_entry;
  uint32_t default_stack;
  uint32_t default_arg0;
  uint32_t magic;
  uint32_t entry;
  uint32_t stack;
  uint32_t arg0;

  if (state == NULL) {
    return;
  }

  base = PPC_ACCEL_BOOT_DESC_OFFSET;
  if ((base + PPC_ACCEL_BOOT_DESC_SIZE) > PPC_ACCEL_Z2_SIZE) {
    return;
  }

  default_entry = PPC_ACCEL_FIRMWARE_ENTRY_PRIMARY;
  default_stack = ppc_accel_boot_desc_default_stack(state);
  default_arg0 = PPC_ACCEL_MAILBOX_OFFSET;
  magic = env_u32_or_default("PPC_ACCEL_BOOT_MAGIC", PPC_ACCEL_BOOT_DESC_MAGIC);
  entry = env_u32_or_default("PPC_ACCEL_BOOT_ENTRY", default_entry);
  stack = env_u32_or_default("PPC_ACCEL_BOOT_STACK", default_stack);
  arg0 = env_u32_or_default("PPC_ACCEL_BOOT_ARG0", default_arg0);

  write_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_MAGIC, magic);
  write_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_ENTRY, entry);
  write_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_STACK, stack);
  write_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_ARG0, arg0);
  write_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_MARKER, 0u);
  state->boot_marker_last = 0u;
  state->have_boot_marker_last = false;

  if (state->verbose == true) {
    LOG_INFO("[PPC-ACCEL] bootdesc off=0x%04" PRIx32 " magic=0x%08" PRIx32
             " entry=0x%08" PRIx32 " stack=0x%08" PRIx32 " arg0=0x%08" PRIx32 "\n",
             base,
             magic,
             entry,
             stack,
             arg0);
  }
}

static uint32_t ppc_accel_boot_desc_read_field(const ppc_accel_state_t *state, uint32_t field_offset)
{
  uint32_t base;

  if (state == NULL) {
    return 0u;
  }

  base = PPC_ACCEL_BOOT_DESC_OFFSET;
  if ((base + PPC_ACCEL_BOOT_DESC_SIZE) > PPC_ACCEL_Z2_SIZE) {
    return 0u;
  }
  if ((field_offset + 4u) > PPC_ACCEL_BOOT_DESC_SIZE) {
    return 0u;
  }

  return read_be32(state->window, base + field_offset);
}

static void ppc_accel_boot_desc_write_field(ppc_accel_state_t *state, uint32_t field_offset, uint32_t value)
{
  uint32_t base;

  if (state == NULL) {
    return;
  }

  base = PPC_ACCEL_BOOT_DESC_OFFSET;
  if ((base + PPC_ACCEL_BOOT_DESC_SIZE) > PPC_ACCEL_Z2_SIZE) {
    return;
  }
  if ((field_offset + 4u) > PPC_ACCEL_BOOT_DESC_SIZE) {
    return;
  }

  write_be32(state->window, base + field_offset, value);
  if (field_offset != PPC_ACCEL_BOOT_DESC_OFF_MARKER) {
    write_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_MARKER, 0u);
    state->boot_marker_last = 0u;
    state->have_boot_marker_last = false;
  }
}

static void ppc_accel_reset_io_trace(ppc_accel_state_t *state)
{
  if (state == NULL) {
    return;
  }

  state->trace_io_seen = false;
  state->trace_io_limit_noted = false;
  state->trace_io_emitted = 0u;
  state->trace_mmio_limit_noted = false;
  state->trace_mmio_emitted = 0u;
  state->diag_trace_limit_noted = false;
  state->diag_trace_emitted = 0u;
  state->io_read32_count = 0u;
  state->io_write32_count = 0u;
  state->io_read64_count = 0u;
  state->io_write64_count = 0u;
}

static void ppc_accel_trace_mmio_event(
    ppc_accel_state_t *state,
    bool is_write,
    int width_bits,
    uint32_t offset,
    uint32_t value)
{
  const char *op;

  if ((state == NULL) || (state->trace_mmio_enabled == false)) {
    return;
  }
  if ((state->trace_mmio_limit != 0u) && (state->trace_mmio_emitted >= state->trace_mmio_limit)) {
    if (state->trace_mmio_limit_noted == false) {
      state->trace_mmio_limit_noted = true;
      LOG_INFO("[PPC-ACCEL] MMIO trace limit reached (%" PRIu32 " events)\n", state->trace_mmio_limit);
    }
    return;
  }

  state->trace_mmio_emitted++;
  op = is_write ? "W" : "R";
  LOG_INFO("[PPC-ACCEL][MMIO] %s%d off=0x%04" PRIx32 " val=0x%08" PRIx32 "\n",
           op,
           width_bits,
           offset,
           value);
}

static void ppc_accel_trace_io_event(
    ppc_accel_state_t *state,
    const char *kind,
    uint32_t addr,
    uint64_t value,
    int width_bytes)
{
  if ((state == NULL) || (state->trace_io_enabled == false)) {
    return;
  }
  if (width_bytes <= 0) {
    width_bytes = 4;
  }
  if (state->trace_io_seen == false) {
    state->trace_io_seen = true;
    LOG_INFO("[PPC-ACCEL] qemu-uae PPC I/O callback path is active\n");
  }
  if ((state->trace_io_limit != 0u) && (state->trace_io_emitted >= state->trace_io_limit)) {
    if (state->trace_io_limit_noted == false) {
      state->trace_io_limit_noted = true;
      LOG_INFO("[PPC-ACCEL] IO trace limit reached (%" PRIu32 " events)\n", state->trace_io_limit);
    }
    return;
  }

  state->trace_io_emitted++;
  LOG_INFO("[PPC-ACCEL][IO] %s addr=0x%08" PRIx32 " width=%d value=0x%0*" PRIx64 "\n",
           kind,
           addr,
           width_bytes,
           width_bytes * 2,
           value);
}

static void ppc_accel_log_io_summary(const ppc_accel_state_t *state)
{
  if (state == NULL) {
    return;
  }
  if ((state->trace_io_enabled == false) && (state->qemu_log_enabled == false)) {
    return;
  }

  LOG_INFO("[PPC-ACCEL] io summary r32=%" PRIu64 " w32=%" PRIu64 " r64=%" PRIu64 " w64=%" PRIu64
           " traced=%" PRIu32 "%s\n",
           state->io_read32_count,
           state->io_write32_count,
           state->io_read64_count,
           state->io_write64_count,
           state->trace_io_emitted,
           (state->trace_io_limit != 0u) ? " (limited)" : "");
}

static bool io_read32(uint32_t addr, uint32_t *data, int size)
{
  ppc_accel_state_t *state;
  uint32_t value;
  int width_bytes;

  state = &g_ppc_accel_state;
  value = 0xDEADBEEFu;
  width_bytes = size;
  if (width_bytes <= 0) {
    width_bytes = 4;
  }

  if (data != NULL) {
    *data = value;
  }
  state->io_read32_count++;
  ppc_accel_trace_io_event(state, "read32", addr, (uint64_t)value, width_bytes);
  return true;
}

static bool io_write32(uint32_t addr, uint32_t data, int size)
{
  ppc_accel_state_t *state;
  int width_bytes;

  state = &g_ppc_accel_state;
  width_bytes = size;
  if (width_bytes <= 0) {
    width_bytes = 4;
  }

  state->io_write32_count++;
  ppc_accel_trace_io_event(state, "write32", addr, (uint64_t)data, width_bytes);
  return true;
}

static bool io_read64(uint32_t addr, uint64_t *data)
{
  ppc_accel_state_t *state;
  uint64_t value;

  state = &g_ppc_accel_state;
  value = 0xDEADBEEFDEADBEEFULL;
  if (data != NULL) {
    *data = value;
  }

  state->io_read64_count++;
  ppc_accel_trace_io_event(state, "read64", addr, value, 8);
  return true;
}

static bool io_write64(uint32_t addr, uint64_t data)
{
  ppc_accel_state_t *state;

  state = &g_ppc_accel_state;
  state->io_write64_count++;
  ppc_accel_trace_io_event(state, "write64", addr, data, 8);
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

static bool ppc_accel_resolve_ppc_address(
    const ppc_accel_host_service_context_t *ctx,
    uint32_t addr,
    uint32_t len,
    uint8_t **out_ptr)
{
  uint64_t end_addr;
  uint64_t window_size64;
  uint64_t ram_start;
  uint64_t ram_end;

  if ((ctx == NULL) || (out_ptr == NULL)) {
    return false;
  }

  end_addr = (uint64_t)addr + (uint64_t)len;
  window_size64 = (uint64_t)ctx->window_size;
  if ((ctx->window != NULL) && (addr < ctx->window_size) && (end_addr <= window_size64)) {
    *out_ptr = ctx->window + addr;
    return true;
  }

  if ((ctx->ppc_ram == NULL) || (ctx->ppc_ram_size == 0u)) {
    return false;
  }

  ram_start = (uint64_t)ctx->ppc_ram_base;
  ram_end = ram_start + (uint64_t)ctx->ppc_ram_size;
  if (((uint64_t)addr < ram_start) || (end_addr > ram_end)) {
    return false;
  }

  *out_ptr = ctx->ppc_ram + (uint32_t)((uint64_t)addr - ram_start);
  return true;
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
    uint8_t *data;

    data = NULL;
    if (ppc_accel_resolve_ppc_address(ctx, arg0, arg1, &data) == false) {
      status = PPC_MAILBOX_STATUS_RANGE;
    } else {
      result0 = crc32_ieee(data, arg1);
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

static bool ppc_accel_ranges_overlap(uint32_t start_a, uint32_t size_a, uint32_t start_b, uint32_t size_b)
{
  uint64_t a_start;
  uint64_t a_end;
  uint64_t b_start;
  uint64_t b_end;

  if ((size_a == 0u) || (size_b == 0u)) {
    return false;
  }

  a_start = (uint64_t)start_a;
  a_end = a_start + (uint64_t)size_a;
  b_start = (uint64_t)start_b;
  b_end = b_start + (uint64_t)size_b;

  if ((a_end <= b_start) || (b_end <= a_start)) {
    return false;
  }
  return true;
}

static bool ppc_accel_prepare_ppc_ram(ppc_accel_state_t *state)
{
  uint32_t ram_base;
  uint32_t ram_size;
  uint64_t ram_end;

  if (state->ppc_ram != NULL) {
    return true;
  }

  ram_base = env_u32_or_default("PPC_ACCEL_PPC_RAM_BASE", PPC_ACCEL_PPC_RAM_BASE_DEFAULT);
  ram_size = ppc_accel_env_ppc_ram_size_bytes_or_default();
  ram_end = (uint64_t)ram_base + (uint64_t)ram_size;
  if (ram_end > 0x100000000ULL) {
    LOG_WARN("[PPC-ACCEL] PPC RAM mapping overflows 32-bit space: base=0x%08" PRIx32
             " size=0x%08" PRIx32 "\n",
             ram_base, ram_size);
    return false;
  }

  state->ppc_ram = (uint8_t *)calloc(1, (size_t)ram_size);
  if (state->ppc_ram == NULL) {
    LOG_WARN("[PPC-ACCEL] failed to allocate PPC RAM (%" PRIu32 " MiB)\n",
             ram_size / (1024u * 1024u));
    return false;
  }

  state->ppc_ram_base = ram_base;
  state->ppc_ram_size = ram_size;
  LOG_INFO("[PPC-ACCEL] PPC RAM mapped at 0x%08" PRIx32 " size=%" PRIu32 " MiB\n",
           state->ppc_ram_base, state->ppc_ram_size / (1024u * 1024u));
  return true;
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

static bool install_reset_trampoline(uint8_t *ram, uint32_t ram_size, uint32_t entry_offset,
                                     uint32_t boot_desc_offset)
{
  static const uint32_t program_words[] = {
      0x3c600000u, 0x60630000u, 0x80830000u, 0x3ca00000u, 0x60a50000u, 0x7c042800u,
      0x4082fff0u, 0x38c00001u, 0x90c30010u, 0x80830004u, 0x80230008u, 0x38e00002u,
      0x90e30010u, 0x8063000cu, 0x7c8903a6u, 0x4e800420u};
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
      value = 0x3c600000u | ((boot_desc_offset >> 16u) & 0xffffu);
    } else if (i == 1u) {
      value = 0x60630000u | (boot_desc_offset & 0xffffu);
    } else if (i == 3u) {
      value = 0x3ca00000u | ((PPC_ACCEL_BOOT_DESC_MAGIC >> 16u) & 0xffffu);
    } else if (i == 4u) {
      value = 0x60a50000u | (PPC_ACCEL_BOOT_DESC_MAGIC & 0xffffu);
    }
    write_be32(ram, entry_offset + (i * 4u), value);
  }

  return true;
}

static bool install_boot_test_firmware(uint8_t *ram, uint32_t ram_size, uint32_t entry_offset,
                                       uint32_t marker_addr)
{
  static const uint32_t program_words[] = {
      0x3c600000u, /* lis   r3, marker@h */
      0x60630000u, /* ori   r3, r3, marker@l */
      0x38800003u, /* li    r4, 3 */
      0x90830000u, /* stw   r4, 0(r3) */
      0x4bfffffcu  /* b     . */
  };
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
      value = 0x3c600000u | ((marker_addr >> 16u) & 0xffffu);
    } else if (i == 1u) {
      value = 0x60630000u | (marker_addr & 0xffffu);
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
  ppc_accel_write_boot_descriptor(state);
  ppc_accel_write_shared_info(state);
  ppc_accel_write_diag_area(state);

  state->have_last_ack_seq = false;
  state->last_ack_seq = 0u;
}

static void ppc_accel_poll_boot_marker(ppc_accel_state_t *state)
{
  uint32_t base;
  uint32_t marker;

  if (state == NULL) {
    return;
  }

  base = PPC_ACCEL_BOOT_DESC_OFFSET;
  if ((base + PPC_ACCEL_BOOT_DESC_SIZE) > PPC_ACCEL_Z2_SIZE) {
    return;
  }

  marker = read_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_MARKER);
  if (state->have_boot_marker_last == false) {
    state->have_boot_marker_last = true;
    state->boot_marker_last = marker;
    if (marker != 0u) {
      LOG_INFO("[PPC-ACCEL] boot marker=0x%08" PRIx32 " entry=0x%08" PRIx32
               " stack=0x%08" PRIx32 " arg0=0x%08" PRIx32 "\n",
               marker,
               read_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_ENTRY),
               read_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_STACK),
               read_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_ARG0));
    }
    return;
  }
  if (marker == state->boot_marker_last) {
    return;
  }

  state->boot_marker_last = marker;
  LOG_INFO("[PPC-ACCEL] boot marker=0x%08" PRIx32 " entry=0x%08" PRIx32
           " stack=0x%08" PRIx32 " arg0=0x%08" PRIx32 "\n",
           marker,
           read_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_ENTRY),
           read_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_STACK),
           read_be32(state->window, base + PPC_ACCEL_BOOT_DESC_OFF_ARG0));
}

static void ppc_accel_probe_boot_marker_startup(ppc_accel_state_t *state)
{
  int polls;
  int sleep_between_ms;
  int i;

  if (state == NULL) {
    return;
  }

  polls = env_nonneg_int_or_default("PPC_ACCEL_BOOT_MARKER_POLLS", 20);
  sleep_between_ms = env_nonneg_int_or_default("PPC_ACCEL_BOOT_MARKER_SLEEP_MS", 1);
  if (polls <= 0) {
    return;
  }

  for (i = 0; i < polls; i++) {
    ppc_accel_poll_boot_marker(state);
    if ((state->have_boot_marker_last == true) && (state->boot_marker_last >= 2u)) {
      return;
    }
    if (sleep_between_ms > 0) {
      sleep_ms(sleep_between_ms);
    } else {
      sched_yield();
    }
  }

  LOG_WARN("[PPC-ACCEL] boot marker did not advance within startup probe"
           " (polls=%d sleep_ms=%d magic=0x%08" PRIx32
           " entry=0x%08" PRIx32 " stack=0x%08" PRIx32 " arg0=0x%08" PRIx32 ")\n",
           polls,
           sleep_between_ms,
           ppc_accel_boot_desc_read_field(state, PPC_ACCEL_BOOT_DESC_OFF_MAGIC),
           ppc_accel_boot_desc_read_field(state, PPC_ACCEL_BOOT_DESC_OFF_ENTRY),
           ppc_accel_boot_desc_read_field(state, PPC_ACCEL_BOOT_DESC_OFF_STACK),
           ppc_accel_boot_desc_read_field(state, PPC_ACCEL_BOOT_DESC_OFF_ARG0));
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

  ppc_accel_poll_boot_marker(state);

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
  write_be32(state->window, base + PPC_ACCEL_SHARED_INFO_OFF_RESERVED0, PPC_ACCEL_BOOT_DESC_OFFSET);
  write_be32(state->window, base + PPC_ACCEL_SHARED_INFO_OFF_RESERVED1, PPC_ACCEL_BOOT_DESC_SIZE);
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
  if (state->ppc_ram != NULL) {
    free(state->ppc_ram);
    state->ppc_ram = NULL;
    state->ppc_ram_base = 0u;
    state->ppc_ram_size = 0u;
  }
}

static bool ppc_accel_start_host_thread(ppc_accel_state_t *state)
{
  int thread_rc;

  if (state->host_thread_started == true) {
    return true;
  }

  memset(&state->host_service, 0, sizeof(state->host_service));
  state->host_service.mailbox = state->window + PPC_ACCEL_MAILBOX_OFFSET;
  state->host_service.window = state->window;
  state->host_service.window_size = PPC_ACCEL_Z2_SIZE;
  state->host_service.ppc_ram = state->ppc_ram;
  state->host_service.ppc_ram_base = state->ppc_ram_base;
  state->host_service.ppc_ram_size = state->ppc_ram_size;
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
  PPCMemoryRegion regions[3];
  int region_count;
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
  ppc_accel_reset_io_trace(state);

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
  if (state->trace_io_enabled == true) {
    LOG_INFO("[PPC-ACCEL] io callback tracing armed (limit=%" PRIu32 ")\n",
             state->trace_io_limit);
  }

  g_ppc_qemu_log_enabled = state->qemu_log_enabled;
  if (qemu_uae_loader_install_log_callback(&state->loader, ppc_accel_qemu_log) == false) {
    LOG_WARN("[PPC-ACCEL] install log callback failed: %s\n",
             qemu_uae_loader_error(&state->loader));
    goto fail;
  }
  if (state->qemu_log_enabled == true) {
    LOG_INFO("[PPC-ACCEL] qemu log callback armed\n");
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
  if (ppc_accel_prepare_ppc_ram(state) == false) {
    goto fail;
  }
  ppc_accel_write_boot_descriptor(state);

  if (ppc_accel_ranges_overlap(0u, PPC_ACCEL_Z2_SIZE, state->ppc_ram_base, state->ppc_ram_size) == true) {
    LOG_WARN("[PPC-ACCEL] PPC RAM mapping overlaps board window: base=0x%08" PRIx32
             " size=0x%08" PRIx32 "\n",
             state->ppc_ram_base, state->ppc_ram_size);
    goto fail;
  }
  if (ppc_accel_ranges_overlap(
          PPC_ACCEL_RESET_WINDOW_BASE, PPC_ACCEL_Z2_SIZE, state->ppc_ram_base, state->ppc_ram_size)
      == true) {
    LOG_WARN("[PPC-ACCEL] PPC RAM mapping overlaps reset window: base=0x%08" PRIx32
             " size=0x%08" PRIx32 "\n",
             state->ppc_ram_base, state->ppc_ram_size);
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
  if (install_boot_test_firmware(
          state->window,
          PPC_ACCEL_Z2_SIZE,
          PPC_ACCEL_FIRMWARE_ENTRY_BOOT_TEST,
          PPC_ACCEL_BOOT_DESC_OFFSET + PPC_ACCEL_BOOT_DESC_OFF_MARKER)
      == false) {
    LOG_WARN("[PPC-ACCEL] failed to install boot-test firmware\n");
    goto fail;
  }
  if (install_reset_trampoline(
          state->window,
          PPC_ACCEL_Z2_SIZE,
          PPC_ACCEL_FIRMWARE_ENTRY_SECONDARY,
          PPC_ACCEL_BOOT_DESC_OFFSET)
      == false) {
    LOG_WARN("[PPC-ACCEL] failed to install reset trampoline firmware\n");
    goto fail;
  }

  memset(regions, 0, sizeof(regions));
  regions[0].start = 0u;
  regions[0].size = PPC_ACCEL_Z2_SIZE;
  regions[0].memory = state->window;
  regions[0].name = (char *)"ppc-accel-z2-window";
  regions[0].alias = 0u;

  regions[1].start = state->ppc_ram_base;
  regions[1].size = state->ppc_ram_size;
  regions[1].memory = state->ppc_ram;
  regions[1].name = (char *)"ppc-accel-ppc-ram";
  regions[1].alias = 0u;

  regions[2].start = PPC_ACCEL_RESET_WINDOW_BASE;
  regions[2].size = PPC_ACCEL_Z2_SIZE;
  regions[2].memory = state->window;
  regions[2].name = (char *)"ppc-accel-reset-window";
  regions[2].alias = 0u;

  region_count = 3;
  state->loader.ppc_cpu_map_memory(regions, region_count);
  state->loader.ppc_cpu_reset();

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
    ppc_accel_probe_boot_marker_startup(state);
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
  ppc_accel_log_io_summary(state);
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
  case PPC_ACCEL_REG_PPC_RAM_BASE:
    if (state->ppc_ram_size != 0u) {
      return state->ppc_ram_base;
    }
    return env_u32_or_default("PPC_ACCEL_PPC_RAM_BASE", PPC_ACCEL_PPC_RAM_BASE_DEFAULT);
  case PPC_ACCEL_REG_PPC_RAM_SIZE:
    if (state->ppc_ram_size != 0u) {
      return state->ppc_ram_size;
    }
    return ppc_accel_env_ppc_ram_size_bytes_or_default();
  case PPC_ACCEL_REG_BOOT_MAGIC:
    return ppc_accel_boot_desc_read_field(state, PPC_ACCEL_BOOT_DESC_OFF_MAGIC);
  case PPC_ACCEL_REG_BOOT_ENTRY:
    return ppc_accel_boot_desc_read_field(state, PPC_ACCEL_BOOT_DESC_OFF_ENTRY);
  case PPC_ACCEL_REG_BOOT_STACK:
    return ppc_accel_boot_desc_read_field(state, PPC_ACCEL_BOOT_DESC_OFF_STACK);
  case PPC_ACCEL_REG_BOOT_ARG0:
    return ppc_accel_boot_desc_read_field(state, PPC_ACCEL_BOOT_DESC_OFF_ARG0);
  default:
    return read_be32(state->window, offset);
  }
}

static void ppc_accel_reg_write32(ppc_accel_state_t *state, uint32_t offset, uint32_t value)
{
  switch (offset) {
  case PPC_ACCEL_REG_CONTROL: {
    uint32_t new_control;
    bool running_ok;

    new_control = value & (PPC_ACCEL_CTRL_START | PPC_ACCEL_CTRL_RESET | PPC_ACCEL_CTRL_IRQ_ENABLE);
    LOG_INFO("[PPC-ACCEL] CONTROL write value=0x%08" PRIx32 " masked=0x%08" PRIx32 "\n",
             value,
             new_control);
    if ((new_control & PPC_ACCEL_CTRL_RESET) != 0u) {
      uint32_t keep_irq;

      keep_irq = new_control & PPC_ACCEL_CTRL_IRQ_ENABLE;
      ppc_accel_device_reset_state(state);
      state->control = keep_irq;
      state->status &= ~PPC_ACCEL_STATUS_FAULT;
      LOG_INFO("[PPC-ACCEL] CONTROL.RESET applied (irq_enable=%u)\n",
               (keep_irq & PPC_ACCEL_CTRL_IRQ_ENABLE) != 0u ? 1u : 0u);
    } else {
      state->control = new_control;
      if ((state->control & PPC_ACCEL_CTRL_START) != 0u) {
        running_ok = ppc_accel_set_running(state, true);
        LOG_INFO("[PPC-ACCEL] CONTROL.START requested -> %s\n",
                 running_ok == true ? "running" : "failed");
      } else {
        running_ok = ppc_accel_set_running(state, false);
        LOG_INFO("[PPC-ACCEL] CONTROL.START cleared -> %s\n",
                 running_ok == true ? "paused/stopped" : "failed");
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
  case PPC_ACCEL_REG_PPC_RAM_BASE:
  case PPC_ACCEL_REG_PPC_RAM_SIZE:
    break;
  case PPC_ACCEL_REG_BOOT_MAGIC:
    ppc_accel_boot_desc_write_field(state, PPC_ACCEL_BOOT_DESC_OFF_MAGIC, value);
    break;
  case PPC_ACCEL_REG_BOOT_ENTRY:
    ppc_accel_boot_desc_write_field(state, PPC_ACCEL_BOOT_DESC_OFF_ENTRY, value);
    break;
  case PPC_ACCEL_REG_BOOT_STACK:
    ppc_accel_boot_desc_write_field(state, PPC_ACCEL_BOOT_DESC_OFF_STACK, value);
    break;
  case PPC_ACCEL_REG_BOOT_ARG0:
    ppc_accel_boot_desc_write_field(state, PPC_ACCEL_BOOT_DESC_OFF_ARG0, value);
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
  uint8_t value;

  state = (ppc_accel_state_t *)dev->priv;
  if (offset >= PPC_ACCEL_Z2_SIZE) {
    return 0xFFu;
  }

  ppc_accel_poll_mailbox(state);

  if (offset < PPC_ACCEL_REG_WINDOW_SIZE) {
    reg_base = offset & ~0x3u;
    reg_value = ppc_accel_reg_read32(state, reg_base);
    shift = (3u - (offset & 0x3u)) * 8u;
    value = (uint8_t)((reg_value >> shift) & 0xFFu);
    ppc_accel_trace_mmio_event(state, false, 8, offset, (uint32_t)value);
    return value;
  }

  value = state->window[offset];
  ppc_accel_trace_diag_read(state, offset, value);
  ppc_accel_trace_mmio_event(state, false, 8, offset, (uint32_t)value);
  return value;
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
  uint32_t value;

  state = (ppc_accel_state_t *)dev->priv;
  if ((offset + 3u) >= PPC_ACCEL_Z2_SIZE) {
    return 0xFFFFFFFFu;
  }

  ppc_accel_poll_mailbox(state);

  if ((offset < PPC_ACCEL_REG_WINDOW_SIZE) && ((offset & 0x3u) == 0u)) {
    value = ppc_accel_reg_read32(state, offset);
    ppc_accel_trace_mmio_event(state, false, 32, offset, value);
    return value;
  }

  value = ((uint32_t)ppc_accel_read8(dev, offset + 0u) << 24)
          | ((uint32_t)ppc_accel_read8(dev, offset + 1u) << 16)
          | ((uint32_t)ppc_accel_read8(dev, offset + 2u) << 8)
          | (uint32_t)ppc_accel_read8(dev, offset + 3u);
  ppc_accel_trace_mmio_event(state, false, 32, offset, value);
  return value;
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

  ppc_accel_trace_mmio_event(state, true, 8, offset, (uint32_t)value);
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

  ppc_accel_trace_mmio_event(state, true, 32, offset, value);
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
  uint32_t ac_serial;
  uint32_t ac_diag_vec;

  if (g_ppc_accel_registered == true) {
    LOG_WARN("[ZORRO] Z2 PPC accelerator already registered; ignoring duplicate setvar\n");
    return;
  }

  if (g_ppc_accel_atexit_installed == false) {
    (void)atexit(ppc_accel_cleanup_at_exit);
    g_ppc_accel_atexit_installed = true;
  }

  ac_serial = env_u32_or_default("PPC_ACCEL_AC_SERIAL", PPC_ACCEL_AC_SERIAL_DEFAULT);
  ac_diag_vec = env_u32_or_default("PPC_ACCEL_AC_DIAG_VEC", PPC_ACCEL_AC_DIAG_VEC_DEFAULT);
  ppc_accel_set_autoconfig_serial(ac_serial);
  ppc_accel_set_autoconfig_diag_vec((uint16_t)(ac_diag_vec & 0xffffu));

  memset(&g_ppc_accel_state, 0, sizeof(g_ppc_accel_state));
  g_ppc_accel_state.verbose = env_bool_or_default("PPC_VERBOSE", false);
  g_ppc_accel_state.qemu_log_enabled = env_bool_or_default("PPC_ACCEL_QEMU_LOG", false);
  g_ppc_accel_state.trace_io_enabled = env_bool_or_default("PPC_ACCEL_TRACE_IO", false);
  g_ppc_accel_state.trace_io_limit = env_u32_or_default("PPC_ACCEL_TRACE_IO_LIMIT", 256u);
  g_ppc_accel_state.trace_mmio_enabled = env_bool_or_default("PPC_ACCEL_MMIO_TRACE", false);
  g_ppc_accel_state.trace_mmio_limit = env_u32_or_default("PPC_ACCEL_MMIO_TRACE_LIMIT", 512u);
  g_ppc_accel_state.diag_trace_enabled = env_bool_or_default("PPC_ACCEL_DIAG_TRACE", false);
  g_ppc_accel_state.diag_trace_limit = env_u32_or_default("PPC_ACCEL_DIAG_TRACE_LIMIT", 256u);
  g_ppc_accel_state.runtime_state = PPC_ACCEL_RUNTIME_STOPPED;
  ppc_accel_reset_io_trace(&g_ppc_accel_state);

  if (g_ppc_accel_state.verbose == true) {
    LOG_INFO("[ZORRO] z2-ppc-accel AutoConfig serial=0x%08" PRIx32 "\n", ac_serial);
    LOG_INFO("[ZORRO] z2-ppc-accel AutoConfig diagvec=0x%04" PRIx32 "\n",
             ac_diag_vec & 0xffffu);
    LOG_INFO("[PPC-ACCEL] qemu_log=%d trace_io=%d trace_io_limit=%" PRIu32 "\n",
             g_ppc_accel_state.qemu_log_enabled ? 1 : 0,
             g_ppc_accel_state.trace_io_enabled ? 1 : 0,
             g_ppc_accel_state.trace_io_limit);
    LOG_INFO("[PPC-ACCEL] mmio_trace=%d mmio_trace_limit=%" PRIu32 "\n",
             g_ppc_accel_state.trace_mmio_enabled ? 1 : 0,
             g_ppc_accel_state.trace_mmio_limit);
    LOG_INFO("[PPC-ACCEL] diag_trace=%d diag_trace_limit=%" PRIu32 "\n",
             g_ppc_accel_state.diag_trace_enabled ? 1 : 0,
             g_ppc_accel_state.diag_trace_limit);
  }

  LOG_INFO("[ZORRO] Registering Z2 PPC accelerator device.\n");
  ppc_accel_device_reset_state(&g_ppc_accel_state);

  slot = zorro_register_device(&z2_ppc_accel_device);
  if (slot < 0) {
    LOG_INFO("[ZORRO] Failed to register Z2 PPC accelerator device.\n");
    return;
  }
  g_ppc_accel_registered = true;
}
