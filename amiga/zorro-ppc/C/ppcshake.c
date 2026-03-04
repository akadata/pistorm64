// SPDX-License-Identifier: MIT
#include <exec/types.h>
#include <libraries/expansionbase.h>
#include <proto/exec.h>
#include <proto/expansion.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppc_accel_regs.h"

#define PPC_ACCEL_MANUFACTURER PPC_ACCEL_MANUFACTURER_ID
#define PPC_ACCEL_PRODUCT PPC_ACCEL_PRODUCT_ID

#define PPCSHAKE_WAIT_SPINS 200000u
#define PPCSHAKE_BUSY_SETTLE_SPINS 50000u
#define PPCSHAKE_BUSY_RESULT 0xFFFFFFFEu
#define PPCSHAKE_ERROR_RESULT 0xFFFFFFFFu
#define PPCSHAKE_LOCK_PATH "T:ppcshake.lock"

static BPTR ppcshake_acquire_instance_lock(void) {
  BPTR fh;
  BPTR lock;

  fh = Open((STRPTR)PPCSHAKE_LOCK_PATH, MODE_READWRITE);
  if (fh == (BPTR)0) {
    fh = Open((STRPTR)PPCSHAKE_LOCK_PATH, MODE_NEWFILE);
  }
  if (fh != (BPTR)0) {
    Close(fh);
  }

  lock = Lock((STRPTR)PPCSHAKE_LOCK_PATH, EXCLUSIVE_LOCK);
  return lock;
}

