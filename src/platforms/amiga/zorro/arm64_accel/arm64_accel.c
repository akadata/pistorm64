// SPDX-License-Identifier: MIT
#include <elf.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "arm64_accel.h"
#include "arm64_accel_regs.h"
#include "log.h"
#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"

typedef uint64_t (*arm64_accel_entry_fn)(void *job_ptr);

typedef struct arm64_accel_state {
  uint8_t window[ARM64_ACCEL_Z2_SIZE];
  pthread_mutex_t lock;
  pthread_t worker_thread;
  bool worker_started;
  bool worker_stop;
  int worker_idle_ms;
  bool worker_verbose;
  uint32_t control;
  uint32_t status;
  uint32_t irq_status;
  uint32_t heartbeat;
  bool mmio_trace;
  bool mmio_trace_limit_noted;
  uint32_t mmio_trace_limit;
  uint32_t mmio_trace_emitted;
} arm64_accel_state_t;

typedef struct arm64_accel_cmd_snapshot {
  uint32_t seq;
  uint32_t cmd;
  uint8_t *elf_image;
  uint32_t elf_size;
} arm64_accel_cmd_snapshot_t;

static arm64_accel_state_t g_arm64_accel_state;
static bool g_arm64_accel_registered;
static bool g_arm64_accel_atexit_installed;

static uint32_t env_u32_or_default(const char *name, uint32_t default_value) {
  const char *value = getenv(name);
  if (!value || value[0] == '\0') {
    return default_value;
  }
  char *end = NULL;
  unsigned long parsed = strtoul(value, &end, 0);
  if (end == value) {
    return default_value;
  }
  if (parsed > UINT32_MAX) {
    parsed = UINT32_MAX;
  }
  return (uint32_t)parsed;
}

static bool env_bool_enabled(const char *name) {
  const char *value = getenv(name);
  if (!value || value[0] == '\0') {
    return false;
  }
  return !(value[0] == '0' && value[1] == '\0');
}

static uint32_t arm64_accel_window_read32_nolock(const arm64_accel_state_t *state, uint32_t offset) {
  if (!state || offset > (ARM64_ACCEL_Z2_SIZE - 4u)) {
    return 0u;
  }
  return ((uint32_t)state->window[offset + 0u] << 24u) |
         ((uint32_t)state->window[offset + 1u] << 16u) |
         ((uint32_t)state->window[offset + 2u] << 8u) |
         (uint32_t)state->window[offset + 3u];
}

static void arm64_accel_window_write32_nolock(arm64_accel_state_t *state, uint32_t offset,
                                              uint32_t value) {
  if (!state || offset > (ARM64_ACCEL_Z2_SIZE - 4u)) {
    return;
  }
  state->window[offset + 0u] = (uint8_t)((value >> 24u) & 0xFFu);
  state->window[offset + 1u] = (uint8_t)((value >> 16u) & 0xFFu);
  state->window[offset + 2u] = (uint8_t)((value >> 8u) & 0xFFu);
  state->window[offset + 3u] = (uint8_t)(value & 0xFFu);
}

static void arm64_accel_jobdesc_write_u64_nolock(arm64_accel_state_t *state, uint32_t lo_off,
                                                  uint64_t value) {
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_JOBDESC_OFFSET + lo_off,
                                    (uint32_t)(value & 0xFFFFFFFFu));
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_JOBDESC_OFFSET + lo_off + 4u,
                                    (uint32_t)((value >> 32u) & 0xFFFFFFFFu));
}

static void arm64_accel_jobdesc_set_state_nolock(arm64_accel_state_t *state, uint32_t state_value,
                                                  uint32_t result, uint64_t retval) {
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_STATE,
                                    state_value);
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RESULT, result);
  arm64_accel_jobdesc_write_u64_nolock(state, ARM64_ACCEL_JOBDESC_OFF_RETVAL_LO, retval);
}

