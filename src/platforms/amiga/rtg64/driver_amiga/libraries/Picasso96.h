#ifndef LIBRARIES_PICASSO96_H
#define LIBRARIES_PICASSO96_H

#include <exec/types.h>

typedef UWORD RGBFTYPE;

/* Minimal compatibility structs used by boardinfo.h */
struct RenderInfo {
  APTR Memory;
  WORD BytesPerRow;
  UWORD RGBFormat;
};

/* RGB format IDs used by P96 board drivers. */
#define RGBFB_NONE        0
#define RGBFB_CLUT        1
#define RGBFB_R5G6B5      2
#define RGBFB_R5G6B5PC    3
#define RGBFB_B5G6R5PC    4
#define RGBFB_R8G8B8      5
#define RGBFB_B8G8R8      6
#define RGBFB_A8R8G8B8    7
#define RGBFB_A8B8G8R8    8
#define RGBFB_R8G8B8A8    9
#define RGBFB_B8G8R8A8    10
#define RGBFB_R5G5B5      11
#define RGBFB_R5G5B5PC    12
#define RGBFB_B5G5R5PC    13

/* RGB format masks advertised by board drivers. */
#define RGBFF_NONE        (1UL << RGBFB_NONE)
#define RGBFF_CLUT        (1UL << RGBFB_CLUT)
#define RGBFF_R5G6B5      (1UL << RGBFB_R5G6B5)
#define RGBFF_R5G6B5PC    (1UL << RGBFB_R5G6B5PC)
#define RGBFF_B5G6R5PC    (1UL << RGBFB_B5G6R5PC)
#define RGBFF_R8G8B8      (1UL << RGBFB_R8G8B8)
#define RGBFF_B8G8R8      (1UL << RGBFB_B8G8R8)
#define RGBFF_A8R8G8B8    (1UL << RGBFB_A8R8G8B8)
#define RGBFF_A8B8G8R8    (1UL << RGBFB_A8B8G8R8)
#define RGBFF_R8G8B8A8    (1UL << RGBFB_R8G8B8A8)
#define RGBFF_B8G8R8A8    (1UL << RGBFB_B8G8R8A8)
#define RGBFF_R5G5B5      (1UL << RGBFB_R5G5B5)
#define RGBFF_R5G5B5PC    (1UL << RGBFB_R5G5B5PC)
#define RGBFF_B5G5R5PC    (1UL << RGBFB_B5G5R5PC)

#define RGBFF_CHUNKY RGBFF_CLUT
#define RGBFF_HICOLOR (RGBFF_R5G6B5 | RGBFF_R5G6B5PC | RGBFF_B5G6R5PC | RGBFF_R5G5B5 | \
                       RGBFF_R5G5B5PC | RGBFF_B5G5R5PC)
#define RGBFF_TRUECOLOR (RGBFF_R8G8B8 | RGBFF_B8G8R8 | RGBFF_R8G8B8A8 | RGBFF_B8G8R8A8)
#define RGBFF_TRUEALPHA (RGBFF_A8R8G8B8 | RGBFF_A8B8G8R8)

/* Private tag base used by boardinfo.h extensions. */
#ifndef P96BD_Dummy
#define P96BD_Dummy (0x90000000UL)
#endif

#endif
