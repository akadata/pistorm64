// SPDX-License-Identifier: MIT
#include <exec/types.h>
#include <libraries/expansionbase.h>
#include <proto/exec.h>
#include <proto/expansion.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm64_accel_regs.h"

#define ARMSHAKE_WAIT_SPINS_CMD 200000u
#define ARMSHAKE_WAIT_SPINS_RUN_ELF 200000000u
#define ARMSHAKE_BUSY_SETTLE_SPINS 50000u
#define ARMSHAKE_BUSY_RESULT 0xFFFFFFFEu
#define ARMSHAKE_ERROR_RESULT 0xFFFFFFFFu
#define ARMSHAKE_RUN_ELF_BUSY_RETRIES 64u
#define ARMSHAKE_RUN_ELF_BUSY_BACKOFF_SPINS 20000u
#define ARMSHAKE_ELF_IMAGE_OFFSET (ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_SIZE)

static void print_usage(const char *prog) {
  printf("Usage: %s [options] [loops]\n", prog);
  printf("Options:\n");
  printf("  --id           Dump board identity/registers only\n");
  printf("  --ping         Run mailbox ping test (default)\n");
  printf("  --irq          Run IRQ semantics test on ping completion\n");
  printf("  --elf <path>   Load AArch64 ELF64 into shared window and execute\n");
}