static void arm64_accel_trace_mmio_locked(arm64_accel_state_t *state, const char *op, uint32_t offset,
                                          uint32_t value) {
  if (!state || !state->mmio_trace) {
    return;
  }
  if (state->mmio_trace_emitted >= state->mmio_trace_limit) {
    if (!state->mmio_trace_limit_noted) {
      LOG_INFO("[ARM64-ACCEL] MMIO trace limit reached (%u events)\n", state->mmio_trace_limit);
      state->mmio_trace_limit_noted = true;
    }
    return;
  }
  state->mmio_trace_emitted++;
  LOG_INFO("[ARM64-ACCEL][MMIO] %s off=0x%04X val=0x%08X\n", op, offset, value);
}

static void arm64_accel_raise_irq_nolock(arm64_accel_state_t *state, uint32_t bits) {
  state->irq_status |= bits;
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_IRQ_STATUS, state->irq_status);
}

static void arm64_accel_write_shared_info_nolock(arm64_accel_state_t *state) {
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_SIGNATURE,
      ARM64_ACCEL_SHARED_INFO_SIGNATURE);
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_ABI_VERSION,
      ARM64_ACCEL_SHARED_INFO_ABI_VERSION);
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_MB_OFFSET,
      ARM64_ACCEL_MAILBOX_OFFSET);
  arm64_accel_window_write32_nolock(state,
                                    ARM64_ACCEL_SHARED_INFO_OFFSET +
                                        ARM64_ACCEL_SHARED_INFO_OFF_MB_SIZE,
                                    ARM64_ACCEL_MAILBOX_SIZE);
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_JOBDESC_OFF,
      ARM64_ACCEL_JOBDESC_OFFSET);
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_JOBDESC_SIZE,
      ARM64_ACCEL_JOBDESC_SIZE);
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_FEATURES,
      ARM64_ACCEL_SHARED_INFO_FEAT_MAILBOX | ARM64_ACCEL_SHARED_INFO_FEAT_IRQ |
          ARM64_ACCEL_SHARED_INFO_FEAT_SHARED_RAM);
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_RESERVED0, 0u);
}

static void arm64_accel_write_registers_nolock(arm64_accel_state_t *state) {
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_MAGIC, ARM64_ACCEL_MAGIC);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_ABI_VERSION, ARM64_ACCEL_ABI_VERSION);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_CONTROL, state->control);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_STATUS, state->status);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_IRQ_STATUS, state->irq_status);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_IRQ_ACK, 0u);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_MAILBOX_OFFSET, ARM64_ACCEL_MAILBOX_OFFSET);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_MAILBOX_SIZE, ARM64_ACCEL_MAILBOX_SIZE);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_SHARED_OFFSET, ARM64_ACCEL_SHARED_OFFSET);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_SHARED_SIZE, ARM64_ACCEL_SHARED_SIZE);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_JOBDESC_OFFSET, ARM64_ACCEL_JOBDESC_OFFSET);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_JOBDESC_SIZE, ARM64_ACCEL_JOBDESC_SIZE);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_HEARTBEAT, state->heartbeat);
}

static void arm64_accel_init_mailbox_nolock(arm64_accel_state_t *state) {
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_MAGIC,
                                    ARM64_ACCEL_MB_MAGIC);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_VERSION,
                                    ARM64_ACCEL_MB_VERSION);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_SEQ, 0u);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_ACK_SEQ, 0u);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_CMD,
                                    ARM64_ACCEL_MB_CMD_NONE);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_STATUS,
                                    ARM64_ACCEL_MB_STATUS_IDLE);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_ARG0, 0u);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_ARG1, 0u);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_ARG2, 0u);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_ARG3, 0u);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_RESULT0, 0u);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_RESULT1, 0u);
}

static void arm64_accel_init_jobdesc_nolock(arm64_accel_state_t *state) {
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_MAGIC,
                                    ARM64_ACCEL_JOBDESC_MAGIC);
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_VERSION,
      ARM64_ACCEL_JOBDESC_VERSION);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_FLAGS,
                                    0u);
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_ELF_OFFSET, 0u);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_ELF_SIZE,
                                    0u);
  arm64_accel_window_write32_nolock(
      state, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_ENTRY_ARG,
      ARM64_ACCEL_JOBDESC_OFFSET);
  arm64_accel_jobdesc_set_state_nolock(state, ARM64_ACCEL_JOBDESC_STATE_IDLE,
                                       ARM64_ACCEL_JOBDESC_RESULT_OK, 0u);
}

