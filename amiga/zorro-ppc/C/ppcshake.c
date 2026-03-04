// SPDX-License-Identifier: MIT
#include <exec/types.h>
#include <libraries/expansionbase.h>
#include <proto/exec.h>
#include <proto/expansion.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppc_accel_regs.h"

#define PPC_ACCEL_MANUFACTURER PPC_ACCEL_MANUFACTURER_ID
#define PPC_ACCEL_PRODUCT PPC_ACCEL_PRODUCT_ID

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
  ULONG seq;
  ULONG timeout;
  ULONG ack;
  ULONG status;

  mailbox = board + read_be32(board, PPC_ACCEL_REG_MAILBOX_OFFSET);
  if (read_be32(mailbox, PPC_ACCEL_MB_OFF_MAGIC) != PPC_ACCEL_MB_MAGIC) {
    printf("Mailbox magic mismatch.\n");
    return 0xFFFFFFFFu;
  }

  seq = read_be32(mailbox, PPC_ACCEL_MB_OFF_SEQ) + 1u;
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

  timeout = 2000000u;
  while (timeout > 0u) {
    ack = read_be32(mailbox, PPC_ACCEL_MB_OFF_ACK_SEQ);
    status = read_be32(mailbox, PPC_ACCEL_MB_OFF_STATUS);
    if ((ack == seq) && ((status == PPC_ACCEL_MB_STATUS_DONE) || (status == PPC_ACCEL_MB_STATUS_ERR))) {
      break;
    }
    timeout--;
  }

  if (timeout == 0u) {
    printf("Mailbox timeout waiting for seq %u.\n", (unsigned int)seq);
    return 0xFFFFFFFFu;
  }

  status = read_be32(mailbox, PPC_ACCEL_MB_OFF_STATUS);
  if (status != PPC_ACCEL_MB_STATUS_DONE) {
    printf("Mailbox command failed, status=%u.\n", (unsigned int)status);
    return 0xFFFFFFFFu;
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
  if (t32 == 0xFFFFFFFFu) {
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
  volatile UBYTE *board;
  ULONG loops;
  ULONG i;
  int do_irq_test;

  loops = 1u;
  do_irq_test = 0;
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
    return 1;
  }

  cd = (struct ConfigDev *)FindConfigDev(NULL, PPC_ACCEL_MANUFACTURER, PPC_ACCEL_PRODUCT);
  if (cd == NULL) {
    printf("PPC accel device not found (manuf=$%04X product=$%04X).\n",
           PPC_ACCEL_MANUFACTURER, PPC_ACCEL_PRODUCT);
    CloseLibrary((struct Library *)ExpansionBase);
    return 2;
  }

  board = (volatile UBYTE *)cd->cd_BoardAddr;
  printf("PPC accel found at $%08X\n", (unsigned int)(ULONG)board);

  if (read_be32(board, PPC_ACCEL_REG_MAGIC) != PPC_ACCEL_MAGIC) {
    printf("Register magic mismatch.\n");
    CloseLibrary((struct Library *)ExpansionBase);
    return 3;
  }

  if (read_be32(board, PPC_ACCEL_REG_ABI_VERSION) != PPC_ACCEL_ABI_VERSION) {
    printf("Register ABI version mismatch.\n");
    CloseLibrary((struct Library *)ExpansionBase);
    return 6;
  }

  write_be32(board, PPC_ACCEL_REG_CONTROL, read_be32(board, PPC_ACCEL_REG_CONTROL) | PPC_ACCEL_CTRL_START);
  if ((read_be32(board, PPC_ACCEL_REG_STATUS) & PPC_ACCEL_STATUS_RUNNING) == 0u) {
    printf("PPC accel did not enter running state.\n");
    CloseLibrary((struct Library *)ExpansionBase);
    return 4;
  }

  for (i = 0u; i < loops; i++) {
    ULONG t32;

    t32 = do_time32_roundtrip(board);
    if (t32 == 0xFFFFFFFFu) {
      CloseLibrary((struct Library *)ExpansionBase);
      return 5;
    }
    printf("TIME32[%u] = $%08X (%u)\n", (unsigned int)i, (unsigned int)t32, (unsigned int)t32);
  }

  if (do_irq_test != 0) {
    int irq_rc;

    irq_rc = do_irq_semantics_test(board);
    if (irq_rc != 0) {
      CloseLibrary((struct Library *)ExpansionBase);
      return 10 + irq_rc;
    }
  }

  CloseLibrary((struct Library *)ExpansionBase);
  return 0;
}
