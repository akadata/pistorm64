// SPDX-License-Identifier: MIT
#include <exec/types.h>
#include <libraries/expansionbase.h>
#include <proto/exec.h>
#include <proto/expansion.h>

#include <stdio.h>
#include <stdlib.h>

#define PPC_ACCEL_MANUFACTURER 0x07DBu
#define PPC_ACCEL_PRODUCT      0x0040u

#define PPC_ACCEL_REG_MAGIC       0x0000u
#define PPC_ACCEL_REG_CONTROL     0x0008u
#define PPC_ACCEL_REG_STATUS      0x000Cu
#define PPC_ACCEL_REG_MB_OFFSET   0x001Cu

#define PPC_ACCEL_MAGIC           0x50504341u
#define PPC_ACCEL_CTRL_START      0x00000001u
#define PPC_ACCEL_STATUS_RUNNING  0x00000001u

#define PPC_ACCEL_MB_OFF_MAGIC    0x0000u
#define PPC_ACCEL_MB_OFF_SEQ      0x0008u
#define PPC_ACCEL_MB_OFF_ACK_SEQ  0x000Cu
#define PPC_ACCEL_MB_OFF_CMD      0x0010u
#define PPC_ACCEL_MB_OFF_STATUS   0x0014u
#define PPC_ACCEL_MB_OFF_ARG0     0x0018u
#define PPC_ACCEL_MB_OFF_ARG1     0x001Cu
#define PPC_ACCEL_MB_OFF_RESULT0  0x0020u
#define PPC_ACCEL_MB_OFF_RESULT1  0x0024u

#define PPC_ACCEL_MB_MAGIC        0x504D4241u
#define PPC_ACCEL_MB_CMD_TIME32   3u
#define PPC_ACCEL_MB_STATUS_IDLE  0u
#define PPC_ACCEL_MB_STATUS_DONE  2u
#define PPC_ACCEL_MB_STATUS_ERR   3u

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

  mailbox = board + read_be32(board, PPC_ACCEL_REG_MB_OFFSET);
  if (read_be32(mailbox, PPC_ACCEL_MB_OFF_MAGIC) != PPC_ACCEL_MB_MAGIC) {
    printf("Mailbox magic mismatch.\n");
    return 0xFFFFFFFFu;
  }

  seq = read_be32(mailbox, PPC_ACCEL_MB_OFF_SEQ) + 1u;
  if (seq == 0u) {
    seq = 1u;
  }

  write_be32(mailbox, PPC_ACCEL_MB_OFF_CMD, PPC_ACCEL_MB_CMD_TIME32);
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

int main(int argc, char **argv) {
  struct ExpansionBase *ExpansionBase;
  struct ConfigDev *cd;
  volatile UBYTE *board;
  ULONG loops;
  ULONG i;

  loops = 1u;
  if (argc >= 2) {
    loops = (ULONG)strtoul(argv[1], NULL, 0);
    if (loops == 0u) {
      loops = 1u;
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

  CloseLibrary((struct Library *)ExpansionBase);
  return 0;
}
