// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pissa.h"

static uint32_t parse_hex(const char *s) {
  if (!s) {
    return 0;
  }
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    return (uint32_t)strtoul(s + 2, NULL, 16);
  }
  return (uint32_t)strtoul(s, NULL, 16);
}

static void usage(void) {
  printf("Usage: pissa <base_hex>\n");
  printf("Example: pissa 0x00E90000\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return 5;
  }

  uint32_t base = parse_hex(argv[1]);
  if (base == 0) {
    printf("Invalid base address.\n");
    return 5;
  }

  uint32_t status = pissa_read_status(base);
  printf("PISSA status @0x%08lx = 0x%08lx\n", (unsigned long)base, (unsigned long)status);
  return 0;
}