static ULONG read_be32(volatile UBYTE *base, ULONG offset) {
  ULONG b0 = (ULONG)base[offset + 0];
  ULONG b1 = (ULONG)base[offset + 1];
  ULONG b2 = (ULONG)base[offset + 2];
  ULONG b3 = (ULONG)base[offset + 3];
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

static void write_be32(volatile UBYTE *base, ULONG offset, ULONG value) {
  base[offset + 0] = (UBYTE)((value >> 24) & 0xFFu);
  base[offset + 1] = (UBYTE)((value >> 16) & 0xFFu);
  base[offset + 2] = (UBYTE)((value >> 8) & 0xFFu);
  base[offset + 3] = (UBYTE)(value & 0xFFu);
}

static void write_bytes(volatile UBYTE *base, ULONG offset, const UBYTE *src, ULONG len) {
  ULONG i;
  for (i = 0u; i < len; i++) {
    base[offset + i] = src[i];
  }
}

static void ensure_started(volatile UBYTE *board) {
  ULONG control = read_be32(board, ARM64_ACCEL_REG_CONTROL);
  if ((control & ARM64_ACCEL_CTRL_START) == 0u) {
    control |= ARM64_ACCEL_CTRL_START;
    write_be32(board, ARM64_ACCEL_REG_CONTROL, control);
  }
}

static ULONG do_mailbox_roundtrip(volatile UBYTE *board, ULONG cmd, ULONG arg0, ULONG arg1,
                                  ULONG arg2, ULONG arg3, ULONG wait_spins,
                                  ULONG *out_result1) {
  volatile UBYTE *mailbox;
  ULONG current_seq;
  ULONG current_ack;
  ULONG current_status;
  ULONG published_seq;
  ULONG seq;
  ULONG timeout;
  ULONG ack;
  ULONG status;

  ensure_started(board);
  mailbox = board + read_be32(board, ARM64_ACCEL_REG_MAILBOX_OFFSET);
  if (read_be32(mailbox, ARM64_ACCEL_MB_OFF_MAGIC) != ARM64_ACCEL_MB_MAGIC) {
    printf("Mailbox magic mismatch.\n");
    return ARMSHAKE_ERROR_RESULT;
  }

  current_seq = read_be32(mailbox, ARM64_ACCEL_MB_OFF_SEQ);
  current_ack = read_be32(mailbox, ARM64_ACCEL_MB_OFF_ACK_SEQ);
  current_status = read_be32(mailbox, ARM64_ACCEL_MB_OFF_STATUS);
  if (current_seq != current_ack) {
    ULONG settle = ARMSHAKE_BUSY_SETTLE_SPINS;
    while (settle > 0u) {
      current_seq = read_be32(mailbox, ARM64_ACCEL_MB_OFF_SEQ);
      current_ack = read_be32(mailbox, ARM64_ACCEL_MB_OFF_ACK_SEQ);
      current_status = read_be32(mailbox, ARM64_ACCEL_MB_OFF_STATUS);
      if (current_seq == current_ack) {
        break;
      }
      settle--;
    }
  }
  if (current_seq != current_ack) {
    printf("Mailbox busy (seq=%u ack=%u status=%u).\n", (unsigned int)current_seq,
           (unsigned int)current_ack, (unsigned int)current_status);
    return ARMSHAKE_BUSY_RESULT;
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
    printf("Mailbox busy (seq publish rejected: wanted=%u got=%u ack=%u status=%u).\n",
           (unsigned int)seq, (unsigned int)published_seq,
           (unsigned int)read_be32(mailbox, ARM64_ACCEL_MB_OFF_ACK_SEQ),
           (unsigned int)read_be32(mailbox, ARM64_ACCEL_MB_OFF_STATUS));
    return ARMSHAKE_BUSY_RESULT;
  }

  timeout = wait_spins;
  while (timeout > 0u) {
    ULONG seen_seq = read_be32(mailbox, ARM64_ACCEL_MB_OFF_SEQ);
    if (seen_seq != seq) {
      printf("Mailbox busy (seq ownership lost: wanted=%u saw=%u).\n", (unsigned int)seq,
             (unsigned int)seen_seq);
      return ARMSHAKE_BUSY_RESULT;
    }
    ack = read_be32(mailbox, ARM64_ACCEL_MB_OFF_ACK_SEQ);
    status = read_be32(mailbox, ARM64_ACCEL_MB_OFF_STATUS);
    if ((ack == seq) &&
        ((status == ARM64_ACCEL_MB_STATUS_DONE) || (status == ARM64_ACCEL_MB_STATUS_ERR))) {
      break;
    }
    timeout--;
  }
  if (timeout == 0u) {
    printf("Mailbox timeout waiting for seq %u.\n", (unsigned int)seq);
    return ARMSHAKE_ERROR_RESULT;
  }

  status = read_be32(mailbox, ARM64_ACCEL_MB_OFF_STATUS);
  if (status != ARM64_ACCEL_MB_STATUS_DONE) {
    printf("Mailbox command failed, status=%u.\n", (unsigned int)status);
    return ARMSHAKE_ERROR_RESULT;
  }
  if (out_result1 != NULL) {
    *out_result1 = read_be32(mailbox, ARM64_ACCEL_MB_OFF_RESULT1);
  }
  return read_be32(mailbox, ARM64_ACCEL_MB_OFF_RESULT0);
}

static ULONG do_ping_roundtrip(volatile UBYTE *board) {
  return do_mailbox_roundtrip(board, ARM64_ACCEL_MB_CMD_PING, 0u, 0u, 0u, 0u,
                              ARMSHAKE_WAIT_SPINS_CMD, NULL);
}

static int dump_board_identity(volatile UBYTE *board) {
  ULONG magic = read_be32(board, ARM64_ACCEL_REG_MAGIC);
  ULONG abi_version = read_be32(board, ARM64_ACCEL_REG_ABI_VERSION);
  ULONG control = read_be32(board, ARM64_ACCEL_REG_CONTROL);
  ULONG status = read_be32(board, ARM64_ACCEL_REG_STATUS);
  ULONG irq_status = read_be32(board, ARM64_ACCEL_REG_IRQ_STATUS);
  ULONG mb_offset = read_be32(board, ARM64_ACCEL_REG_MAILBOX_OFFSET);
  ULONG mb_size = read_be32(board, ARM64_ACCEL_REG_MAILBOX_SIZE);
  ULONG shared_offset = read_be32(board, ARM64_ACCEL_REG_SHARED_OFFSET);
  ULONG shared_size = read_be32(board, ARM64_ACCEL_REG_SHARED_SIZE);
  ULONG jobdesc_offset = read_be32(board, ARM64_ACCEL_REG_JOBDESC_OFFSET);
  ULONG jobdesc_size = read_be32(board, ARM64_ACCEL_REG_JOBDESC_SIZE);
  ULONG heartbeat = read_be32(board, ARM64_ACCEL_REG_HEARTBEAT);

  ULONG shared_sig =
      read_be32(board, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_SIGNATURE);
  ULONG shared_abi =
      read_be32(board, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_ABI_VERSION);
  ULONG shared_mb_offset =
      read_be32(board, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_MB_OFFSET);
  ULONG shared_mb_size =
      read_be32(board, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_MB_SIZE);
  ULONG shared_jobdesc_offset = read_be32(
      board, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_JOBDESC_OFF);
  ULONG shared_jobdesc_size = read_be32(
      board, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_JOBDESC_SIZE);
  ULONG shared_features =
      read_be32(board, ARM64_ACCEL_SHARED_INFO_OFFSET + ARM64_ACCEL_SHARED_INFO_OFF_FEATURES);

  ULONG mb_magic = read_be32(board, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_MAGIC);
  ULONG mb_version = read_be32(board, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_VERSION);
  ULONG mb_seq = read_be32(board, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_SEQ);
  ULONG mb_ack = read_be32(board, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_ACK_SEQ);
  ULONG mb_cmd = read_be32(board, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_CMD);
  ULONG mb_status = read_be32(board, ARM64_ACCEL_MAILBOX_OFFSET + ARM64_ACCEL_MB_OFF_STATUS);

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

static int do_irq_test(volatile UBYTE *board) {
  ULONG control;
  ULONG irq_status;
  ULONG ping;

  control = read_be32(board, ARM64_ACCEL_REG_CONTROL);
  control |= ARM64_ACCEL_CTRL_START | ARM64_ACCEL_CTRL_IRQ_ENABLE;
  write_be32(board, ARM64_ACCEL_REG_CONTROL, control);

  write_be32(board, ARM64_ACCEL_REG_IRQ_ACK, ARM64_ACCEL_IRQ_JOB_DONE | ARM64_ACCEL_IRQ_HOST_EVENT);
  irq_status = read_be32(board, ARM64_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & (ARM64_ACCEL_IRQ_JOB_DONE | ARM64_ACCEL_IRQ_HOST_EVENT)) != 0u) {
    printf("IRQ clear failed, status=$%08X.\n", (unsigned int)irq_status);
    return 1;
  }

  ping = do_ping_roundtrip(board);
  if (ping == ARMSHAKE_BUSY_RESULT) {
    return 2;
  }
  if (ping == ARMSHAKE_ERROR_RESULT) {
    return 3;
  }

  irq_status = read_be32(board, ARM64_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & ARM64_ACCEL_IRQ_JOB_DONE) == 0u) {
    printf("JOB_DONE IRQ not raised, status=$%08X.\n", (unsigned int)irq_status);
    return 4;
  }
  write_be32(board, ARM64_ACCEL_REG_IRQ_ACK, ARM64_ACCEL_IRQ_JOB_DONE);
  irq_status = read_be32(board, ARM64_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & ARM64_ACCEL_IRQ_JOB_DONE) != 0u) {
    printf("JOB_DONE IRQ not cleared, status=$%08X.\n", (unsigned int)irq_status);
    return 5;
  }
  printf("IRQ test OK: job-done raise/ack.\n");
  return 0;
}

