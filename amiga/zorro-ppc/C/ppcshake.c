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
#include <errno.h>

#include "ppc_accel_regs.h"

#define PPC_ACCEL_MANUFACTURER PPC_ACCEL_MANUFACTURER_ID
#define PPC_ACCEL_PRODUCT PPC_ACCEL_PRODUCT_ID

#define PPCSHAKE_WAIT_SPINS 200000u
#define PPCSHAKE_BUSY_SETTLE_SPINS 50000u
#define PPCSHAKE_BUSY_RESULT 0xFFFFFFFEu
#define PPCSHAKE_ERROR_RESULT 0xFFFFFFFFu
#define PPCSHAKE_LOCK_PATH "T:ppcshake.lock"
#define PPCSHAKE_STATE_WAIT_SPINS 200000u
#define PPCSHAKE_ELF_EHDR_SIZE 52u
#define PPCSHAKE_ELF_PHDR_MIN_SIZE 32u

static void print_usage(const char *prog) {
  printf("Usage: %s [options] [loops]\n", prog);
  printf("Options:\n");
  printf("  --id            Dump board identity/registers only\n");
  printf("  --irq           Run IRQ semantics test\n");
  printf("  --reset         Assert board reset and clear mailbox lane\n");
  printf("  --boot          Start PPC runtime (CONTROL.START)\n");
  printf("  --shutdown      Stop PPC runtime (clear CONTROL.START)\n");
  printf("  --elf <path>    Load PPC32 big-endian ELF into PPC RAM and boot entry\n");
}

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

static ULONG file_be16(const UBYTE *p) {
  return ((ULONG)p[0] << 8) | (ULONG)p[1];
}

static ULONG file_be32(const UBYTE *p) {
  return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | (ULONG)p[3];
}

static void write_abs_u8(ULONG addr, const UBYTE *src, ULONG len) {
  volatile UBYTE *dst;
  ULONG i;

  dst = (volatile UBYTE *)(ULONG)addr;
  for (i = 0u; i < len; i++) {
    dst[i] = src[i];
  }
}

static void zero_abs_u8(ULONG addr, ULONG len) {
  volatile UBYTE *dst;
  ULONG i;

  dst = (volatile UBYTE *)(ULONG)addr;
  for (i = 0u; i < len; i++) {
    dst[i] = 0u;
  }
}