static uint64_t align_down_u64(uint64_t value, uint64_t align) { return value & ~(align - 1u); }

static uint64_t align_up_u64(uint64_t value, uint64_t align) {
  return (value + (align - 1u)) & ~(align - 1u);
}

static uint32_t arm64_accel_execute_elf(const uint8_t *image, uint32_t image_size, void *job_ptr,
                                        uint64_t *out_retval) {
  const Elf64_Ehdr *eh = (const Elf64_Ehdr *)image;
  const Elf64_Phdr *phdrs = NULL;
  uint64_t min_vaddr = UINT64_MAX;
  uint64_t max_vaddr = 0u;
  uint64_t min_page;
  uint64_t max_page;
  uint64_t map_span_u64;
  size_t map_span;
  long page_size_sys;
  uint64_t page_size;
  uint8_t *mapping;
  uint64_t i;
  bool saw_load = false;

  if (!image || image_size < sizeof(Elf64_Ehdr) || !out_retval) {
    return ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
  }
  if (eh->e_ident[EI_MAG0] != ELFMAG0 || eh->e_ident[EI_MAG1] != ELFMAG1 ||
      eh->e_ident[EI_MAG2] != ELFMAG2 || eh->e_ident[EI_MAG3] != ELFMAG3) {
    return ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
  }
  if (eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_ident[EI_DATA] != ELFDATA2LSB ||
      eh->e_machine != EM_AARCH64) {
    return ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
  }
  if (eh->e_phentsize < sizeof(Elf64_Phdr)) {
    return ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
  }
  if (eh->e_phoff > image_size) {
    return ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
  }

  {
    uint64_t ph_table_size = (uint64_t)eh->e_phentsize * (uint64_t)eh->e_phnum;
    if (ph_table_size > ((uint64_t)image_size - eh->e_phoff)) {
      return ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
    }
  }

  phdrs = (const Elf64_Phdr *)(image + eh->e_phoff);
  for (i = 0u; i < eh->e_phnum; i++) {
    const Elf64_Phdr *ph = (const Elf64_Phdr *)((const uint8_t *)phdrs + (i * eh->e_phentsize));
    uint64_t seg_end;
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0u) {
      continue;
    }
    if (ph->p_offset > image_size || ph->p_filesz > ((uint64_t)image_size - ph->p_offset)) {
      return ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
    }
    if (ph->p_filesz > ph->p_memsz) {
      return ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
    }
    if (ph->p_vaddr > UINT64_MAX - ph->p_memsz) {
      return ARM64_ACCEL_JOBDESC_RESULT_ERR_RANGE;
    }
    seg_end = ph->p_vaddr + ph->p_memsz;
    if (ph->p_vaddr < min_vaddr) {
      min_vaddr = ph->p_vaddr;
    }
    if (seg_end > max_vaddr) {
      max_vaddr = seg_end;
    }
    saw_load = true;
  }
  if (!saw_load || min_vaddr >= max_vaddr) {
    return ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
  }

  page_size_sys = sysconf(_SC_PAGESIZE);
  if (page_size_sys <= 0) {
    page_size = 4096u;
  } else {
    page_size = (uint64_t)page_size_sys;
  }
  min_page = align_down_u64(min_vaddr, page_size);
  max_page = align_up_u64(max_vaddr, page_size);
  if (max_page <= min_page) {
    return ARM64_ACCEL_JOBDESC_RESULT_ERR_RANGE;
  }
  map_span_u64 = max_page - min_page;
  if (map_span_u64 > (uint64_t)SIZE_MAX) {
    return ARM64_ACCEL_JOBDESC_RESULT_ERR_RANGE;
  }
  map_span = (size_t)map_span_u64;

  mapping = mmap(NULL, map_span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapping == MAP_FAILED) {
    return ARM64_ACCEL_JOBDESC_RESULT_ERR_LOAD;
  }

  for (i = 0u; i < eh->e_phnum; i++) {
    const Elf64_Phdr *ph = (const Elf64_Phdr *)((const uint8_t *)phdrs + (i * eh->e_phentsize));
    uint8_t *seg_dst;
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0u) {
      continue;
    }
    seg_dst = mapping + (size_t)(ph->p_vaddr - min_page);
    if (ph->p_filesz > 0u) {
      memcpy(seg_dst, image + ph->p_offset, (size_t)ph->p_filesz);
    }
    if (ph->p_memsz > ph->p_filesz) {
      memset(seg_dst + ph->p_filesz, 0, (size_t)(ph->p_memsz - ph->p_filesz));
    }
  }

  for (i = 0u; i < eh->e_phnum; i++) {
    const Elf64_Phdr *ph = (const Elf64_Phdr *)((const uint8_t *)phdrs + (i * eh->e_phentsize));
    uint64_t seg_start;
    uint64_t seg_end;
    uint64_t seg_len_u64;
    size_t seg_len;
    int prot = 0;

    if (ph->p_type != PT_LOAD || ph->p_memsz == 0u) {
      continue;
    }
    seg_start = align_down_u64(ph->p_vaddr, page_size);
    seg_end = align_up_u64(ph->p_vaddr + ph->p_memsz, page_size);
    seg_len_u64 = seg_end - seg_start;
    if (seg_len_u64 == 0u || seg_len_u64 > (uint64_t)SIZE_MAX) {
      munmap(mapping, map_span);
      return ARM64_ACCEL_JOBDESC_RESULT_ERR_LOAD;
    }
    seg_len = (size_t)seg_len_u64;

    if ((ph->p_flags & PF_R) != 0u) {
      prot |= PROT_READ;
    }
    if ((ph->p_flags & PF_W) != 0u) {
      prot |= PROT_WRITE;
    }
    if ((ph->p_flags & PF_X) != 0u) {
      prot |= PROT_EXEC;
    }
    if (prot == 0) {
      prot = PROT_READ;
    }

    if (mprotect(mapping + (size_t)(seg_start - min_page), seg_len, prot) != 0) {
      munmap(mapping, map_span);
      return ARM64_ACCEL_JOBDESC_RESULT_ERR_LOAD;
    }
  }

  if (eh->e_entry < min_vaddr || eh->e_entry >= max_vaddr) {
    munmap(mapping, map_span);
    return ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
  }

  {
    arm64_accel_entry_fn entry =
        (arm64_accel_entry_fn)(void *)(mapping + (size_t)(eh->e_entry - min_page));
    *out_retval = entry(job_ptr);
  }

  munmap(mapping, map_span);
  return ARM64_ACCEL_JOBDESC_RESULT_OK;
}

