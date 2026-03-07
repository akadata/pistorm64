/* SPDX-License-Identifier: MIT */
/* Minimal standalone PPC payload:
 * - writes 0x48454C4F ("HELO") to boot marker at 0x00002050
 * - loops forever
 */
    .section .text
    .globl _start
_start:
    lis     3, 0x0000
    ori     3, 3, 0x2050
    lis     4, 0x4845
    ori     4, 4, 0x4c4f
    stw     4, 0(3)
1:
    b       1b