static ULONG read_be32(volatile UBYTE *base, ULONG offset) {
  ULONG b0;
  ULONG b1;
  ULONG b2;
  ULONG b3;

  b0 = (ULONG)base[offset + 0];
  b1 = (ULONG)base[offset + 1];
  b2 = (ULONG)base[offset + 2];
  b3 = (ULONG)base[offset + 3];
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

static void write_be32(volatile UBYTE *base, ULONG offset, ULONG value) {
  base[offset + 0] = (UBYTE)((value >> 24) & 0xFFu);
  base[offset + 1] = (UBYTE)((value >> 16) & 0xFFu);
  base[offset + 2] = (UBYTE)((value >> 8) & 0xFFu);
  base[offset + 3] = (UBYTE)(value & 0xFFu);
}

static ULONG do_time32_roundtrip(volatile UBYTE *board) {
  volatile UBYTE *mailbox;
  ULONG current_seq;
  ULONG current_ack;
  ULONG current_status;
  ULONG published_seq;
  ULONG seq;
  ULONG timeout;
  ULONG ack;
  ULONG status;

  mailbox = board + read_be32(board, PPC_ACCEL_REG_MAILBOX_OFFSET);
  if (read_be32(mailbox, PPC_ACCEL_MB_OFF_MAGIC) != PPC_ACCEL_MB_MAGIC) {
    printf("Mailbox magic mismatch.\n");
    return PPCSHAKE_ERROR_RESULT;
  }

  current_seq = read_be32(mailbox, PPC_ACCEL_MB_OFF_SEQ);
  current_ack = read_be32(mailbox, PPC_ACCEL_MB_OFF_ACK_SEQ);
  current_status = read_be32(mailbox, PPC_ACCEL_MB_OFF_STATUS);
  if (current_seq != current_ack) {
    ULONG settle;

    settle = PPCSHAKE_BUSY_SETTLE_SPINS;
    while (settle > 0u) {
      current_seq = read_be32(mailbox, PPC_ACCEL_MB_OFF_SEQ);
      current_ack = read_be32(mailbox, PPC_ACCEL_MB_OFF_ACK_SEQ);
      current_status = read_be32(mailbox, PPC_ACCEL_MB_OFF_STATUS);
      if (current_seq == current_ack) {
        break;
      }
      settle--;
    }
  }
  if (current_seq != current_ack) {
    printf("Mailbox busy (seq=%u ack=%u status=%u).\n",
           (unsigned int)current_seq,
           (unsigned int)current_ack,
           (unsigned int)current_status);
    printf("Try again.\n");
    return PPCSHAKE_BUSY_RESULT;
  }

  seq = current_seq + 1u;
  if (seq == 0u) {
    seq = 1u;
  }

  write_be32(mailbox, PPC_ACCEL_MB_OFF_CMD, PPC_ACCEL_MB_CMD_HOST_TIME32);
  write_be32(mailbox, PPC_ACCEL_MB_OFF_ARG0, 0u);
  write_be32(mailbox, PPC_ACCEL_MB_OFF_ARG1, 0u);
  write_be32(mailbox, PPC_ACCEL_MB_OFF_RESULT0, 0u);
  write_be32(mailbox, PPC_ACCEL_MB_OFF_RESULT1, 0u);
  write_be32(mailbox, PPC_ACCEL_MB_OFF_STATUS, PPC_ACCEL_MB_STATUS_IDLE);
  write_be32(mailbox, PPC_ACCEL_MB_OFF_SEQ, seq);

  published_seq = read_be32(mailbox, PPC_ACCEL_MB_OFF_SEQ);
  if (published_seq != seq) {
    printf("Mailbox busy (seq publish rejected: wanted=%u got=%u ack=%u status=%u).\n",
           (unsigned int)seq,
           (unsigned int)published_seq,
           (unsigned int)read_be32(mailbox, PPC_ACCEL_MB_OFF_ACK_SEQ),
           (unsigned int)read_be32(mailbox, PPC_ACCEL_MB_OFF_STATUS));
    return PPCSHAKE_BUSY_RESULT;
  }

  timeout = PPCSHAKE_WAIT_SPINS;
  while (timeout > 0u) {
    ULONG seen_seq;

    seen_seq = read_be32(mailbox, PPC_ACCEL_MB_OFF_SEQ);
    if (seen_seq != seq) {
      printf("Mailbox busy (seq ownership lost: wanted=%u saw=%u).\n",
             (unsigned int)seq,
             (unsigned int)seen_seq);
      return PPCSHAKE_BUSY_RESULT;
    }
    ack = read_be32(mailbox, PPC_ACCEL_MB_OFF_ACK_SEQ);
    status = read_be32(mailbox, PPC_ACCEL_MB_OFF_STATUS);
    if ((ack == seq) && ((status == PPC_ACCEL_MB_STATUS_DONE) || (status == PPC_ACCEL_MB_STATUS_ERR))) {
      break;
    }
    timeout--;
  }

  if (timeout == 0u) {
    printf("Mailbox timeout waiting for seq %u.\n", (unsigned int)seq);
    return PPCSHAKE_ERROR_RESULT;
  }

  status = read_be32(mailbox, PPC_ACCEL_MB_OFF_STATUS);
  if (status != PPC_ACCEL_MB_STATUS_DONE) {
    printf("Mailbox command failed, status=%u.\n", (unsigned int)status);
    return PPCSHAKE_ERROR_RESULT;
  }
  return read_be32(mailbox, PPC_ACCEL_MB_OFF_RESULT0);
}

static int do_irq_semantics_test(volatile UBYTE *board) {
  ULONG control;
  ULONG irq_status;
  ULONG t32;

  control = read_be32(board, PPC_ACCEL_REG_CONTROL);
  control |= PPC_ACCEL_CTRL_START | PPC_ACCEL_CTRL_IRQ_ENABLE;
  write_be32(board, PPC_ACCEL_REG_CONTROL, control);

  write_be32(board, PPC_ACCEL_REG_IRQ_ACK, PPC_ACCEL_IRQ_CMD_DONE | PPC_ACCEL_IRQ_HOST_DOORBELL);
  irq_status = read_be32(board, PPC_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & (PPC_ACCEL_IRQ_CMD_DONE | PPC_ACCEL_IRQ_HOST_DOORBELL)) != 0u) {
    printf("IRQ clear failed, status=$%08X.\n", (unsigned int)irq_status);
    return 1;
  }

  write_be32(board, PPC_ACCEL_REG_DOORBELL, 1u);
  irq_status = read_be32(board, PPC_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & PPC_ACCEL_IRQ_HOST_DOORBELL) == 0u) {
    printf("Doorbell IRQ not raised, status=$%08X.\n", (unsigned int)irq_status);
    return 2;
  }
  write_be32(board, PPC_ACCEL_REG_IRQ_ACK, PPC_ACCEL_IRQ_HOST_DOORBELL);
  irq_status = read_be32(board, PPC_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & PPC_ACCEL_IRQ_HOST_DOORBELL) != 0u) {
    printf("Doorbell IRQ not cleared, status=$%08X.\n", (unsigned int)irq_status);
    return 3;
  }

  t32 = do_time32_roundtrip(board);
  if (t32 == PPCSHAKE_BUSY_RESULT) {
    return 7;
  }
  if (t32 == PPCSHAKE_ERROR_RESULT) {
    return 4;
  }

  irq_status = read_be32(board, PPC_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & PPC_ACCEL_IRQ_CMD_DONE) == 0u) {
    printf("CMD_DONE IRQ not raised, status=$%08X.\n", (unsigned int)irq_status);
    return 5;
  }
  write_be32(board, PPC_ACCEL_REG_IRQ_ACK, PPC_ACCEL_IRQ_CMD_DONE);
  irq_status = read_be32(board, PPC_ACCEL_REG_IRQ_STATUS);
  if ((irq_status & PPC_ACCEL_IRQ_CMD_DONE) != 0u) {
    printf("CMD_DONE IRQ not cleared, status=$%08X.\n", (unsigned int)irq_status);
    return 6;
  }

  printf("IRQ test OK: doorbell raise/ack and cmd_done raise/ack.\n");
  return 0;
}

