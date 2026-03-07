/* SPDX-License-Identifier: MIT */
OUTPUT_FORMAT("elf32-powerpc")
ENTRY(_start)

SECTIONS
{
  . = 0x08000200;
  .text : { *(.text*) *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
}
