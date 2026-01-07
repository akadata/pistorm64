# Gary (Bus / Glue Logic)

Sources:
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node00D3.html.txt

## Notes
The ADCD 2.1 corpus does not provide a programmer-visible register map for the Gary chip.
Gary is primarily glue logic for bus control, ROM decode, and arbitration, and does not
expose custom registers in the 0xDFFxxx space.

For software-facing work, use the system memory map and custom chip registers instead.