static bool arm64_accel_prepare_run_elf_locked(arm64_accel_state_t *state,
                                                arm64_accel_cmd_snapshot_t *snapshot,
                                                uint32_t *out_job_result) {
  uint32_t job_magic;
  uint32_t job_version;
  uint32_t elf_offset;
  uint32_t elf_size;

  if (!state || !snapshot || !out_job_result) {
    return false;
  }

  snapshot->elf_image = NULL;
  snapshot->elf_size = 0u;

  job_magic =
      arm64_accel_window_read32_nolock(state, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_MAGIC);
  job_version = arm64_accel_window_read32_nolock(state,
                                                 ARM64_ACCEL_JOBDESC_OFFSET +
                                                     ARM64_ACCEL_JOBDESC_OFF_VERSION);
  elf_offset = arm64_accel_window_read32_nolock(
      state, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_ELF_OFFSET);
  elf_size = arm64_accel_window_read32_nolock(state,
                                              ARM64_ACCEL_JOBDESC_OFFSET +
                                                  ARM64_ACCEL_JOBDESC_OFF_ELF_SIZE);

  if (job_magic != ARM64_ACCEL_JOBDESC_MAGIC || job_version != ARM64_ACCEL_JOBDESC_VERSION) {
    *out_job_result = ARM64_ACCEL_JOBDESC_RESULT_ERR_FORMAT;
    arm64_accel_jobdesc_set_state_nolock(state, ARM64_ACCEL_JOBDESC_STATE_ERROR, *out_job_result, 0u);
    return false;
  }
  if (elf_size == 0u || elf_offset >= ARM64_ACCEL_Z2_SIZE ||
      elf_size > (ARM64_ACCEL_Z2_SIZE - elf_offset)) {
    *out_job_result = ARM64_ACCEL_JOBDESC_RESULT_ERR_RANGE;
    arm64_accel_jobdesc_set_state_nolock(state, ARM64_ACCEL_JOBDESC_STATE_ERROR, *out_job_result, 0u);
    return false;
  }

  snapshot->elf_image = (uint8_t *)malloc(elf_size);
  if (!snapshot->elf_image) {
    *out_job_result = ARM64_ACCEL_JOBDESC_RESULT_ERR_INTERNAL;
    arm64_accel_jobdesc_set_state_nolock(state, ARM64_ACCEL_JOBDESC_STATE_ERROR, *out_job_result, 0u);
    return false;
  }
  memcpy(snapshot->elf_image, &state->window[elf_offset], elf_size);
  snapshot->elf_size = elf_size;
  *out_job_result = ARM64_ACCEL_JOBDESC_RESULT_OK;
  arm64_accel_jobdesc_set_state_nolock(state, ARM64_ACCEL_JOBDESC_STATE_RUNNING,
                                       ARM64_ACCEL_JOBDESC_RESULT_OK, 0u);
  return true;
}

