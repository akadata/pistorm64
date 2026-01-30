// SPDX-License-Identifier: MIT
#ifndef Z3SERIAL_H
#define Z3SERIAL_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void z3serial_set_base(ULONG base);
ULONG z3serial_get_base(void);

UBYTE z3serial_get_status(void);
void z3serial_clear(void);

void z3serial_putc(UBYTE c);
UBYTE z3serial_getc(void);

void z3serial_write(const char *s);
ULONG z3serial_read(UBYTE *out, ULONG max_len);

#ifdef __cplusplus
}
#endif

#endif
