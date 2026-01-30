// SPDX-License-Identifier: MIT
#include <exec/types.h>
#include <string.h>

#include "z3serial.h"

#define REG_DATA   0x00
#define REG_STATUS 0x04
#define REG_CTRL   0x08

#define STATUS_RX_READY 0x01
#define STATUS_TX_READY 0x02

static volatile UBYTE *z3serial_base = (UBYTE *)0;

void z3serial_set_base(ULONG base) {
  z3serial_base = (volatile UBYTE *)base;
}

ULONG z3serial_get_base(void) {
  return (ULONG)z3serial_base;
}

UBYTE z3serial_get_status(void) {
  if (!z3serial_base) {
    return 0;
  }
  return z3serial_base[REG_STATUS];
}

void z3serial_clear(void) {
  if (!z3serial_base) {
    return;
  }
  z3serial_base[REG_CTRL] = 0x01;
}

void z3serial_putc(UBYTE c) {
  if (!z3serial_base) {
    return;
  }
  z3serial_base[REG_DATA] = c;
}

UBYTE z3serial_getc(void) {
  if (!z3serial_base) {
    return 0;
  }
  while (!(z3serial_base[REG_STATUS] & STATUS_RX_READY)) {
    ;
  }
  return z3serial_base[REG_DATA];
}

void z3serial_write(const char *s) {
  if (!z3serial_base || !s) {
    return;
  }
  while (*s) {
    z3serial_putc((UBYTE)*s++);
  }
}

ULONG z3serial_read(UBYTE *out, ULONG max_len) {
  ULONG count = 0;
  if (!z3serial_base || !out || max_len == 0) {
    return 0;
  }
  while (count < max_len) {
    if (!(z3serial_base[REG_STATUS] & STATUS_RX_READY)) {
      break;
    }
    out[count++] = z3serial_base[REG_DATA];
  }
  return count;
}