static int load_ppc_elf32(volatile UBYTE *board, const char *path, ULONG *out_entry, ULONG *out_stack) {
  FILE *fp;
  UBYTE ehdr[PPCSHAKE_ELF_EHDR_SIZE];
  UBYTE phdr[64];
  UBYTE buf[1024];
  ULONG phoff;
  ULONG phentsize;
  ULONG phnum;
  ULONG entry;
  ULONG ram_base;
  ULONG ram_size;
  ULONG ram_end;
  ULONG seg_loaded;
  ULONG i;

  fp = fopen(path, "rb");
  if (fp == NULL) {
    printf("ELF open failed: %s (%s)\n", path, strerror(errno));
    return 1;
  }

  if (fread(ehdr, 1u, PPCSHAKE_ELF_EHDR_SIZE, fp) != PPCSHAKE_ELF_EHDR_SIZE) {
    printf("ELF read failed: header too short\n");
    fclose(fp);
    return 1;
  }

  if ((ehdr[0] != 0x7fu) || (ehdr[1] != 'E') || (ehdr[2] != 'L') || (ehdr[3] != 'F')) {
    printf("ELF error: bad magic\n");
    fclose(fp);
    return 1;
  }
  if (ehdr[4] != 1u) {
    printf("ELF error: not ELF32\n");
    fclose(fp);
    return 1;
  }
  if (ehdr[5] != 2u) {
    printf("ELF error: not big-endian\n");
    fclose(fp);
    return 1;
  }
  if (file_be16(&ehdr[18]) != 20u) {
    printf("ELF error: e_machine=%u (expected PPC=20)\n", (unsigned int)file_be16(&ehdr[18]));
    fclose(fp);
    return 1;
  }

  entry = file_be32(&ehdr[24]);
  phoff = file_be32(&ehdr[28]);
  phentsize = file_be16(&ehdr[42]);
  phnum = file_be16(&ehdr[44]);

  if ((phentsize < PPCSHAKE_ELF_PHDR_MIN_SIZE) || (phentsize > sizeof(phdr))) {
    printf("ELF error: unsupported phentsize=%u\n", (unsigned int)phentsize);
    fclose(fp);
    return 1;
  }
  if (phnum == 0u) {
    printf("ELF error: no program headers\n");
    fclose(fp);
    return 1;
  }

  ram_base = read_be32(board, PPC_ACCEL_REG_PPC_RAM_BASE);
  ram_size = read_be32(board, PPC_ACCEL_REG_PPC_RAM_SIZE);
  ram_end = ram_base + ram_size;
  if ((ram_size == 0u) || (ram_end < ram_base)) {
    printf("ELF error: invalid PPC RAM map base=$%08X size=$%08X\n",
           (unsigned int)ram_base, (unsigned int)ram_size);
    fclose(fp);
    return 1;
  }
  if ((entry < ram_base) || (entry >= ram_end)) {
    printf("ELF error: entry point out of PPC RAM range entry=$%08X ram=$%08X+$%08X\n",
           (unsigned int)entry,
           (unsigned int)ram_base,
           (unsigned int)ram_size);
    fclose(fp);
    return 1;
  }

  seg_loaded = 0u;
  for (i = 0u; i < phnum; i++) {
    ULONG p_type;
    ULONG p_offset;
    ULONG p_vaddr;
    ULONG p_filesz;
    ULONG p_memsz;

    if (fseek(fp, (long)(phoff + (i * phentsize)), SEEK_SET) != 0) {
      printf("ELF error: seek phdr %u failed\n", (unsigned int)i);
      fclose(fp);
      return 1;
    }
    if (fread(phdr, 1u, phentsize, fp) != phentsize) {
      printf("ELF error: read phdr %u failed\n", (unsigned int)i);
      fclose(fp);
      return 1;
    }

    p_type = file_be32(&phdr[0]);
    p_offset = file_be32(&phdr[4]);
    p_vaddr = file_be32(&phdr[8]);
    p_filesz = file_be32(&phdr[16]);
    p_memsz = file_be32(&phdr[20]);

    if (p_type != 1u) {
      continue;
    }
    if (p_memsz == 0u) {
      continue;
    }
    if ((p_vaddr < ram_base) || (p_vaddr >= ram_end) || (p_memsz > (ram_end - p_vaddr))) {
      printf("ELF error: segment %u out of PPC RAM range vaddr=$%08X memsz=$%08X (ram=$%08X-$%08X)\n",
             (unsigned int)i,
             (unsigned int)p_vaddr,
             (unsigned int)p_memsz,
             (unsigned int)ram_base,
             (unsigned int)(ram_end - 1u));
      fclose(fp);
      return 1;
    }
    if (p_filesz > p_memsz) {
      printf("ELF error: segment %u filesz > memsz\n", (unsigned int)i);
      fclose(fp);
      return 1;
    }

    if (p_filesz > 0u) {
      ULONG remaining;
      ULONG addr;

      if (fseek(fp, (long)p_offset, SEEK_SET) != 0) {
        printf("ELF error: seek segment %u data failed\n", (unsigned int)i);
        fclose(fp);
        return 1;
      }

      remaining = p_filesz;
      addr = p_vaddr;
      while (remaining > 0u) {
        ULONG chunk;

        chunk = remaining;
        if (chunk > (ULONG)sizeof(buf)) {
          chunk = (ULONG)sizeof(buf);
        }
        if (fread(buf, 1u, chunk, fp) != chunk) {
          printf("ELF error: read segment %u data failed\n", (unsigned int)i);
          fclose(fp);
          return 1;
        }
        write_abs_u8(addr, buf, chunk);
        addr += chunk;
        remaining -= chunk;
      }
    }

    if (p_memsz > p_filesz) {
      zero_abs_u8(p_vaddr + p_filesz, p_memsz - p_filesz);
    }

    seg_loaded++;
  }

  fclose(fp);

  if (seg_loaded == 0u) {
    printf("ELF error: no PT_LOAD segments loaded\n");
    return 1;
  }

  *out_entry = entry;
  *out_stack = ram_end - 0x1000u;
  printf("ELF loaded: %s segments=%u entry=$%08X ram=$%08X+$%08X\n",
         path,
         (unsigned int)seg_loaded,
         (unsigned int)entry,
         (unsigned int)ram_base,
         (unsigned int)ram_size);
  return 0;
}