static int load_elf_into_window(volatile UBYTE *board, const char *path, ULONG *out_elf_size) {
  FILE *fp;
  UBYTE *elf_image;
  long file_size_long;
  ULONG file_size;
  ULONG max_size;

  fp = fopen(path, "rb");
  if (fp == NULL) {
    printf("ELF open failed: %s (%s)\n", path, strerror(errno));
    return 1;
  }
  if (fseek(fp, 0L, SEEK_END) != 0) {
    fclose(fp);
    printf("ELF read failed: seek end\n");
    return 1;
  }
  file_size_long = ftell(fp);
  if (file_size_long <= 0) {
    fclose(fp);
    printf("ELF read failed: invalid size\n");
    return 1;
  }
  if (fseek(fp, 0L, SEEK_SET) != 0) {
    fclose(fp);
    printf("ELF read failed: seek start\n");
    return 1;
  }

  file_size = (ULONG)file_size_long;
  max_size = ARM64_ACCEL_Z2_SIZE - ARMSHAKE_ELF_IMAGE_OFFSET;
  if (file_size > max_size) {
    fclose(fp);
    printf("ELF too large: %lu bytes (max %lu)\n", (unsigned long)file_size,
           (unsigned long)max_size);
    return 1;
  }

  elf_image = (UBYTE *)malloc(file_size);
  if (elf_image == NULL) {
    fclose(fp);
    printf("ELF read failed: out of memory\n");
    return 1;
  }
  if (fread(elf_image, 1u, file_size, fp) != file_size) {
    free(elf_image);
    fclose(fp);
    printf("ELF read failed: short read\n");
    return 1;
  }
  fclose(fp);

  write_bytes(board, ARMSHAKE_ELF_IMAGE_OFFSET, elf_image, file_size);
  free(elf_image);

  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_MAGIC,
             ARM64_ACCEL_JOBDESC_MAGIC);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_VERSION,
             ARM64_ACCEL_JOBDESC_VERSION);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_STATE,
             ARM64_ACCEL_JOBDESC_STATE_IDLE);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_FLAGS, 0u);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_ELF_OFFSET,
             ARMSHAKE_ELF_IMAGE_OFFSET);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_ELF_SIZE, file_size);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_ENTRY_ARG,
             ARM64_ACCEL_JOBDESC_OFFSET);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_LO, 0u);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_HI, 0u);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RESULT,
             ARM64_ACCEL_JOBDESC_RESULT_OK);

  if (out_elf_size != NULL) {
    *out_elf_size = file_size;
  }
  return 0;
}

