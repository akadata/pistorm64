// SPDX-License-Identifier: MIT
#include <proto/exec.h>
#include <proto/expansion.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm64_accel_regs.h"
#include "armaccel_device.h"
#include "armaccel_service_abi.h"

#define ARMACCEL_BUSY_SETTLE_SPINS 50000u
#define ARMACCEL_RUN_ELF_BUSY_RETRIES 64u
#define ARMACCEL_RUN_ELF_BUSY_BACKOFF_SPINS 20000u
#define ARMACCEL_ELF_IMAGE_OFFSET (ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_SIZE)

static ULONG read_be32(volatile UBYTE *base, ULONG offset) {
  ULONG b0 = (ULONG)base[offset + 0u];
  ULONG b1 = (ULONG)base[offset + 1u];
  ULONG b2 = (ULONG)base[offset + 2u];
  ULONG b3 = (ULONG)base[offset + 3u];
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

static void write_be32(volatile UBYTE *base, ULONG offset, ULONG value) {
  base[offset + 0u] = (UBYTE)((value >> 24) & 0xFFu);
  base[offset + 1u] = (UBYTE)((value >> 16) & 0xFFu);
  base[offset + 2u] = (UBYTE)((value >> 8) & 0xFFu);
  base[offset + 3u] = (UBYTE)(value & 0xFFu);
}

static void write_bytes(volatile UBYTE *base, ULONG offset, const UBYTE *src, ULONG len) {
  ULONG i;
  for (i = 0u; i < len; i++) {
    base[offset + i] = src[i];
  }
}

static void clear_jobdesc_extension(volatile UBYTE *base) {
  ULONG off;
  for (off = 0x40u; off < ARM64_ACCEL_JOBDESC_SIZE; off += 4u) {
    write_be32(base, ARM64_ACCEL_JOBDESC_OFFSET + off, 0u);
  }
}

static void ensure_started(volatile UBYTE *board) {
  ULONG control = read_be32(board, ARM64_ACCEL_REG_CONTROL);
  if ((control & ARM64_ACCEL_CTRL_START) == 0u) {
    control |= ARM64_ACCEL_CTRL_START;
    write_be32(board, ARM64_ACCEL_REG_CONTROL, control);
  }
}

static int service_frame_is_pending(volatile UBYTE *board) {
  ULONG frame_base;
  ULONG magic;
  ULONG version;
  ULONG state;

  if (board == NULL) {
    return 0;
  }

  frame_base = ARM64_ACCEL_JOBDESC_OFFSET + ARMACCEL_SVC_FRAME_JOBDESC_OFF;
  magic = read_be32(board, frame_base + ARMACCEL_SVC_OFF_MAGIC);
  if (magic != ARMACCEL_SVC_FRAME_MAGIC) {
    return 0;
  }
  version = read_be32(board, frame_base + ARMACCEL_SVC_OFF_VERSION);
  if (version != ARMACCEL_SVC_FRAME_VERSION) {
    return 0;
  }
  state = read_be32(board, frame_base + ARMACCEL_SVC_OFF_STATE);
  return (state == ARMACCEL_SVC_STATE_PENDING) ? 1 : 0;
}

static void service_frame_fail(volatile UBYTE *board, ULONG result0, ULONG result1) {
  ULONG frame_base;

  if (board == NULL) {
    return;
  }

  frame_base = ARM64_ACCEL_JOBDESC_OFFSET + ARMACCEL_SVC_FRAME_JOBDESC_OFF;
  write_be32(board, frame_base + ARMACCEL_SVC_OFF_RESULT0, result0);
  write_be32(board, frame_base + ARMACCEL_SVC_OFF_RESULT1, result1);
  write_be32(board, frame_base + ARMACCEL_SVC_OFF_STATE, ARMACCEL_SVC_STATE_ERROR);
}

static ULONG do_mailbox_roundtrip(volatile UBYTE *board, ULONG cmd, ULONG arg0, ULONG arg1, ULONG arg2,
                                  ULONG arg3, ULONG wait_spins, ULONG *out_result1,
                                  ArmAccelServiceHook service_hook, APTR service_ctx,
                                  ULONG *out_service_dispatch_count, ULONG *out_service_hook_status) {
  volatile UBYTE *mailbox;
  ULONG current_seq;
  ULONG current_ack;
  ULONG published_seq;
  ULONG seq;
  ULONG timeout;
  ULONG ack;
  ULONG status;
  ULONG service_dispatch_count = 0u;
  ULONG service_hook_status = 0u;

  ensure_started(board);
  mailbox = board + read_be32(board, ARM64_ACCEL_REG_MAILBOX_OFFSET);
  if (read_be32(mailbox, ARM64_ACCEL_MB_OFF_MAGIC) != ARM64_ACCEL_MB_MAGIC) {
    return ARMACCEL_DEVICE_ERROR_RESULT;
  }

  current_seq = read_be32(mailbox, ARM64_ACCEL_MB_OFF_SEQ);
  current_ack = read_be32(mailbox, ARM64_ACCEL_MB_OFF_ACK_SEQ);
  if (current_seq != current_ack) {
    ULONG settle = ARMACCEL_BUSY_SETTLE_SPINS;
    while (settle > 0u) {
      current_seq = read_be32(mailbox, ARM64_ACCEL_MB_OFF_SEQ);
      current_ack = read_be32(mailbox, ARM64_ACCEL_MB_OFF_ACK_SEQ);
      if (current_seq == current_ack) {
        break;
      }
      settle--;
    }
  }
  if (current_seq != current_ack) {
    return ARMACCEL_DEVICE_BUSY_RESULT;
  }

  seq = current_seq + 1u;
  if (seq == 0u) {
    seq = 1u;
  }

  write_be32(mailbox, ARM64_ACCEL_MB_OFF_CMD, cmd);
  write_be32(mailbox, ARM64_ACCEL_MB_OFF_ARG0, arg0);
  write_be32(mailbox, ARM64_ACCEL_MB_OFF_ARG1, arg1);
  write_be32(mailbox, ARM64_ACCEL_MB_OFF_ARG2, arg2);
  write_be32(mailbox, ARM64_ACCEL_MB_OFF_ARG3, arg3);
  write_be32(mailbox, ARM64_ACCEL_MB_OFF_RESULT0, 0u);
  write_be32(mailbox, ARM64_ACCEL_MB_OFF_RESULT1, 0u);
  write_be32(mailbox, ARM64_ACCEL_MB_OFF_STATUS, ARM64_ACCEL_MB_STATUS_IDLE);
  write_be32(mailbox, ARM64_ACCEL_MB_OFF_SEQ, seq);

  published_seq = read_be32(mailbox, ARM64_ACCEL_MB_OFF_SEQ);
  if (published_seq != seq) {
    return ARMACCEL_DEVICE_BUSY_RESULT;
  }

  timeout = wait_spins;
  while (timeout > 0u) {
    ULONG seen_seq = read_be32(mailbox, ARM64_ACCEL_MB_OFF_SEQ);
    if (seen_seq != seq) {
      return ARMACCEL_DEVICE_BUSY_RESULT;
    }
    ack = read_be32(mailbox, ARM64_ACCEL_MB_OFF_ACK_SEQ);
    status = read_be32(mailbox, ARM64_ACCEL_MB_OFF_STATUS);
    if ((ack == seq) &&
        ((status == ARM64_ACCEL_MB_STATUS_DONE) || (status == ARM64_ACCEL_MB_STATUS_ERR))) {
      break;
    }

    if ((service_hook != NULL) && service_frame_is_pending(board)) {
      ULONG hook_rc = service_hook(board, service_ctx);
      service_dispatch_count++;
      service_hook_status = hook_rc;
      if (hook_rc != 0u) {
        service_frame_fail(board, ARMACCEL_SVC_RES_INTERNAL,
                           ARMACCEL_SVC_ERR_PACK(ARMACCEL_SVC_ERRNS_EXEC, hook_rc));
      }
    }

    timeout--;
  }
  if (timeout == 0u) {
    return ARMACCEL_DEVICE_ERROR_RESULT;
  }

  status = read_be32(mailbox, ARM64_ACCEL_MB_OFF_STATUS);
  if (status != ARM64_ACCEL_MB_STATUS_DONE) {
    return ARMACCEL_DEVICE_ERROR_RESULT;
  }
  if (out_result1 != NULL) {
    *out_result1 = read_be32(mailbox, ARM64_ACCEL_MB_OFF_RESULT1);
  }
  if (out_service_dispatch_count != NULL) {
    *out_service_dispatch_count = service_dispatch_count;
  }
  if (out_service_hook_status != NULL) {
    *out_service_hook_status = service_hook_status;
  }
  return read_be32(mailbox, ARM64_ACCEL_MB_OFF_RESULT0);
}

int armaccel_device_open(struct armaccel_board *out_board) {
  struct ExpansionBase *expansion_base;
  struct ConfigDev *cd;

  if (out_board == NULL) {
    return 1;
  }
  out_board->expansion_base = NULL;
  out_board->config_dev = NULL;
  out_board->base = NULL;

  expansion_base = (struct ExpansionBase *)OpenLibrary((STRPTR)"expansion.library", 0L);
  if (expansion_base == NULL) {
    return 2;
  }

  cd = FindConfigDev(NULL, ARM64_ACCEL_MANUFACTURER_ID, ARM64_ACCEL_PRODUCT_ID);
  if (cd == NULL) {
    CloseLibrary((struct Library *)expansion_base);
    return 3;
  }

  out_board->expansion_base = expansion_base;
  out_board->config_dev = cd;
  out_board->base = (volatile UBYTE *)cd->cd_BoardAddr;
  return 0;
}

void armaccel_device_close(struct armaccel_board *board) {
  if ((board != NULL) && (board->expansion_base != NULL)) {
    CloseLibrary((struct Library *)board->expansion_base);
    board->expansion_base = NULL;
    board->config_dev = NULL;
    board->base = NULL;
  }
}

int armaccel_device_dump_identity(struct armaccel_board *board) {
  ULONG magic;
  ULONG abi_version;
  ULONG control;
  ULONG status;
  ULONG irq_status;
  ULONG mb_offset;
  ULONG mb_size;
  ULONG shared_offset;
  ULONG shared_size;
  ULONG jobdesc_offset;
  ULONG jobdesc_size;
  ULONG heartbeat;
  ULONG shared_sig;
  ULONG shared_abi;
  ULONG shared_mb_offset;
  ULONG shared_mb_size;
  ULONG shared_jobdesc_offset;
  ULONG shared_jobdesc_size;
  ULONG shared_features;
  ULONG mb_magic;
  ULONG mb_version;
  ULONG mb_seq;
  ULONG mb_ack;
  ULONG mb_cmd;
  ULONG mb_status;

  if ((board == NULL) || (board->base == NULL)) {
    return 1;
  }

  magic = read_be32(board->base, ARM64_ACCEL_REG_MAGIC);
  abi_version = read_be32(board->base, ARM64_ACCEL_REG_ABI_VERSION);
  control = read_be32(board->base, ARM64_ACCEL_REG_CONTROL);
  status = read_be32(board->base, ARM64_ACCEL_REG_STATUS);
  irq_status = read_be32(board->base, ARM64_ACCEL_REG_IRQ_STATUS);
  mb_offset = read_be32(board->base, ARM64_ACCEL_REG_MAILBOX_OFFSET);
  mb_size = read_be32(board->base, ARM64_ACCEL_REG_MAILBOX_SIZE);
  shared_offset = read_be32(board->base, ARM64_ACCEL_REG_SHARED_OFFSET);
  shared_size = read_be32(board->base, ARM64_ACCEL_REG_SHARED_SIZE);
  jobdesc_offset = read_be32(board->base, ARM64_ACCEL_REG_JOBDESC_OFFSET);
  jobdesc_size = read_be32(board->base, ARM64_ACCEL_REG_JOBDESC_SIZE);
  heartbeat = read_be32(board->base, ARM64_ACCEL_REG_HEARTBEAT);

  shared_sig = read_be32(board->base,
                         ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_SIGNATURE);
  shared_abi = read_be32(board->base,
                         ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_ABI_VERSION);
  shared_mb_offset = read_be32(board->base,
                               ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_MB_OFFSET);
  shared_mb_size = read_be32(board->base,
                             ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_MB_SIZE);
  shared_jobdesc_offset =
      read_be32(board->base,
                ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_JOBDESC_OFF);
  shared_jobdesc_size =
      read_be32(board->base,
                ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_JOBDESC_SIZE);
  shared_features = read_be32(board->base,
                              ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_FEATURES);

  mb_magic = read_be32(board->base, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_MAGIC);
  mb_version = read_be32(board->base, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_VERSION);
  mb_seq = read_be32(board->base, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_SEQ);
  mb_ack = read_be32(board->base, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_ACK_SEQ);
  mb_cmd = read_be32(board->base, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_CMD);
  mb_status = read_be32(board->base, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_STATUS);

  printf("Board identity:\n");
  printf("  MAGIC         = $%08X\n", (unsigned int)magic);
  printf("  ABI_VERSION   = %u\n", (unsigned int)abi_version);
  printf("  CONTROL       = $%08X\n", (unsigned int)control);
  printf("  STATUS        = $%08X (READY=%u BUSY=%u FAULT=%u)\n", (unsigned int)status,
         (unsigned int)((status & ARM64_ACCEL_STATUS_READY) != 0u),
         (unsigned int)((status & ARM64_ACCEL_STATUS_BUSY) != 0u),
         (unsigned int)((status & ARM64_ACCEL_STATUS_FAULT) != 0u));
  printf("  IRQ_STATUS    = $%08X\n", (unsigned int)irq_status);
  printf("  MAILBOX       = off=$%08X size=$%08X\n", (unsigned int)mb_offset,
         (unsigned int)mb_size);
  printf("  SHARED        = off=$%08X size=$%08X\n", (unsigned int)shared_offset,
         (unsigned int)shared_size);
  printf("  JOBDESC       = off=$%08X size=$%08X\n", (unsigned int)jobdesc_offset,
         (unsigned int)jobdesc_size);
  printf("  HEARTBEAT     = %u\n", (unsigned int)heartbeat);

  printf("Shared info @ +$%08X:\n", (unsigned int)ARM64_ACCEL_SHARED_INFO_OFFSET);
  printf("  SIG           = $%08X\n", (unsigned int)shared_sig);
  printf("  ABI_VERSION   = %u\n", (unsigned int)shared_abi);
  printf("  MB_OFFSET     = $%08X\n", (unsigned int)shared_mb_offset);
  printf("  MB_SIZE       = $%08X\n", (unsigned int)shared_mb_size);
  printf("  JOBDESC_OFF   = $%08X\n", (unsigned int)shared_jobdesc_offset);
  printf("  JOBDESC_SIZE  = $%08X\n", (unsigned int)shared_jobdesc_size);
  printf("  FEATURES      = $%08X (MAILBOX=%u IRQ=%u SHARED=%u)\n",
         (unsigned int)shared_features,
         (unsigned int)((shared_features & ARM64_ACCEL_SHARED_INFO_FEAT_MAILBOX) != 0u),
         (unsigned int)((shared_features & ARM64_ACCEL_SHARED_INFO_FEAT_IRQ) != 0u),
         (unsigned int)((shared_features & ARM64_ACCEL_SHARED_INFO_FEAT_SHARED_RAM) != 0u));

  printf("Mailbox @ +$%08X:\n", (unsigned int)ARM64_ACCEL_MAILBOX_OFFSET);
  printf("  MAGIC         = $%08X\n", (unsigned int)mb_magic);
  printf("  VERSION       = %u\n", (unsigned int)mb_version);
  printf("  SEQ/ACK       = %u/%u\n", (unsigned int)mb_seq, (unsigned int)mb_ack);
  printf("  CMD/STATUS    = %u/%u\n", (unsigned int)mb_cmd, (unsigned int)mb_status);
  return 0;
}

ULONG armaccel_device_ping(struct armaccel_board *board) {
  if ((board == NULL) || (board->base == NULL)) {
    return ARMACCEL_DEVICE_ERROR_RESULT;
  }
  return do_mailbox_roundtrip(board->base, ARM64_ACCEL_MB_CMD_PING, 0u, 0u, 0u, 0u,
                              ARMACCEL_DEVICE_WAIT_SPINS_CMD, NULL, NULL, NULL, NULL, NULL);
}

int armaccel_device_irq_test(struct armaccel_board *board) {
  ULONG control;
  ULONG irq_status;
  ULONG ping;

  if ((board == NULL) || (board->base == NULL)) {
    return 1;
  }

  control = read_be32(board->base, ARM64_ACCEL_REG_CONTROL);
  control |= ARM64_ACCEL_CTRL_START | ARM64_ACCEL_CTRL_IRQ_ENABLE;
  write_be32(board->base, ARM64_ACCEL_REG_CONTROL, control);

  write_be32(board->base, ARM64_ACCEL_REG_IRQ_ACK,
             ARM64_ACCEL_IRQ_JOB_DONE | ARM64_ACCEL_IRQ_HOST_EVENT);
  irq_status = read_be32(board->base, ARM64_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & (ARM64_ACCEL_IRQ_JOB_DONE | ARM64_ACCEL_IRQ_HOST_EVENT)) != 0u) {
    printf("IRQ clear failed, status=$%08X.\n", (unsigned int)irq_status);
    return 2;
  }

  ping = armaccel_device_ping(board);
  if (ping == ARMACCEL_DEVICE_BUSY_RESULT) {
    return 3;
  }
  if (ping == ARMACCEL_DEVICE_ERROR_RESULT) {
    return 4;
  }

  irq_status = read_be32(board->base, ARM64_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & ARM64_ACCEL_IRQ_JOB_DONE) == 0u) {
    printf("JOB_DONE IRQ not raised, status=$%08X.\n", (unsigned int)irq_status);
    return 5;
  }
  write_be32(board->base, ARM64_ACCEL_REG_IRQ_ACK, ARM64_ACCEL_IRQ_JOB_DONE);
  irq_status = read_be32(board->base, ARM64_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & ARM64_ACCEL_IRQ_JOB_DONE) != 0u) {
    printf("JOB_DONE IRQ not cleared, status=$%08X.\n", (unsigned int)irq_status);
    return 6;
  }
  printf("IRQ test OK: job-done raise/ack.\n");
  return 0;
}

