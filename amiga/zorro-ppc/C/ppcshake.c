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
#define PPCSHAKE_BOOT_MARKER_SPINS 500000u
#define PPCSHAKE_BOOT_TEST_ENTRY 0x00000200u
#define PPCSHAKE_BOOT_MARKER_DONE 0x00000002u
#define PPCSHAKE_BOOT_MARKER_TEST 0x00000003u

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

static ULONG wait_boot_marker(volatile UBYTE *board, ULONG expect, ULONG spins) {
  ULONG marker_offset;
  ULONG i;

  marker_offset = PPC_ACCEL_BOOT_DESC_OFFSET + PPC_ACCEL_BOOT_DESC_OFF_MARKER;
  for (i = 0u; i < spins; i++) {
    ULONG marker;

    marker = read_be32(board, marker_offset);
    if (marker == expect) {
      return marker;
    }
  }

  return read_be32(board, marker_offset);
}

static int do_boot_descriptor_test(volatile UBYTE *board) {
  ULONG control_before;
  ULONG keep_irq;
  ULONG old_magic;
  ULONG old_entry;
  ULONG old_stack;
  ULONG old_arg0;
  ULONG old_marker;
  ULONG observed;

  control_before = read_be32(board, PPC_ACCEL_REG_CONTROL);
  keep_irq = control_before & PPC_ACCEL_CTRL_IRQ_ENABLE;
  old_magic = read_be32(board, PPC_ACCEL_REG_BOOT_MAGIC);
  old_entry = read_be32(board, PPC_ACCEL_REG_BOOT_ENTRY);
  old_stack = read_be32(board, PPC_ACCEL_REG_BOOT_STACK);
  old_arg0 = read_be32(board, PPC_ACCEL_REG_BOOT_ARG0);
  old_marker = read_be32(board, PPC_ACCEL_BOOT_DESC_OFFSET + PPC_ACCEL_BOOT_DESC_OFF_MARKER);

  write_be32(board, PPC_ACCEL_REG_BOOT_MAGIC, PPC_ACCEL_BOOT_DESC_MAGIC);
  write_be32(board, PPC_ACCEL_REG_BOOT_ENTRY, PPCSHAKE_BOOT_TEST_ENTRY);
  if (old_stack != 0u) {
    write_be32(board, PPC_ACCEL_REG_BOOT_STACK, old_stack);
  }
  if (old_arg0 != 0u) {
    write_be32(board, PPC_ACCEL_REG_BOOT_ARG0, old_arg0);
  }

  write_be32(board, PPC_ACCEL_REG_CONTROL, keep_irq | PPC_ACCEL_CTRL_RESET);
  write_be32(board, PPC_ACCEL_REG_CONTROL, keep_irq | PPC_ACCEL_CTRL_START);
  observed = wait_boot_marker(board, PPCSHAKE_BOOT_MARKER_TEST, PPCSHAKE_BOOT_MARKER_SPINS);
  if (observed != PPCSHAKE_BOOT_MARKER_TEST) {
    printf("BOOT test failed: marker=$%08X expected=$%08X.\n",
           (unsigned int)observed,
           (unsigned int)PPCSHAKE_BOOT_MARKER_TEST);
    printf("BOOT regs: magic=$%08X entry=$%08X stack=$%08X arg0=$%08X\n",
           (unsigned int)read_be32(board, PPC_ACCEL_REG_BOOT_MAGIC),
           (unsigned int)read_be32(board, PPC_ACCEL_REG_BOOT_ENTRY),
           (unsigned int)read_be32(board, PPC_ACCEL_REG_BOOT_STACK),
           (unsigned int)read_be32(board, PPC_ACCEL_REG_BOOT_ARG0));
    return 1;
  }

  printf("BOOT test stage1 OK: entry=$%08X marker=$%08X\n",
         (unsigned int)PPCSHAKE_BOOT_TEST_ENTRY,
         (unsigned int)observed);

  write_be32(board, PPC_ACCEL_REG_BOOT_MAGIC, old_magic);
  write_be32(board, PPC_ACCEL_REG_BOOT_ENTRY, old_entry);
  write_be32(board, PPC_ACCEL_REG_BOOT_STACK, old_stack);
  write_be32(board, PPC_ACCEL_REG_BOOT_ARG0, old_arg0);
  write_be32(board, PPC_ACCEL_BOOT_DESC_OFFSET + PPC_ACCEL_BOOT_DESC_OFF_MARKER, old_marker);

  write_be32(board, PPC_ACCEL_REG_CONTROL, keep_irq | PPC_ACCEL_CTRL_RESET);
  write_be32(board, PPC_ACCEL_REG_CONTROL, keep_irq | PPC_ACCEL_CTRL_START);
  observed = wait_boot_marker(board, PPCSHAKE_BOOT_MARKER_DONE, PPCSHAKE_BOOT_MARKER_SPINS);
  if (observed != PPCSHAKE_BOOT_MARKER_DONE) {
    printf("BOOT test restore failed: marker=$%08X expected=$%08X.\n",
           (unsigned int)observed,
           (unsigned int)PPCSHAKE_BOOT_MARKER_DONE);
    return 2;
  }

  printf("BOOT test stage2 OK: restored entry=$%08X marker=$%08X\n",
         (unsigned int)old_entry,
         (unsigned int)observed);
  return 0;
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

static int dump_board_identity(volatile UBYTE *board) {
  ULONG magic;
  ULONG abi_version;
  ULONG control;
  ULONG status;
  ULONG irq_status;
  ULONG mb_offset;
  ULONG mb_size;
  ULONG shared_offset;
  ULONG shared_size;
  ULONG ppc_ram_base;
  ULONG ppc_ram_size;
  ULONG boot_magic;
  ULONG boot_entry;
  ULONG boot_stack;
  ULONG boot_arg0;
  ULONG boot_marker;
  ULONG shared_sig;
  ULONG shared_abi;
  ULONG shared_mb_offset;
  ULONG shared_mb_size;
  ULONG shared_db_reg;
  ULONG shared_features;
  ULONG irq_before;
  ULONG irq_after_ring;
  ULONG irq_after_ack;

  magic = read_be32(board, PPC_ACCEL_REG_MAGIC);
  abi_version = read_be32(board, PPC_ACCEL_REG_ABI_VERSION);
  control = read_be32(board, PPC_ACCEL_REG_CONTROL);
  status = read_be32(board, PPC_ACCEL_REG_STATUS);
  irq_status = read_be32(board, PPC_ACCEL_REG_IRQ_STATUS);
  mb_offset = read_be32(board, PPC_ACCEL_REG_MAILBOX_OFFSET);
  mb_size = read_be32(board, PPC_ACCEL_REG_MAILBOX_SIZE);
  shared_offset = read_be32(board, PPC_ACCEL_REG_SHARED_OFFSET);
  shared_size = read_be32(board, PPC_ACCEL_REG_SHARED_SIZE);
  ppc_ram_base = read_be32(board, PPC_ACCEL_REG_PPC_RAM_BASE);
  ppc_ram_size = read_be32(board, PPC_ACCEL_REG_PPC_RAM_SIZE);
  boot_magic = read_be32(board, PPC_ACCEL_REG_BOOT_MAGIC);
  boot_entry = read_be32(board, PPC_ACCEL_REG_BOOT_ENTRY);
  boot_stack = read_be32(board, PPC_ACCEL_REG_BOOT_STACK);
  boot_arg0 = read_be32(board, PPC_ACCEL_REG_BOOT_ARG0);
  boot_marker = read_be32(board, PPC_ACCEL_BOOT_DESC_OFFSET + PPC_ACCEL_BOOT_DESC_OFF_MARKER);

  printf("Board identity:\n");
  printf("  MAGIC         = $%08X\n", (unsigned int)magic);
  printf("  ABI_VERSION   = %u\n", (unsigned int)abi_version);
  printf("  CONTROL       = $%08X\n", (unsigned int)control);
  printf("  STATUS        = $%08X (RUNNING=%u FAULT=%u)\n",
         (unsigned int)status,
         (unsigned int)((status & PPC_ACCEL_STATUS_RUNNING) != 0u),
         (unsigned int)((status & PPC_ACCEL_STATUS_FAULT) != 0u));
  printf("  IRQ_STATUS    = $%08X\n", (unsigned int)irq_status);
  printf("  MAILBOX       = off=$%08X size=$%08X\n",
         (unsigned int)mb_offset,
         (unsigned int)mb_size);
  printf("  SHARED        = off=$%08X size=$%08X\n",
         (unsigned int)shared_offset,
         (unsigned int)shared_size);
  printf("  PPC_RAM       = base=$%08X size=$%08X (%u MiB)\n",
         (unsigned int)ppc_ram_base,
         (unsigned int)ppc_ram_size,
         (unsigned int)(ppc_ram_size / (1024u * 1024u)));
  printf("  BOOT_DESC     = magic=$%08X entry=$%08X stack=$%08X arg0=$%08X marker=$%08X\n",
         (unsigned int)boot_magic,
         (unsigned int)boot_entry,
         (unsigned int)boot_stack,
         (unsigned int)boot_arg0,
         (unsigned int)boot_marker);

  shared_sig = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_SIGNATURE);
  shared_abi = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_ABI_VERSION);
  shared_mb_offset = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_MB_OFFSET);
  shared_mb_size = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_MB_SIZE);
  shared_db_reg = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_DB_REG);
  shared_features = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_FEATURES);

  printf("Shared info @ +$%08X:\n", (unsigned int)PPC_ACCEL_SHARED_INFO_OFFSET);
  printf("  SIG           = $%08X\n", (unsigned int)shared_sig);
  printf("  ABI_VERSION   = %u\n", (unsigned int)shared_abi);
  printf("  MB_OFFSET     = $%08X\n", (unsigned int)shared_mb_offset);
  printf("  MB_SIZE       = $%08X\n", (unsigned int)shared_mb_size);
  printf("  DOORBELL_REG  = $%08X\n", (unsigned int)shared_db_reg);
  printf("  FEATURES      = $%08X (HOSTSVC=%u IRQ=%u DOORBELL=%u)\n",
         (unsigned int)shared_features,
         (unsigned int)((shared_features & PPC_ACCEL_SHARED_INFO_FEAT_HOSTSVC) != 0u),
         (unsigned int)((shared_features & PPC_ACCEL_SHARED_INFO_FEAT_IRQ) != 0u),
         (unsigned int)((shared_features & PPC_ACCEL_SHARED_INFO_FEAT_DOORBELL) != 0u));

  write_be32(board, PPC_ACCEL_REG_IRQ_ACK, PPC_ACCEL_IRQ_CMD_DONE | PPC_ACCEL_IRQ_HOST_DOORBELL);
  irq_before = read_be32(board, PPC_ACCEL_REG_IRQ_STATUS);
  write_be32(board, PPC_ACCEL_REG_DOORBELL, 1u);
  irq_after_ring = read_be32(board, PPC_ACCEL_REG_IRQ_STATUS);
  write_be32(board, PPC_ACCEL_REG_IRQ_ACK, PPC_ACCEL_IRQ_HOST_DOORBELL);
  irq_after_ack = read_be32(board, PPC_ACCEL_REG_IRQ_STATUS);

  printf("IRQ doorbell check: before=$%08X after_ring=$%08X after_ack=$%08X\n",
         (unsigned int)irq_before,
         (unsigned int)irq_after_ring,
         (unsigned int)irq_after_ack);

  if ((irq_after_ring & PPC_ACCEL_IRQ_HOST_DOORBELL) == 0u) {
    printf("IRQ doorbell check failed: HOST_DOORBELL did not set.\n");
    return 1;
  }
  if ((irq_after_ack & PPC_ACCEL_IRQ_HOST_DOORBELL) != 0u) {
    printf("IRQ doorbell check failed: HOST_DOORBELL did not clear.\n");
    return 2;
  }

  return 0;
}