static ULONG wait_running_state(volatile UBYTE *board, ULONG want_running) {
  ULONG spins;

  spins = PPCSHAKE_STATE_WAIT_SPINS;
  while (spins > 0u) {
    ULONG status;
    ULONG running;

    status = read_be32(board, PPC_ACCEL_REG_STATUS);
    running = (status & PPC_ACCEL_STATUS_RUNNING) != 0u ? 1u : 0u;
    if (running == want_running) {
      return 1u;
    }
    spins--;
  }

  return 0u;
}

static int do_board_boot(volatile UBYTE *board) {
  ULONG control;
  ULONG status;

  control = read_be32(board, PPC_ACCEL_REG_CONTROL);
  control |= PPC_ACCEL_CTRL_START;
  write_be32(board, PPC_ACCEL_REG_CONTROL, control);

  if (wait_running_state(board, 1u) == 0u) {
    status = read_be32(board, PPC_ACCEL_REG_STATUS);
    printf("Boot failed: STATUS=$%08X\n", (unsigned int)status);
    return 1;
  }

  status = read_be32(board, PPC_ACCEL_REG_STATUS);
  printf("Boot OK: STATUS=$%08X CONTROL=$%08X\n",
         (unsigned int)status,
         (unsigned int)read_be32(board, PPC_ACCEL_REG_CONTROL));
  return 0;
}

static int do_board_shutdown(volatile UBYTE *board) {
  ULONG control;
  ULONG status;

  control = read_be32(board, PPC_ACCEL_REG_CONTROL);
  control &= ~PPC_ACCEL_CTRL_START;
  write_be32(board, PPC_ACCEL_REG_CONTROL, control);

  if (wait_running_state(board, 0u) == 0u) {
    status = read_be32(board, PPC_ACCEL_REG_STATUS);
    printf("Shutdown failed: STATUS=$%08X\n", (unsigned int)status);
    return 1;
  }

  status = read_be32(board, PPC_ACCEL_REG_STATUS);
  printf("Shutdown OK: STATUS=$%08X CONTROL=$%08X\n",
         (unsigned int)status,
         (unsigned int)read_be32(board, PPC_ACCEL_REG_CONTROL));
  return 0;
}

