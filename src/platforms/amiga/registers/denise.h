// SPDX-License-Identifier: MIT
// DENISE chip register definitions for Amiga
// Based on Amiga Hardware Reference Manual (ADCD 2.1)

#ifndef DENISE_H
#define DENISE_H

#include "amiga_custom_chips.h"

// Input Sampling Registers
#define JOY0DAT  (DENISE_BASE + 0x00A)  // Joystick/mouse 0 (vertical/horizontal counters)
#define JOY1DAT  (DENISE_BASE + 0x00C)  // Joystick/mouse 1 (vertical/horizontal counters)

// Collision Detection Registers
#define CLXDAT   (DENISE_BASE + 0x00E)  // Collision data register (read and clear)
#define CLXCON   (DENISE_BASE + 0x098)  // Collision control register

// Sprite Registers (Pointers in AGNUS, Position/Data in DENISE)
#define SPR0POS  (DENISE_BASE + 0x140)  // Sprite 0 position
#define SPR0CTL  (DENISE_BASE + 0x142)  // Sprite 0 control
#define SPR0DATA (DENISE_BASE + 0x144)  // Sprite 0 data
#define SPR0DATB (DENISE_BASE + 0x146)  // Sprite 0 data B

#define SPR1POS  (DENISE_BASE + 0x148)  // Sprite 1 position
#define SPR1CTL  (DENISE_BASE + 0x14A)  // Sprite 1 control
#define SPR1DATA (DENISE_BASE + 0x14C)  // Sprite 1 data
#define SPR1DATB (DENISE_BASE + 0x14E)  // Sprite 1 data B

#define SPR2POS  (DENISE_BASE + 0x150)  // Sprite 2 position
#define SPR2CTL  (DENISE_BASE + 0x152)  // Sprite 2 control
#define SPR2DATA (DENISE_BASE + 0x154)  // Sprite 2 data
#define SPR2DATB (DENISE_BASE + 0x156)  // Sprite 2 data B

#define SPR3POS  (DENISE_BASE + 0x158)  // Sprite 3 position
#define SPR3CTL  (DENISE_BASE + 0x15A)  // Sprite 3 control
#define SPR3DATA (DENISE_BASE + 0x15C)  // Sprite 3 data
#define SPR3DATB (DENISE_BASE + 0x15E)  // Sprite 3 data B

#define SPR4POS  (DENISE_BASE + 0x160)  // Sprite 4 position
#define SPR4CTL  (DENISE_BASE + 0x162)  // Sprite 4 control
#define SPR4DATA (DENISE_BASE + 0x164)  // Sprite 4 data
#define SPR4DATB (DENISE_BASE + 0x166)  // Sprite 4 data B

#define SPR5POS  (DENISE_BASE + 0x168)  // Sprite 5 position
#define SPR5CTL  (DENISE_BASE + 0x16A)  // Sprite 5 control
#define SPR5DATA (DENISE_BASE + 0x16C)  // Sprite 5 data
#define SPR5DATB (DENISE_BASE + 0x16E)  // Sprite 5 data B

#define SPR6POS  (DENISE_BASE + 0x170)  // Sprite 6 position
#define SPR6CTL  (DENISE_BASE + 0x172)  // Sprite 6 control
#define SPR6DATA (DENISE_BASE + 0x174)  // Sprite 6 data
#define SPR6DATB (DENISE_BASE + 0x176)  // Sprite 6 data B

#define SPR7POS  (DENISE_BASE + 0x178)  // Sprite 7 position
#define SPR7CTL  (DENISE_BASE + 0x17A)  // Sprite 7 control
#define SPR7DATA (DENISE_BASE + 0x17C)  // Sprite 7 data
#define SPR7DATB (DENISE_BASE + 0x17E)  // Sprite 7 data B

// Color Palette Registers
#define COLOR00  (DENISE_BASE + 0x180)  // Color 00
#define COLOR01  (DENISE_BASE + 0x182)  // Color 01
#define COLOR02  (DENISE_BASE + 0x184)  // Color 02
#define COLOR03  (DENISE_BASE + 0x186)  // Color 03
#define COLOR04  (DENISE_BASE + 0x188)  // Color 04
#define COLOR05  (DENISE_BASE + 0x18A)  // Color 05
#define COLOR06  (DENISE_BASE + 0x18C)  // Color 06
#define COLOR07  (DENISE_BASE + 0x18E)  // Color 07
#define COLOR08  (DENISE_BASE + 0x190)  // Color 08
#define COLOR09  (DENISE_BASE + 0x192)  // Color 09
#define COLOR10  (DENISE_BASE + 0x194)  // Color 10
#define COLOR11  (DENISE_BASE + 0x196)  // Color 11
#define COLOR12  (DENISE_BASE + 0x198)  // Color 12
#define COLOR13  (DENISE_BASE + 0x19A)  // Color 13
#define COLOR14  (DENISE_BASE + 0x19C)  // Color 14
#define COLOR15  (DENISE_BASE + 0x19E)  // Color 15
#define COLOR16  (DENISE_BASE + 0x1A0)  // Color 16
#define COLOR17  (DENISE_BASE + 0x1A2)  // Color 17
#define COLOR18  (DENISE_BASE + 0x1A4)  // Color 18
#define COLOR19  (DENISE_BASE + 0x1A6)  // Color 19
#define COLOR20  (DENISE_BASE + 0x1A8)  // Color 20
#define COLOR21  (DENISE_BASE + 0x1AA)  // Color 21
#define COLOR22  (DENISE_BASE + 0x1AC)  // Color 22
#define COLOR23  (DENISE_BASE + 0x1AE)  // Color 23
#define COLOR24  (DENISE_BASE + 0x1B0)  // Color 24
#define COLOR25  (DENISE_BASE + 0x1B2)  // Color 25
#define COLOR26  (DENISE_BASE + 0x1B4)  // Color 26
#define COLOR27  (DENISE_BASE + 0x1B6)  // Color 27
#define COLOR28  (DENISE_BASE + 0x1B8)  // Color 28
#define COLOR29  (DENISE_BASE + 0x1BA)  // Color 29
#define COLOR30  (DENISE_BASE + 0x1BC)  // Color 30
#define COLOR31  (DENISE_BASE + 0x1BE)  // Color 31

