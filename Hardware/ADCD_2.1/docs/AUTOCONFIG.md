# AUTOCONFIG (Zorro II/III)

Source:
- Hardware/ADCD_2.1/_txt/Hardware_Manual_guide/node02C7.html.txt

## Summary
- On reset, all cards are unconfigured and hold /CFGOUTn negated.
- The config chain uses /CFGINn and /CFGOUTn so only one card responds at a time.
- A card with /CFGINn asserted responds to configuration space with read-only registers (AUTOCONFIG ROM) followed by write-only registers.
- The OS reads the AUTOCONFIG ROM to determine size/type, then writes the base address or "shutup".
- Writing the final base address bit (or shutup) causes the card to assert /CFGOUTn, enabling the next device in the chain.

## Configuration Spaces
- Zorro II config space: 0x00E8xxxx (64KB), 16-bit cycles.
- Zorro III config space: 0xFF00xxxx (64KB), 32-bit cycles. A Zorro III PIC can use either Zorro II or Zorro III config space, not both.

## Register Mapping Notes
- Read registers return only the top 4 bits on D31-D28.
- Write registers support nybble/byte/word access to the same logical register.
- Registers are defined as 8-bit logical values, composed of nybbles from paired addresses.
- Read registers (except 0x00) are physically complemented in hardware (logical 0 returned as all 1s).