static int run_elf_job_once(volatile UBYTE *board) {
  ULONG result0;
  ULONG result1;
  ULONG job_state;
  ULONG job_result;
  ULONG job_ret_lo;
  ULONG job_ret_hi;
  ULONG attempt;

  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_STATE,
             ARM64_ACCEL_JOBDESC_STATE_QUEUED);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RESULT,
             ARM64_ACCEL_JOBDESC_RESULT_OK);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_LO, 0u);
  write_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_HI, 0u);

  result0 = ARMSHAKE_BUSY_RESULT;
  result1 = 0u;
  for (attempt = 0u; attempt < ARMSHAKE_RUN_ELF_BUSY_RETRIES; attempt++) {
    result0 = do_mailbox_roundtrip(board, ARM64_ACCEL_MB_CMD_RUN_ELF, 0u, 0u, 0u, 0u,
                                   ARMSHAKE_WAIT_SPINS_RUN_ELF, &result1);
    if (result0 != ARMSHAKE_BUSY_RESULT) {
      break;
    }
    {
      volatile ULONG spin;
      for (spin = 0u; spin < ARMSHAKE_RUN_ELF_BUSY_BACKOFF_SPINS; spin++) {
      }
    }
  }
  if (result0 == ARMSHAKE_BUSY_RESULT) {
    printf("ELF_RUN busy\n");
    return 2;
  }
  if (result0 == ARMSHAKE_ERROR_RESULT) {
    printf("ELF_RUN mailbox failure\n");
    return 3;
  }

  job_state = read_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_STATE);
  job_result = read_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RESULT);
  job_ret_lo = read_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_LO);
  job_ret_hi = read_be32(board, ARM64_ACCEL_JOBDESC_OFFSET + ARM64_ACCEL_JOBDESC_OFF_RETVAL_HI);

  printf("ELF_RUN result0=%lu result1=$%08lX job_state=%lu job_result=%lu retval=$%08lX%08lX\n",
         (unsigned long)result0, (unsigned long)result1, (unsigned long)job_state,
         (unsigned long)job_result, (unsigned long)job_ret_hi, (unsigned long)job_ret_lo);

  if (result0 != ARM64_ACCEL_JOBDESC_RESULT_OK) {
    return 4;
  }
  if (job_state != ARM64_ACCEL_JOBDESC_STATE_DONE ||
      job_result != ARM64_ACCEL_JOBDESC_RESULT_OK) {
    return 5;
  }
  return 0;
}