ULONG armaccel_device_max_payload_size(void) {
  return ARM64_ACCEL_Z2_SIZE - ARMACCEL_ELF_IMAGE_OFFSET;
}

int armaccel_device_load_elf(struct armaccel_board *board, const char *path, ULONG *out_elf_size) {
  FILE *fp;
  UBYTE *elf_image;
  long file_size_long;
  ULONG file_size;
  ULONG max_size;

  if ((board == NULL) || (board->base == NULL) || (path == NULL)) {
    return 1;
  }

  fp = fopen(path, "rb");
  if (fp == NULL) {
    printf("ELF open failed: %s (%s)\n", path, strerror(errno));
    return 2;
  }
  if (fseek(fp, 0L, SEEK_END) != 0) {
    fclose(fp);
    printf("ELF read failed: seek end\n");
    return 3;
  }
  file_size_long = ftell(fp);
  if (file_size_long <= 0L) {
    fclose(fp);
    printf("ELF read failed: invalid size\n");
    return 4;
  }
  if (fseek(fp, 0L, SEEK_SET) != 0) {
    fclose(fp);
    printf("ELF read failed: seek start\n");
    return 5;
  }

  file_size = (ULONG)file_size_long;
  max_size = armaccel_device_max_payload_size();
  if (file_size > max_size) {
    fclose(fp);
    printf("ELF too large for current transport window: %lu bytes (max %lu)\n",
           (unsigned long)file_size, (unsigned long)max_size);
    return 6;
  }

  elf_image = (UBYTE *)malloc(file_size);
  if (elf_image == NULL) {
    fclose(fp);
    printf("ELF read failed: out of memory\n");
    return 7;
  }
  if (fread(elf_image, 1u, file_size, fp) != file_size) {
    free(elf_image);
    fclose(fp);
    printf("ELF read failed: short read\n");
    return 8;
  }
  fclose(fp);

  write_bytes(board->base, ARMACCEL_ELF_IMAGE_OFFSET, elf_image, file_size);
  free(elf_image);

  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_MAGIC,
             ARM64_ACCEL_JOBDESC_MAGIC);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_VERSION,
             ARM64_ACCEL_JOBDESC_VERSION);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_STATE,
             ARM64_ACCEL_JOBDESC_STATE_IDLE);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_FLAGS, 0u);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_ELF_OFFSET,
             ARMACCEL_ELF_IMAGE_OFFSET);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_ELF_SIZE, file_size);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_ENTRY_ARG,
             ARM64_ACCEL_JOBDESC_OFFSET);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_LO, 0u);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_HI, 0u);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RESULT,
             ARM64_ACCEL_JOBDESC_RESULT_OK);
  clear_jobdesc_extension(board->base);

  if (out_elf_size != NULL) {
    *out_elf_size = file_size;
  }
  return 0;
}