// Denise Revision Register
#define DENISEID (DENISE_BASE + 0x07C)  // Denise chip revision level (read-only)

// Sprite Control Register Bits (SPRxCTL)
#define SPRCTL_ATTACH 0x0080  // Attach to next sprite
#define SPRCTL_VSTART0 0x0001  // Vertical start bit 0
#define SPRCTL_VSTART1 0x0002  // Vertical start bit 1
#define SPRCTL_VSTART2 0x0004  // Vertical start bit 2
#define SPRCTL_VSTART3 0x0008  // Vertical start bit 3
#define SPRCTL_VSTART4 0x0010  // Vertical start bit 4
#define SPRCTL_VSTART5 0x0020  // Vertical start bit 5
#define SPRCTL_VSTART6 0x0040  // Vertical start bit 6
#define SPRCTL_VSTOP0  0x0100  // Vertical stop bit 0
#define SPRCTL_VSTOP1  0x0200  // Vertical stop bit 1
#define SPRCTL_VSTOP2  0x0400  // Vertical stop bit 2
#define SPRCTL_VSTOP3  0x0800  // Vertical stop bit 3
#define SPRCTL_VSTOP4  0x1000  // Vertical stop bit 4
#define SPRCTL_VSTOP5  0x2000  // Vertical stop bit 5
#define SPRCTL_VSTOP6  0x4000  // Vertical stop bit 6

// Sprite Position Register Bits (SPRxPOS)
#define SPRPOS_HSTART0  0x0001  // Horizontal start bit 0
#define SPRPOS_HSTART1  0x0002  // Horizontal start bit 1
#define SPRPOS_HSTART2  0x0004  // Horizontal start bit 2
#define SPRPOS_HSTART3  0x0008  // Horizontal start bit 3
#define SPRPOS_HSTART4  0x0010  // Horizontal start bit 4
#define SPRPOS_HSTART5  0x0020  // Horizontal start bit 5
#define SPRPOS_HSTART6  0x0040  // Horizontal start bit 6
#define SPRPOS_HSTART7  0x0080  // Horizontal start bit 7
#define SPRPOS_HSTART8  0x0100  // Horizontal start bit 8
#define SPRPOS_SPRVSTART0 0x0200  // Sprite vertical start bit 0
#define SPRPOS_SPRVSTART1 0x0400  // Sprite vertical start bit 1
#define SPRPOS_SPRVSTART2 0x0800  // Sprite vertical start bit 2
#define SPRPOS_SPRVSTART3 0x1000  // Sprite vertical start bit 3
#define SPRPOS_SPRVSTART4 0x2000  // Sprite vertical start bit 4
#define SPRPOS_SPRVSTART5 0x4000  // Sprite vertical start bit 5
#define SPRPOS_SPRVSTART6 0x8000  // Sprite vertical start bit 6

// Collision Control Register Bits (CLXCON)
#define CLXCON_COLLISION_ENABLE 0x007F  // Collision enable mask (bits 0-6)
#define CLXCON_COLLISION_MODE   0x7F80  // Collision mode mask (bits 7-14)

// Collision Data Register Bits (CLXDAT)
#define CLXDAT_SPRITE_COLLISION 0x007F  // Sprite collision bits (bits 0-6)
#define CLXDAT_BITPLANE_COLLISION 0x7F80  // Bitplane collision bits (bits 7-14)

// Collision bit definitions
#define CLXBIT_PLANE0   0x0001  // Bitplane 0 collision
#define CLXBIT_PLANE1   0x0002  // Bitplane 1 collision
#define CLXBIT_PLANE2   0x0004  // Bitplane 2 collision
#define CLXBIT_PLANE3   0x0008  // Bitplane 3 collision
#define CLXBIT_PLANE4   0x0010  // Bitplane 4 collision
#define CLXBIT_PLANE5   0x0020  // Bitplane 5 collision
#define CLXBIT_SPR0     0x0040  // Sprite 0 collision
#define CLXBIT_SPR1     0x0080  // Sprite 1 collision
#define CLXBIT_SPR2     0x0100  // Sprite 2 collision
#define CLXBIT_SPR3     0x0200  // Sprite 3 collision
#define CLXBIT_SPR4     0x0400  // Sprite 4 collision
#define CLXBIT_SPR5     0x0800  // Sprite 5 collision
#define CLXBIT_SPR6     0x1000  // Sprite 6 collision
#define CLXBIT_SPR7     0x2000  // Sprite 7 collision
#define CLXBIT_PLN01    0x4000  // Bitplane 0-1 collision
#define CLXBIT_PLN23    0x8000  // Bitplane 2-3 collision

#endif // DENISE_H