int main(int argc, char **argv) {
  struct ExpansionBase *ExpansionBase;
  struct ConfigDev *cd;
  BPTR instance_lock;
  volatile UBYTE *board;
  ULONG loops;
  ULONG i;
  int do_irq_test;
  int exit_code;

  loops = 1u;
  do_irq_test = 0;
  exit_code = 0;
  instance_lock = (BPTR)0;
  ExpansionBase = NULL;
  for (i = 1u; i < (ULONG)argc; i++) {
    if ((strcmp(argv[i], "--irq") == 0) || (strcmp(argv[i], "-i") == 0)) {
      do_irq_test = 1;
    } else {
      loops = (ULONG)strtoul(argv[i], NULL, 0);
      if (loops == 0u) {
        loops = 1u;
      }
    }
  }

  ExpansionBase = (struct ExpansionBase *)OpenLibrary((STRPTR)"expansion.library", 0L);
  if (ExpansionBase == NULL) {
    printf("Failed to open expansion.library.\n");
    exit_code = 1;
    goto cleanup;
  }

  instance_lock = ppcshake_acquire_instance_lock();
  if (instance_lock == (BPTR)0) {
    printf("ppcshake busy: another instance is running. Try again.\n");
    exit_code = 8;
    goto cleanup;
  }

  cd = (struct ConfigDev *)FindConfigDev(NULL, PPC_ACCEL_MANUFACTURER, PPC_ACCEL_PRODUCT);
  if (cd == NULL) {
    printf("PPC accel device not found (manuf=$%04X product=$%04X).\n",
           PPC_ACCEL_MANUFACTURER, PPC_ACCEL_PRODUCT);
    exit_code = 2;
    goto cleanup;
  }

  board = (volatile UBYTE *)cd->cd_BoardAddr;
  printf("PPC accel found at $%08X\n", (unsigned int)(ULONG)board);

  if (read_be32(board, PPC_ACCEL_REG_MAGIC) != PPC_ACCEL_MAGIC) {
    printf("Register magic mismatch.\n");
    exit_code = 3;
    goto cleanup;
  }

  if (read_be32(board, PPC_ACCEL_REG_ABI_VERSION) != PPC_ACCEL_ABI_VERSION) {
    printf("Register ABI version mismatch.\n");
    exit_code = 6;
    goto cleanup;
  }

  write_be32(board, PPC_ACCEL_REG_CONTROL, read_be32(board, PPC_ACCEL_REG_CONTROL) | PPC_ACCEL_CTRL_START);
  if ((read_be32(board, PPC_ACCEL_REG_STATUS) & PPC_ACCEL_STATUS_RUNNING) == 0u) {
    printf("PPC accel did not enter running state.\n");
    exit_code = 4;
    goto cleanup;
  }

  for (i = 0u; i < loops; i++) {
    ULONG t32;

    t32 = do_time32_roundtrip(board);
    if (t32 == PPCSHAKE_BUSY_RESULT) {
      exit_code = 7;
      goto cleanup;
    }
    if (t32 == PPCSHAKE_ERROR_RESULT) {
      exit_code = 5;
      goto cleanup;
    }
    printf("TIME32[%u] = $%08X (%u)\n", (unsigned int)i, (unsigned int)t32, (unsigned int)t32);
  }

  if (do_irq_test != 0) {
    int irq_rc;

    irq_rc = do_irq_semantics_test(board);
    if (irq_rc != 0) {
      exit_code = 10 + irq_rc;
      goto cleanup;
    }
  }

cleanup:
  if (instance_lock != (BPTR)0) {
    UnLock(instance_lock);
  }
  if (ExpansionBase != NULL) {
    CloseLibrary((struct Library *)ExpansionBase);
  }
  return exit_code;
}