int armaccel_device_run_elf_job(struct armaccel_board *board, ULONG *out_result0, ULONG *out_result1,
                                   ULONG *out_job_state, ULONG *out_job_result, ULONG *out_ret_lo,
                                   ULONG *out_ret_hi, ArmAccelServiceHook service_hook,
                                   APTR service_ctx, ULONG *out_service_dispatch_count,
                                   ULONG *out_service_hook_status) {
  ULONG result0;
  ULONG result1;
  ULONG job_state;
  ULONG job_result;
  ULONG job_ret_lo;
  ULONG job_ret_hi;
  ULONG attempt;

  if ((board == NULL) || (board->base == NULL)) {
    return 1;
  }

  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_STATE,
             ARM64_ACCEL_JOBDESC_STATE_QUEUED);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RESULT,
             ARM64_ACCEL_JOBDESC_RESULT_OK);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_LO, 0u);
  write_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_HI, 0u);

  result0 = ARMACCEL_DEVICE_BUSY_RESULT;
  result1 = 0u;
  for (attempt = 0u; attempt < ARMACCEL_RUN_ELF_BUSY_RETRIES; attempt++) {
    result0 = do_mailbox_roundtrip(board->base, ARM64_ACCEL_MB_CMD_RUN_ELF, 0u, 0u, 0u, 0u,
                                   ARMACCEL_DEVICE_WAIT_SPINS_RUN_ELF, &result1, service_hook,
                                   service_ctx, out_service_dispatch_count, out_service_hook_status);
    if (result0 != ARMACCEL_DEVICE_BUSY_RESULT) {
      break;
    }
    {
      volatile ULONG spin;
      for (spin = 0u; spin < ARMACCEL_RUN_ELF_BUSY_BACKOFF_SPINS; spin++) {
      }
    }
  }
  if (result0 == ARMACCEL_DEVICE_BUSY_RESULT) {
    return 2;
  }
  if (result0 == ARMACCEL_DEVICE_ERROR_RESULT) {
    return 3;
  }

  job_state = read_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_STATE);
  job_result = read_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RESULT);
  job_ret_lo = read_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_LO);
  job_ret_hi = read_be32(board->base, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_HI);

  if (out_result0 != NULL) {
    *out_result0 = result0;
  }
  if (out_result1 != NULL) {
    *out_result1 = result1;
  }
  if (out_job_state != NULL) {
    *out_job_state = job_state;
  }
  if (out_job_result != NULL) {
    *out_job_result = job_result;
  }
  if (out_ret_lo != NULL) {
    *out_ret_lo = job_ret_lo;
  }
  if (out_ret_hi != NULL) {
    *out_ret_hi = job_ret_hi;
  }

  if (result0 != ARM64_ACCEL_JOBDESC_RESULT_OK) {
    return 4;
  }
  if ((job_state != ARM64_ACCEL_JOBDESC_STATE_DONE) ||
      (job_result != ARM64_ACCEL_JOBDESC_RESULT_OK)) {
    return 5;
  }
  return 0;
}