static void arm64_accel_finish_mailbox_cmd_locked(arm64_accel_state_t *state, uint32_t seq,
                                                   uint32_t mb_status, uint32_t result0,
                                                   uint32_t result1) {
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_RESULT0,
                                    result0);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_RESULT1,
                                    result1);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_STATUS,
                                    mb_status);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_ACK_SEQ, seq);

  if ((state->control & ARM64_ACCEL_CTRL_IRQ_ENABLE) != 0u) {
    arm64_accel_raise_irq_nolock(state, ARM64_ACCEL_IRQ_JOB_DONE);
  }
  state->status = ARM64_ACCEL_STATUS_READY;
  state->heartbeat++;
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_STATUS, state->status);
  arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_HEARTBEAT, state->heartbeat);
}

static void *arm64_accel_worker_main(void *opaque) {
  arm64_accel_state_t *state = (arm64_accel_state_t *)opaque;
  uint32_t idle_ms = (uint32_t)state->worker_idle_ms;

  while (!state->worker_stop) {
    arm64_accel_cmd_snapshot_t snap;
    uint32_t seq;
    uint32_t ack;
    uint32_t cmd;
    bool have_work = false;
    uint32_t mb_status = ARM64_ACCEL_MB_STATUS_DONE;
    uint32_t result0 = 0u;
    uint32_t result1 = 0u;
    uint32_t job_result = ARM64_ACCEL_JOBDESC_RESULT_OK;
    uint64_t job_retval = 0u;

    memset(&snap, 0, sizeof(snap));

    pthread_mutex_lock(&state->lock);
    if ((state->control & ARM64_ACCEL_CTRL_START) != 0u) {
      seq = arm64_accel_window_read32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_SEQ);
      ack =
          arm64_accel_window_read32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_ACK_SEQ);
      if (seq != ack) {
        cmd = arm64_accel_window_read32_nolock(state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_CMD);
        state->status = ARM64_ACCEL_STATUS_READY | ARM64_ACCEL_STATUS_BUSY;
        arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_STATUS, state->status);
        arm64_accel_window_write32_nolock(
            state, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_STATUS, ARM64_ACCEL_MB_STATUS_BUSY);

        snap.seq = seq;
        snap.cmd = cmd;
        have_work = true;

        if (cmd == ARM64_ACCEL_MB_CMD_RUN_ELF) {
          (void)arm64_accel_prepare_run_elf_locked(state, &snap, &job_result);
        }
      }
    }
    pthread_mutex_unlock(&state->lock);

    if (!have_work) {
      if (idle_ms == 0u) {
        sched_yield();
      } else {
        usleep(idle_ms * 1000u);
      }
      continue;
    }

    switch (snap.cmd) {
    case ARM64_ACCEL_MB_CMD_NONE:
      mb_status = ARM64_ACCEL_MB_STATUS_DONE;
      result0 = 0u;
      break;
    case ARM64_ACCEL_MB_CMD_PING:
      mb_status = ARM64_ACCEL_MB_STATUS_DONE;
      result0 = ARM64_ACCEL_MAGIC;
      break;
    case ARM64_ACCEL_MB_CMD_RUN_ELF:
      if (snap.elf_image == NULL || job_result != ARM64_ACCEL_JOBDESC_RESULT_OK) {
        mb_status = ARM64_ACCEL_MB_STATUS_ERR;
        result0 = job_result;
      } else {
        job_result = arm64_accel_execute_elf(
            snap.elf_image, snap.elf_size, (void *)&state->window[ARM64_ACCEL_JOBDESC_OFFSET], &job_retval);
        if (job_result == ARM64_ACCEL_JOBDESC_RESULT_OK) {
          mb_status = ARM64_ACCEL_MB_STATUS_DONE;
          result0 = ARM64_ACCEL_JOBDESC_RESULT_OK;
          result1 = (uint32_t)(job_retval & 0xFFFFFFFFu);
        } else {
          mb_status = ARM64_ACCEL_MB_STATUS_ERR;
          result0 = job_result;
          result1 = 0u;
        }
      }
      break;
    default:
      mb_status = ARM64_ACCEL_MB_STATUS_ERR;
      result0 = ARM64_ACCEL_JOBDESC_RESULT_ERR_INTERNAL;
      break;
    }

    pthread_mutex_lock(&state->lock);
    if (snap.cmd == ARM64_ACCEL_MB_CMD_RUN_ELF) {
      if (job_result == ARM64_ACCEL_JOBDESC_RESULT_OK) {
        arm64_accel_jobdesc_set_state_nolock(state, ARM64_ACCEL_JOBDESC_STATE_DONE,
                                             ARM64_ACCEL_JOBDESC_RESULT_OK, job_retval);
      } else {
        arm64_accel_jobdesc_set_state_nolock(state, ARM64_ACCEL_JOBDESC_STATE_ERROR, job_result, 0u);
      }
    }
    arm64_accel_finish_mailbox_cmd_locked(state, snap.seq, mb_status, result0, result1);
    if (state->worker_verbose) {
      LOG_INFO("[ARM64-ACCEL] cmd=%u seq=%u mb_status=%u result0=0x%08X result1=0x%08X\n",
               snap.cmd, snap.seq, mb_status, result0, result1);
    }
    pthread_mutex_unlock(&state->lock);

    if (snap.elf_image) {
      free(snap.elf_image);
    }
  }
  return NULL;
}