static int do_run_elf(volatile UBYTE *board, const char *path) {
  ULONG elf_size;
  if (load_elf_into_window(board, path, &elf_size) != 0) {
    return 1;
  }
  if (run_elf_job_once(board) != 0) {
    return 2;
  }
  return 0;
}

int main(int argc, char **argv) {
  struct ExpansionBase *ExpansionBase;
  struct ConfigDev *cd;
  volatile UBYTE *board;
  ULONG loops;
  ULONG i;
  int do_identity_dump;
  int do_ping;
  int do_irq;
  int do_elf;
  int parse_error;
  int exit_code;
  const char *elf_path;

  ExpansionBase = NULL;
  loops = 1u;
  do_identity_dump = 0;
  do_ping = 1;
  do_irq = 0;
  do_elf = 0;
  parse_error = 0;
  exit_code = 0;
  elf_path = NULL;

  for (i = 1u; i < (ULONG)argc; i++) {
    if ((strcmp(argv[i], "--id") == 0) || (strcmp(argv[i], "--identity") == 0)) {
      do_identity_dump = 1;
      do_ping = 0;
    } else if (strcmp(argv[i], "--ping") == 0) {
      do_ping = 1;
    } else if (strcmp(argv[i], "--irq") == 0) {
      do_irq = 1;
    } else if (strcmp(argv[i], "--elf") == 0) {
      if ((i + 1u) >= (ULONG)argc) {
        printf("Missing argument for --elf\n");
        parse_error = 1;
        break;
      }
      do_elf = 1;
      do_ping = 0;
      i++;
      elf_path = argv[i];
    } else if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-h") == 0)) {
      print_usage(argv[0]);
      return 0;
    } else if (argv[i][0] == '-') {
      printf("Unknown option: %s\n", argv[i]);
      parse_error = 1;
      break;
    } else {
      loops = (ULONG)strtoul(argv[i], NULL, 0);
      if (loops == 0u) {
        loops = 1u;
      }
    }
  }

  if (parse_error != 0) {
    print_usage(argv[0]);
    return 9;
  }

  ExpansionBase = (struct ExpansionBase *)OpenLibrary((STRPTR)"expansion.library", 0L);
  if (ExpansionBase == NULL) {
    printf("Failed to open expansion.library.\n");
    return 1;
  }

  cd = FindConfigDev(NULL, ARM64_ACCEL_MANUFACTURER_ID, ARM64_ACCEL_PRODUCT_ID);
  if (cd == NULL) {
    printf("ARM64 accel board not found (mfr=$%04X prod=$%04X).\n",
           (unsigned int)ARM64_ACCEL_MANUFACTURER_ID, (unsigned int)ARM64_ACCEL_PRODUCT_ID);
    CloseLibrary((struct Library *)ExpansionBase);
    return 2;
  }

  board = (volatile UBYTE *)cd->cd_BoardAddr;
  printf("ARM64 accel found at $%08lX\n", (unsigned long)cd->cd_BoardAddr);

  if (do_identity_dump != 0) {
    exit_code = dump_board_identity(board);
    CloseLibrary((struct Library *)ExpansionBase);
    return exit_code;
  }

  if (do_irq != 0) {
    exit_code = do_irq_test(board);
    CloseLibrary((struct Library *)ExpansionBase);
    return exit_code;
  }

  if (do_elf != 0) {
    exit_code = do_run_elf(board, elf_path);
    CloseLibrary((struct Library *)ExpansionBase);
    return exit_code;
  }

  if (do_ping != 0) {
    for (i = 0u; i < loops; i++) {
      ULONG result = do_ping_roundtrip(board);
      if (result == ARMSHAKE_BUSY_RESULT) {
        printf("PING[%u] busy\n", (unsigned int)i);
        exit_code = 3;
        break;
      }
      if (result == ARMSHAKE_ERROR_RESULT) {
        printf("PING[%u] failed\n", (unsigned int)i);
        exit_code = 4;
        break;
      }
      printf("PING[%u] result=$%08X\n", (unsigned int)i, (unsigned int)result);
    }
  }

  CloseLibrary((struct Library *)ExpansionBase);
  return exit_code;
}