int main(int argc, char **argv) {
  struct ExpansionBase *ExpansionBase;
  struct ConfigDev *cd;
  BPTR instance_lock;
  volatile UBYTE *board;
  ULONG loops;
  ULONG i;
  int do_identity_dump;
  int do_irq_test;
  int do_boot_test;
  int exit_code;

  loops = 1u;
  do_identity_dump = 0;
  do_irq_test = 0;
  do_boot_test = 0;
  exit_code = 0;
  instance_lock = (BPTR)0;
  ExpansionBase = NULL;
  for (i = 1u; i < (ULONG)argc; i++) {
    if ((strcmp(argv[i], "--irq") == 0) || (strcmp(argv[i], "-i") == 0)) {
      do_irq_test = 1;
    } else if ((strcmp(argv[i], "--id") == 0) || (strcmp(argv[i], "--identity") == 0)) {
      do_identity_dump = 1;
    } else if ((strcmp(argv[i], "--boot-test") == 0) || (strcmp(argv[i], "--boot") == 0)) {
      do_boot_test = 1;
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

  if (do_identity_dump != 0) {
    int id_rc;

    id_rc = dump_board_identity(board);
    if (id_rc != 0) {
      exit_code = 30 + id_rc;
    }
    goto cleanup;
  }

  if (do_boot_test != 0) {
    int boot_rc;

    boot_rc = do_boot_descriptor_test(board);
    if (boot_rc != 0) {
      exit_code = 40 + boot_rc;
    }
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
