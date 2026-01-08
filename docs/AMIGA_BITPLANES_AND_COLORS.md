Amiga Bitplanes and Color Registers (OCS/ECS)

This is a quick reference for how pixels are formed from bitplanes and color
registers on OCS/ECS (A500-class) hardware. It is intended to help with
capture/inspection tools and to avoid common misconceptions.

Summary
- Bitplanes are separate memory regions. Each plane contributes one bit per
  pixel at the same (x, y) coordinate.
- The combined bits form a color index, which selects a color register.
- Color registers are a palette, not interleaved pixel data.
- EHB and HAM are special modes that change how the bitplanes map to color.

Bitplane basics
- For each pixel, the hardware fetches one bit from each enabled bitplane.
- The bits are combined into an index value:
  - 1 plane: 1 bit -> 2 colors (COLOR00..COLOR01)
  - 2 planes: 2 bits -> 4 colors (COLOR00..COLOR03)
  - 3 planes: 3 bits -> 8 colors (COLOR00..COLOR07)
  - 4 planes: 4 bits -> 16 colors (COLOR00..COLOR15)
  - 5 planes: 5 bits -> 32 colors (COLOR00..COLOR31)
- The bit order is MSB = plane 5 (or plane N), LSB = plane 1.

Practical capture implications
- Each bitplane is a separate linear buffer in Chip RAM.
- To reconstruct pixels, read all enabled planes and combine bits at each
  (x, y) position.
- Palette lookup uses COLOR00..COLOR31 (for 1-5 planes).

Extra-Half-Brite (EHB)
- EHB uses 6 planes.
- Planes 1..5 form a 5-bit color index (0..31).
- Plane 6 is the half-bright control:
  - 0: use COLORxx normally.
  - 1: use half intensity of COLORxx.
- Only COLOR00..COLOR31 are defined; the extra 32 colors are derived.

Hold-And-Modify (HAM)
- HAM uses 6 planes (or 5 on some systems with the top plane assumed 0).
- Planes 5..6 are mode bits; planes 1..4 are data.
- For each pixel:
  - Mode 00: direct color via COLOR00..COLOR15 (planes 1..4).
  - Mode 01: modify blue of previous pixel with planes 1..4.
  - Mode 10: modify red of previous pixel with planes 1..4.
  - Mode 11: modify green of previous pixel with planes 1..4.
- The color of a HAM pixel depends on the pixel to its left.

Common misconception
- The color registers do not store interleaved pixel data. They store palette
  entries. Pixel data lives in the bitplanes only.

Related registers
- BPLCON0: sets plane count, HAM/EHB, hires/lores, interlace.
- BPLxPTH/BPLxPTL: bitplane pointers.
- BPL1MOD/BPL2MOD: modulo for odd/even planes.
- COLOR00..COLOR31: palette registers.

References
- ADCD 2.1 Hardware Manual and Includes/Autodocs (see
  `Hardware/ADCD_2.1/_txt/`).