static void arm64_accel_worker_stop(void) {
  arm64_accel_state_t *state = &g_arm64_accel_state;
  if (!state->worker_started) {
    return;
  }
  state->worker_stop = true;
  pthread_join(state->worker_thread, NULL);
  state->worker_started = false;
}

static bool arm64_accel_worker_start(arm64_accel_state_t *state) {
  if (state->worker_started) {
    return true;
  }
  state->worker_stop = false;
  if (pthread_create(&state->worker_thread, NULL, arm64_accel_worker_main, state) != 0) {
    LOG_WARN("[ARM64-ACCEL] failed to start worker thread: %s\n", strerror(errno));
    return false;
  }
  state->worker_started = true;
  LOG_INFO("[ARM64-ACCEL] worker thread started (idle_ms=%d verbose=%d)\n", state->worker_idle_ms,
           state->worker_verbose ? 1 : 0);
  return true;
}

static void arm64_accel_reset_nolock(arm64_accel_state_t *state) {
  memset(state->window, 0, sizeof(state->window));

  state->control = 0u;
  state->status = ARM64_ACCEL_STATUS_READY;
  state->irq_status = 0u;
  state->heartbeat = 0u;
  state->mmio_trace_emitted = 0u;
  state->mmio_trace_limit_noted = false;
  state->mmio_trace = env_bool_enabled("ARM_ACCEL_MMIO_TRACE");
  state->mmio_trace_limit = env_u32_or_default("ARM_ACCEL_TRACE_LIMIT", 1024u);
  if (state->mmio_trace_limit == 0u) {
    state->mmio_trace_limit = 1u;
  }

  arm64_accel_write_registers_nolock(state);
  arm64_accel_init_mailbox_nolock(state);
  arm64_accel_write_shared_info_nolock(state);
  arm64_accel_init_jobdesc_nolock(state);
}