static int do_board_reset(volatile UBYTE *board) {
  volatile UBYTE *mailbox;
  ULONG mb_offset;
  ULONG seq;
  ULONG ack;
  ULONG mb_status;
  ULONG status;

  write_be32(board, PPC_ACCEL_REG_CONTROL, PPC_ACCEL_CTRL_RESET);
  write_be32(board, PPC_ACCEL_REG_IRQ_ACK, PPC_ACCEL_IRQ_CMD_DONE | PPC_ACCEL_IRQ_HOST_DOORBELL);

  if (wait_running_state(board, 0u) == 0u) {
    status = read_be32(board, PPC_ACCEL_REG_STATUS);
    printf("Reset incomplete: STATUS=$%08X\n", (unsigned int)status);
    return 1;
  }

  mb_offset = read_be32(board, PPC_ACCEL_REG_MAILBOX_OFFSET);
  mailbox = board + mb_offset;
  seq = read_be32(mailbox, PPC_ACCEL_MB_OFF_SEQ);
  ack = read_be32(mailbox, PPC_ACCEL_MB_OFF_ACK_SEQ);
  mb_status = read_be32(mailbox, PPC_ACCEL_MB_OFF_STATUS);

  printf("Reset OK: STATUS=$%08X CONTROL=$%08X MB(seq=%u ack=%u status=%u)\n",
         (unsigned int)read_be32(board, PPC_ACCEL_REG_STATUS),
         (unsigned int)read_be32(board, PPC_ACCEL_REG_CONTROL),
         (unsigned int)seq,
         (unsigned int)ack,
         (unsigned int)mb_status);
  return 0;
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
  ULONG shared_sig;
  ULONG shared_abi;
  ULONG shared_mb_offset;
  ULONG shared_mb_size;
  ULONG shared_db_reg;
  ULONG shared_features;
  ULONG shared_reserved0;
  ULONG shared_reserved1;
  ULONG boot_sig;
  ULONG boot_ver;
  ULONG boot_chip_low;
  ULONG boot_chip_high;
  ULONG boot_main_low;
  ULONG boot_main_high;
  ULONG boot_text_low;
  ULONG boot_text_size;
  ULONG boot_data_size;
  ULONG boot_kern_mem_size;
  ULONG boot_page_size;
  ULONG boot_rodata_size;
  ULONG boot_flags;
  ULONG ppc_ram_base;
  ULONG ppc_ram_size;
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
  printf("  PPC_RAM       = base=$%08X size=$%08X\n",
         (unsigned int)ppc_ram_base,
         (unsigned int)ppc_ram_size);

  shared_sig = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_SIGNATURE);
  shared_abi = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_ABI_VERSION);
  shared_mb_offset = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_MB_OFFSET);
  shared_mb_size = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_MB_SIZE);
  shared_db_reg = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_DB_REG);
  shared_features = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_FEATURES);
  shared_reserved0 = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_RESERVED0);
  shared_reserved1 = read_be32(board, PPC_ACCEL_SHARED_INFO_OFFSET + PPC_ACCEL_SHARED_INFO_OFF_RESERVED1);

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
  printf("  RESERVED0     = $%08X\n", (unsigned int)shared_reserved0);
  printf("  RESERVED1     = $%08X\n", (unsigned int)shared_reserved1);

  boot_sig = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_SIGNATURE);
  boot_ver = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_VERSION);
  boot_chip_low = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_CHIP_LOW);
  boot_chip_high = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_CHIP_HIGH);
  boot_main_low = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_MAIN_LOW);
  boot_main_high = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_MAIN_HIGH);
  boot_text_low = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_TEXT_LOW);
  boot_text_size = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_TEXT_SIZE);
  boot_data_size = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_DATA_SIZE);
  boot_kern_mem_size = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_KERN_MEM_SIZE);
  boot_page_size = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_PAGE_SIZE);
  boot_rodata_size = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_RODATA_SIZE);
  boot_flags = read_be32(board, PPC_ACCEL_BOOTAREA_OFFSET + PPC_ACCEL_BOOTAREA_OFF_FLAGS);

  printf("BootArea mirror @ +$%08X:\n", (unsigned int)PPC_ACCEL_BOOTAREA_OFFSET);
  printf("  SIG           = $%08X\n", (unsigned int)boot_sig);
  printf("  VERSION       = %u\n", (unsigned int)boot_ver);
  printf("  CHIP          = $%08X-$%08X\n",
         (unsigned int)boot_chip_low,
         (unsigned int)boot_chip_high);
  printf("  MAIN          = $%08X-$%08X\n",
         (unsigned int)boot_main_low,
         (unsigned int)boot_main_high);
  printf("  TEXT          = $%08X +$%08X\n",
         (unsigned int)boot_text_low,
         (unsigned int)boot_text_size);
  printf("  DATA_SIZE     = $%08X\n", (unsigned int)boot_data_size);
  printf("  KERN_MEM_SIZE = $%08X\n", (unsigned int)boot_kern_mem_size);
  printf("  PAGE_SIZE     = $%08X\n", (unsigned int)boot_page_size);
  printf("  RODATA_SIZE   = $%08X\n", (unsigned int)boot_rodata_size);
  printf("  FLAGS         = $%08X\n", (unsigned int)boot_flags);

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
  ULONG elf_entry;
  ULONG elf_stack;
  int loops_specified;
  int do_identity_dump;
  int do_irq_test;
  int do_reset;
  int do_boot;
  int do_shutdown;
  int do_elf;
  int parse_error;
  const char *elf_path;
  int exit_code;

  loops = 1u;
  elf_entry = 0u;
  elf_stack = 0u;
  loops_specified = 0;
  do_identity_dump = 0;
  do_irq_test = 0;
  do_reset = 0;
  do_boot = 0;
  do_shutdown = 0;
  do_elf = 0;
  parse_error = 0;
  elf_path = NULL;
  exit_code = 0;
  instance_lock = (BPTR)0;
  ExpansionBase = NULL;
  for (i = 1u; i < (ULONG)argc; i++) {
    if ((strcmp(argv[i], "--irq") == 0) || (strcmp(argv[i], "-i") == 0)) {
      do_irq_test = 1;
    } else if ((strcmp(argv[i], "--id") == 0) || (strcmp(argv[i], "--identity") == 0)) {
      do_identity_dump = 1;
    } else if (strcmp(argv[i], "--reset") == 0) {
      do_reset = 1;
    } else if (strcmp(argv[i], "--boot") == 0) {
      do_boot = 1;
    } else if (strcmp(argv[i], "--shutdown") == 0) {
      do_shutdown = 1;
    } else if (strcmp(argv[i], "--elf") == 0) {
      if ((i + 1u) >= (ULONG)argc) {
        printf("Missing argument for --elf\n");
        parse_error = 1;
        break;
      }
      do_elf = 1;
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
      loops_specified = 1;
    }
  }
  if (parse_error != 0) {
    print_usage(argv[0]);
    return 9;
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

  if ((do_elf != 0) && (loops_specified == 0) && (do_irq_test == 0)) {
    loops = 0u;
  }

  if ((loops_specified == 0) && ((do_irq_test != 0) || (do_reset != 0) || (do_boot != 0)
      || (do_shutdown != 0))) {
    loops = 0u;
  }

  if (do_reset != 0) {
    int reset_rc;

    reset_rc = do_board_reset(board);
    if (reset_rc != 0) {
      exit_code = 40 + reset_rc;
      goto cleanup;
    }
  }

  if (do_shutdown != 0) {
    int shutdown_rc;

    shutdown_rc = do_board_shutdown(board);
    if (shutdown_rc != 0) {
      exit_code = 50 + shutdown_rc;
      goto cleanup;
    }
  }

  if (do_elf != 0) {
    int elf_rc;

    if (do_reset == 0) {
      int reset_rc;

      reset_rc = do_board_reset(board);
      if (reset_rc != 0) {
        exit_code = 40 + reset_rc;
        goto cleanup;
      }
    }

    elf_rc = load_ppc_elf32(board, elf_path, &elf_entry, &elf_stack);
    if (elf_rc != 0) {
      exit_code = 60;
      goto cleanup;
    }

    write_be32(board, PPC_ACCEL_REG_BOOT_MAGIC, PPC_ACCEL_BOOT_DESC_MAGIC);
    write_be32(board, PPC_ACCEL_REG_BOOT_ENTRY, elf_entry);
    write_be32(board, PPC_ACCEL_REG_BOOT_STACK, elf_stack);
    write_be32(board, PPC_ACCEL_REG_BOOT_ARG0, PPC_ACCEL_MAILBOX_OFFSET);
    printf("ELF bootdesc: entry=$%08X stack=$%08X arg0=$%08X\n",
           (unsigned int)elf_entry,
           (unsigned int)elf_stack,
           (unsigned int)PPC_ACCEL_MAILBOX_OFFSET);
    do_boot = 1;
  }

  if ((do_boot != 0) || (loops > 0u) || (do_irq_test != 0)) {
    int boot_rc;

    boot_rc = do_board_boot(board);
    if (boot_rc != 0) {
      exit_code = 4;
      goto cleanup;
    }
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

  if ((do_boot == 0) && (do_irq_test == 0) && (loops == 0u)
      && (do_reset == 0) && (do_shutdown == 0) && (do_elf == 0)) {
    /*
     * Preserve legacy behavior: no flags/no loop argument means one TIME32 roundtrip.
     */
    ULONG t32;

    if (do_board_boot(board) != 0) {
      exit_code = 4;
      goto cleanup;
    }

    t32 = do_time32_roundtrip(board);
    if (t32 == PPCSHAKE_BUSY_RESULT) {
      exit_code = 7;
      goto cleanup;
    }
    if (t32 == PPCSHAKE_ERROR_RESULT) {
      exit_code = 5;
      goto cleanup;
    }
    printf("TIME32[0] = $%08X (%u)\n", (unsigned int)t32, (unsigned int)t32);
    goto cleanup;
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
