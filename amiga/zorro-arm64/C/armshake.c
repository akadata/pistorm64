// SPDX-License-Identifier: MIT
#include <exec/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm64_accel_regs.h"
#include "armaccel_device.h"

static void print_usage(const char *prog) {
  printf("Usage: %s [options] [loops]\n", prog);
  printf("Options:\n");
  printf("  --id           Dump board identity/registers only\n");
  printf("  --ping         Run mailbox ping test (default)\n");
  printf("  --irq          Run IRQ semantics test on ping completion\n");
  printf("  --help, -h     Show help\n");
  printf("\n");
  printf("armshake is diagnostics-only. Payload execution belongs to armaccel.library.\n");
}

int main(int argc, char **argv) {
  struct armaccel_board board;
  ULONG loops;
  ULONG i;
  int do_identity_dump;
  int do_ping;
  int do_irq;
  int parse_error;
  int exit_code;
  int open_rc;

  memset(&board, 0, sizeof(board));
  loops = 1u;
  do_identity_dump = 0;
  do_ping = 1;
  do_irq = 0;
  parse_error = 0;
  exit_code = 0;

  for (i = 1u; i < (ULONG)argc; i++) {
    if ((strcmp(argv[i], "--id") == 0) || (strcmp(argv[i], "--identity") == 0)) {
      do_identity_dump = 1;
      do_ping = 0;
    } else if (strcmp(argv[i], "--ping") == 0) {
      do_ping = 1;
    } else if (strcmp(argv[i], "--irq") == 0) {
      do_irq = 1;
    } else if (strcmp(argv[i], "--elf") == 0) {
      printf("armshake no longer launches payloads. Use armaccel.library execution API.\n");
      return 9;
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

  open_rc = armaccel_device_open(&board);
  if (open_rc != 0) {
    if (open_rc == 2) {
      printf("Failed to open expansion.library.\n");
    } else if (open_rc == 3) {
      printf("ARM64 accel board not found (mfr=$%04X prod=$%04X).\n",
             (unsigned int)ARM64_ACCEL_MANUFACTURER_ID,
             (unsigned int)ARM64_ACCEL_PRODUCT_ID);
    } else {
      printf("Failed to initialize armaccel.device transport.\n");
    }
    return 1;
  }

  printf("ARM64 accel found at $%08lX\n", (unsigned long)board.config_dev->cd_BoardAddr);

  if (do_identity_dump != 0) {
    exit_code = armaccel_device_dump_identity(&board);
    armaccel_device_close(&board);
    return exit_code;
  }

  if (do_irq != 0) {
    exit_code = armaccel_device_irq_test(&board);
    armaccel_device_close(&board);
    return exit_code;
  }

  if (do_ping != 0) {
    for (i = 0u; i < loops; i++) {
      ULONG result = armaccel_device_ping(&board);
      if (result == ARMACCEL_DEVICE_BUSY_RESULT) {
        printf("PING[%u] busy\n", (unsigned int)i);
        exit_code = 3;
        break;
      }
      if (result == ARMACCEL_DEVICE_ERROR_RESULT) {
        printf("PING[%u] failed\n", (unsigned int)i);
        exit_code = 4;
        break;
      }
      printf("PING[%u] result=$%08X\n", (unsigned int)i, (unsigned int)result);
    }
  }

  armaccel_device_close(&board);
  return exit_code;
}