static void arm64_accel_reset(zorro_device_t *dev) {
  arm64_accel_state_t *state = (arm64_accel_state_t *)dev->priv;
  pthread_mutex_lock(&state->lock);
  arm64_accel_reset_nolock(state);
  pthread_mutex_unlock(&state->lock);
}

static uint32_t arm64_accel_read32(zorro_device_t *dev, uint32_t offset) {
  arm64_accel_state_t *state = (arm64_accel_state_t *)dev->priv;
  uint32_t reg = offset & ~0x3u;
  uint32_t value;
  pthread_mutex_lock(&state->lock);
  value = arm64_accel_window_read32_nolock(state, reg);
  arm64_accel_trace_mmio_locked(state, "R32", reg, value);
  pthread_mutex_unlock(&state->lock);
  return value;
}

static uint16_t arm64_accel_read16(zorro_device_t *dev, uint32_t offset) {
  uint32_t base = offset & ~0x3u;
  uint32_t value = arm64_accel_read32(dev, base);
  if ((offset & 0x2u) != 0u) {
    return (uint16_t)(value & 0xFFFFu);
  }
  return (uint16_t)((value >> 16u) & 0xFFFFu);
}

static uint8_t arm64_accel_read8(zorro_device_t *dev, uint32_t offset) {
  uint32_t base = offset & ~0x3u;
  uint32_t value = arm64_accel_read32(dev, base);
  uint32_t shift = (3u - (offset & 0x3u)) * 8u;
  return (uint8_t)((value >> shift) & 0xFFu);
}

static void arm64_accel_write32(zorro_device_t *dev, uint32_t offset, uint32_t value) {
  arm64_accel_state_t *state = (arm64_accel_state_t *)dev->priv;
  uint32_t reg = offset & ~0x3u;

  pthread_mutex_lock(&state->lock);
  switch (reg) {
  case ARM64_ACCEL_REG_CONTROL:
    state->control =
        value & (ARM64_ACCEL_CTRL_START | ARM64_ACCEL_CTRL_STOP | ARM64_ACCEL_CTRL_RESET |
                 ARM64_ACCEL_CTRL_IRQ_ENABLE);
    arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_CONTROL, state->control);

    if ((state->control & ARM64_ACCEL_CTRL_RESET) != 0u) {
      state->irq_status = 0u;
      state->status = ARM64_ACCEL_STATUS_READY;
      state->heartbeat = 0u;
      arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_IRQ_STATUS, 0u);
      arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_STATUS, state->status);
      arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_HEARTBEAT, 0u);
      arm64_accel_init_mailbox_nolock(state);
      arm64_accel_init_jobdesc_nolock(state);
      LOG_INFO("[ARM64-ACCEL] CONTROL.RESET applied\n");
    } else if ((state->control & ARM64_ACCEL_CTRL_STOP) != 0u) {
      state->status = ARM64_ACCEL_STATUS_READY;
      arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_STATUS, state->status);
      LOG_INFO("[ARM64-ACCEL] CONTROL.STOP applied\n");
    } else if ((state->control & ARM64_ACCEL_CTRL_START) != 0u) {
      LOG_INFO("[ARM64-ACCEL] CONTROL.START requested\n");
    }
    break;
  case ARM64_ACCEL_REG_IRQ_ACK:
    state->irq_status &= ~value;
    arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_IRQ_STATUS, state->irq_status);
    arm64_accel_window_write32_nolock(state, ARM64_ACCEL_REG_IRQ_ACK, 0u);
    break;
  default:
    arm64_accel_window_write32_nolock(state, reg, value);
    break;
  }
  arm64_accel_trace_mmio_locked(state, "W32", reg, value);
  pthread_mutex_unlock(&state->lock);
}

