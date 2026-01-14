# Amiga Interrupt and DMA Control Summary

Sources: Hardware_Manual_guide/node0036.html.txt (INTENA/INTENAR), node0037.html.txt (INTREQ/INTREQR), node002F.html.txt (DMACON/DMACONR).

## INTENA / INTREQ (Paula)
- **Set/Clear**: Bit 15 selects set (1) or clear (0) for bits written as 1. Bits written as 0 are unchanged.
- **Master Enable**: Bit 14 (`INTEN`) gates all interrupt outputs; it is enable-only (no request bit).
- **Bit map (same for enable and request)**  
  - 13 `EXTER` (level 6, external)  
  - 12 `DSKSYN` (level 5, disk sync matched)  
  - 11 `RBF` (level 5, serial receive buffer full)  
  - 10 `AUD3` (level 4, audio channel 3 block finished)  
  - 9 `AUD2` (level 4)  
  - 8 `AUD1` (level 4)  
  - 7 `AUD0` (level 4)  
  - 6 `BLIT` (level 3, blitter finished)  
  - 5 `VERTB` (level 3, start of vertical blank)  
  - 4 `COPER` (level 3, Copper)  
  - 3 `PORTS` (level 2, I/O ports and timers)  
  - 2 `SOFTINT` (level 1, software initiated)  
  - 1 `DSKBLK` (level 1, disk block finished)  
  - 0 `TBE` (level 1, serial transmit buffer empty)  
- **Clearing requests**: Request bits are not auto-cleared; write to INTREQ with bit15=0 and the bit(s) set to 1 to clear.

## DMACON / DMACONR (Agnus, shared DMA control)
- **Set/Clear**: Bit 15 (`SET/CLR`) controls whether written 1-bits set or clear.
- **Status (read-only in DMACONR)**: Bit 14 `BBUSY` (blitter busy), bit 13 `BZERO` (blitter logic zero).
- **Control bits**  
  - 10 `BLTPRI` Blitter DMA priority (\"blitter nasty\")  
  - 9 `DMAEN` Master DMA enable (gates all below)  
  - 8 `BPLEN` Bitplane DMA enable  
  - 7 `COPEN` Copper DMA enable  
  - 6 `BLTEN` Blitter DMA enable  
  - 5 `SPREN` Sprite DMA enable  
  - 4 `DSKEN` Disk DMA enable  
  - 3 `AUD3EN` Audio channel 3 DMA enable  
  - 2 `AUD2EN` Audio channel 2 DMA enable  
  - 1 `AUD1EN` Audio channel 1 DMA enable  
  - 0 `AUD0EN` Audio channel 0 DMA enable  
- **Write guidance**: Use `SET/CLR` style (e.g., set bit15 and the channel bits to enable; clear bit15 and the channel bits to disable). Blitter status bits are read-only.
