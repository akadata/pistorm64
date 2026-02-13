#ifndef RTG64_BOARDINFO_H
#define RTG64_BOARDINFO_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#if defined(__has_include)
#if __has_include(<libraries/Picasso96.h>)
#include <libraries/Picasso96.h>
#define RTG64_HAVE_P96 1
#endif
#endif

#ifndef RTG64_HAVE_P96
typedef UWORD RGBFTYPE;
struct BoardInfo;
struct ModeInfo;
struct RenderInfo;
#ifndef RGBFF_CLUT
#define RGBFF_CLUT 0x00000001UL
#endif
#ifndef RGBFF_R5G6B5
#define RGBFF_R5G6B5 0x00000002UL
#endif
#ifndef RGBFF_B8G8R8A8
#define RGBFF_B8G8R8A8 0x00000004UL
#endif
#endif

/*
 * Minimal RTG64 board definition used by the scaffold card driver.
 * The real driver will populate this from ConfigDev and runtime probing.
 */
#define RTG64_MANUFACTURER 2011
#define RTG64_PRODUCT      0x0042

#define RTG64_MMIO_SIZE    0x00010000UL

enum rtg64_format {
  RTG64_FMT_CLUT8 = 1,
  RTG64_FMT_RGB565 = 2,
  RTG64_FMT_XRGB8888 = 3,
  RTG64_FMT_ARGB8888 = 4,
};

typedef struct rtg64_board {
  ULONG board_base;
  ULONG mmio_base;
  ULONG fb_base;
  ULONG fb_size;
  UWORD width;
  UWORD height;
  UWORD format;
  UWORD reserved;
  ULONG stride;
  ULONG flags;
} rtg64_board_t;

#endif
