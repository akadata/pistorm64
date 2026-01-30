# 680x0 Compatibility Guidelines

Key guidance from Amiga developer docs (68010/020/030/040 compatibility):

- Do not use the upper 8 bits of a pointer for unrelated data.
- Do not use signed variables/math for addresses.
- Avoid software delay loops and assumptions about async task ordering.
- Exception stack frames differ across 68k models; check type IDs.
- Do not use `MOVE SR` (use `GetCC()` instead).
- Avoid `CLR` on write-sensitive registers (68020 uses single write).
- Self-modifying code is discouraged; flush instruction cache when needed.

Source: `Hardware/ADCD_2.1/_txt/Libraries_Manual_guide/node001B.html.txt`.