static void arm64_accel_write16(zorro_device_t *dev, uint32_t offset, uint16_t value) {
  uint32_t base = offset & ~0x3u;
  uint32_t current = arm64_accel_read32(dev, base);
  if ((offset & 0x2u) != 0u) {
    current = (current & 0xFFFF0000u) | (uint32_t)value;
  } else {
    current = (current & 0x0000FFFFu) | ((uint32_t)value << 16u);
  }
  arm64_accel_write32(dev, base, current);
}

static void arm64_accel_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  uint32_t base = offset & ~0x3u;
  uint32_t shift = (3u - (offset & 0x3u)) * 8u;
  uint32_t current = arm64_accel_read32(dev, base);
  current &= ~(0xFFu << shift);
  current |= ((uint32_t)value << shift);
  arm64_accel_write32(dev, base, current);
}

static uint8_t arm64_accel_rom[] = {
    Z2_Z2 | Z2_BOOTROM,
    AC_MEM_SIZE_4MB,
    (uint8_t)((ARM64_ACCEL_PRODUCT_ID >> 4u) & 0x0Fu),
    (uint8_t)(ARM64_ACCEL_PRODUCT_ID & 0x0Fu),
    0x0,
    0x0,
    0x0,
    0x0,
    PISTORM_AC_MANUF_ID,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
};

static zorro_device_t arm64_accel_device = {
    .name = "z2-arm64-accel",
    .bus = ZORRO_BUS_Z2,
    .size = ARM64_ACCEL_Z2_SIZE,
    .manufacturer = ARM64_ACCEL_MANUFACTURER_ID,
    .product = ARM64_ACCEL_PRODUCT_ID,
    .flags = 0,
    .ac_rom = arm64_accel_rom,
    .ac_rom_size = sizeof(arm64_accel_rom),
    .reset = arm64_accel_reset,
    .read8 = arm64_accel_read8,
    .read16 = arm64_accel_read16,
    .read32 = arm64_accel_read32,
    .write8 = arm64_accel_write8,
    .write16 = arm64_accel_write16,
    .write32 = arm64_accel_write32,
    .priv = &g_arm64_accel_state,
};

void z2_arm64_accel_register(void) {
  arm64_accel_state_t *state = &g_arm64_accel_state;

  if (g_arm64_accel_registered) {
    return;
  }

  memset(state, 0, sizeof(*state));
  pthread_mutex_init(&state->lock, NULL);
  state->worker_idle_ms = (int)env_u32_or_default("ARM_ACCEL_WORKER_IDLE_MS", 0u);
  if (state->worker_idle_ms < 0) {
    state->worker_idle_ms = 0;
  }
  state->worker_verbose = env_bool_enabled("ARM_ACCEL_WORKER_VERBOSE");

  pthread_mutex_lock(&state->lock);
  arm64_accel_reset_nolock(state);
  pthread_mutex_unlock(&state->lock);

  LOG_INFO("[ZORRO] Registering Z2 ARM64 accelerator device.\n");
  if (!arm64_accel_worker_start(state)) {
    LOG_WARN("[ZORRO] ARM64 worker unavailable; mailbox commands will not execute.\n");
  } else if (!g_arm64_accel_atexit_installed) {
    atexit(arm64_accel_worker_stop);
    g_arm64_accel_atexit_installed = true;
  }

  {
    int slot = zorro_register_device(&arm64_accel_device);
    if (slot < 0) {
      LOG_INFO("[ZORRO] Failed to register Z2 ARM64 accelerator device.\n");
      arm64_accel_worker_stop();
      return;
    }
  }
  g_arm64_accel_registered = true;
}
