// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z3serial.h"

static void usage(const char *prog) {
  fprintf(stderr, "Usage: %s <base_hex> [string]\n", prog);
  fprintf(stderr, "  If string is given, writes it and reads back the echo.\n");
  fprintf(stderr, "  If string is omitted, reads available bytes only.\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  ULONG base = (ULONG)strtoul(argv[1], NULL, 0);
  z3serial_set_base(base);

  if (argc >= 3) {
    const char *msg = argv[2];
    ULONG len = (ULONG)strlen(msg);

    z3serial_clear();
    z3serial_write(msg);

    for (ULONG i = 0; i < len; i++) {
      UBYTE c = z3serial_getc();
      putchar((int)c);
    }
    putchar('\n');
    return 0;
  }

  UBYTE buf[256];
  ULONG n = z3serial_read(buf, (ULONG)sizeof(buf));
  if (n == 0) {
    printf("(no data)\n");
    return 0;
  }
  for (ULONG i = 0; i < n; i++) {
    putchar((int)buf[i]);
  }
  putchar('\n');
  return 0;
}
