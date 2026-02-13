// RTG64 P96 card driver scaffold (autoinit + detection first step)

#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/expansion.h>

#include "../../rtg/rtg_driver_amiga/boardinfo.h"
#include "boardinfo64.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#define DEVICE_VERSION 1
#define DEVICE_REVISION 0
#define DEVICE_PRIORITY 0
#define DEVICE_NAME "rtg64.card"
#define DEVICE_ID_STRING "RTG64 " __DATE__
#define RTG64_FAKE_VRAM_SIZE 0x00400000UL
#define RTG64_DEFAULT_CLOCK_HZ 100000000UL
#define RTG64_PROTOCOL_MAGIC 0x52544736UL

struct GFXBase {
  struct Library libNode;
  BPTR segList;
  struct ExecBase *sysBase;
};

static struct GFXBase *g_gfxbase = NULL;
struct ExecBase *SysBase = NULL;
struct ExpansionBase *ExpansionBase = NULL;

__saveds struct GFXBase *OpenLib(__REGA6(struct GFXBase *gfxbase));
BPTR __saveds CloseLib(__REGA6(struct GFXBase *gfxbase));
BPTR __saveds ExpungeLib(__REGA6(struct GFXBase *gfxbase));
ULONG ExtFuncLib(void);
__saveds struct GFXBase *InitLib(__REGA6(struct ExecBase *sysbase),
                                 __REGA0(BPTR seglist),
                                 __REGD0(struct GFXBase *gfxbase));

int FindCard(__REGA0(struct BoardInfo *b));
int InitCard(__REGA0(struct BoardInfo *b));
void SetDAC(__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
void SetGC(__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(BOOL border));
void SetPanning(__REGA0(struct BoardInfo *b),
                __REGA1(UBYTE *addr),
                __REGD0(UWORD width),
                __REGD1(WORD x_offset),
                __REGD2(WORD y_offset),
                __REGD7(RGBFTYPE format));
BOOL SetDisplay(__REGA0(struct BoardInfo *b), __REGD0(BOOL enabled));
BOOL SetSwitch(__REGA0(struct BoardInfo *b), __REGD0(BOOL enabled));
UWORD CalculateBytesPerRow(__REGA0(struct BoardInfo *b), __REGD0(UWORD width), __REGD7(RGBFTYPE format));
ULONG GetCompatibleFormats(__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
void FillRect(__REGA0(struct BoardInfo *b),
              __REGA1(struct RenderInfo *r),
              __REGD0(WORD x),
              __REGD1(WORD y),
              __REGD2(WORD w),
              __REGD3(WORD h),
              __REGD4(ULONG color),
              __REGD5(UBYTE mask),
              __REGD7(RGBFTYPE format));
void BlitRect(__REGA0(struct BoardInfo *b),
              __REGA1(struct RenderInfo *r),
              __REGD0(WORD x),
              __REGD1(WORD y),
              __REGD2(WORD dx),
              __REGD3(WORD dy),
              __REGD4(WORD w),
              __REGD5(WORD h),
              __REGD6(UBYTE mask),
              __REGD7(RGBFTYPE format));

int __attribute__((no_reorder)) _start(void) {
  return -1;
}

asm("romtag:                                    \n"
    "       dc.w    " XSTR(RTC_MATCHWORD) "     \n"
    "       dc.l    romtag                      \n"
    "       dc.l    endcode                     \n"
    "       dc.b    " XSTR(RTF_AUTOINIT) "      \n"
    "       dc.b    " XSTR(DEVICE_VERSION) "    \n"
    "       dc.b    " XSTR(NT_LIBRARY) "        \n"
    "       dc.b    " XSTR(DEVICE_PRIORITY) "   \n"
    "       dc.l    _device_name                \n"
    "       dc.l    _device_id_string           \n"
    "       dc.l    _auto_init_tables           \n"
    "endcode:                                   \n");

char device_name[] = DEVICE_NAME;
char device_id_string[] = DEVICE_ID_STRING;

__saveds struct GFXBase *InitLib(__REGA6(struct ExecBase *sysbase),
                                 __REGA0(BPTR seglist),
                                 __REGD0(struct GFXBase *gfxbase)) {
  g_gfxbase = gfxbase;
  SysBase = sysbase;
  g_gfxbase->sysBase = sysbase;
  g_gfxbase->segList = seglist;
  return g_gfxbase;
}

__saveds struct GFXBase *OpenLib(__REGA6(struct GFXBase *gfxbase)) {
  gfxbase->libNode.lib_OpenCnt++;
  gfxbase->libNode.lib_Flags &= ~LIBF_DELEXP;
  return gfxbase;
}

BPTR __saveds CloseLib(__REGA6(struct GFXBase *gfxbase)) {
  gfxbase->libNode.lib_OpenCnt--;
  if (!gfxbase->libNode.lib_OpenCnt && (gfxbase->libNode.lib_Flags & LIBF_DELEXP)) {
    return ExpungeLib(gfxbase);
  }
  return 0;
}

BPTR __saveds ExpungeLib(__REGA6(struct GFXBase *gfxbase)) {
  if (!gfxbase->libNode.lib_OpenCnt) {
    BPTR seglist = gfxbase->segList;
    ULONG negsize = gfxbase->libNode.lib_NegSize;
    ULONG possize = gfxbase->libNode.lib_PosSize;
    UBYTE *base = (UBYTE *)gfxbase - negsize;
    Remove((struct Node *)gfxbase);
    FreeMem(base, negsize + possize);
    return seglist;
  }
  gfxbase->libNode.lib_Flags |= LIBF_DELEXP;
  return 0;
}

ULONG ExtFuncLib(void) {
  return 0;
}

int __attribute__((used)) FindCard(__REGA0(struct BoardInfo *b)) {
  struct ConfigDev *cd;
  volatile ULONG *reg32;

  if (!b) {
    return 0;
  }

  ExpansionBase = (struct ExpansionBase *)OpenLibrary((STRPTR)"expansion.library", 0L);
  if (!ExpansionBase) {
    return 0;
  }

  cd = NULL;
  cd = FindConfigDev(cd, RTG64_MANUFACTURER, RTG64_PRODUCT);
  if (!cd) {
    CloseLibrary((struct Library *)ExpansionBase);
    ExpansionBase = NULL;
    return 0;
  }

  b->RegisterBase = (UBYTE *)cd->cd_BoardAddr;
  b->MemoryBase = (UBYTE *)cd->cd_BoardAddr + RTG64_MMIO_SIZE;
  b->MemorySize = RTG64_FAKE_VRAM_SIZE;
  b->CardData[0] = (ULONG)cd->cd_BoardAddr;
  b->CardData[1] = (ULONG)cd;

  reg32 = (volatile ULONG *)b->RegisterBase;
  if (reg32[0] != RTG64_PROTOCOL_MAGIC) {
    CloseLibrary((struct Library *)ExpansionBase);
    ExpansionBase = NULL;
    return 0;
  }

  CloseLibrary((struct Library *)ExpansionBase);
  ExpansionBase = NULL;
  return 1;
}

int __attribute__((used)) InitCard(__REGA0(struct BoardInfo *b)) {
  int i;

  if (!b) {
    return 0;
  }

  b->CardBase = (struct CardBase *)g_gfxbase;
  b->ExecBase = SysBase;
  b->BoardName = "PiStorm RTG64";
  b->BoardType = BT_uaegfx;
  b->PaletteChipType = PCT_Unknown;
  b->GraphicsControllerType = GCT_Unknown;
  b->Flags |= BIF_GRANTDIRECTACCESS;
  b->RGBFormats = RGBFF_CLUT | RGBFF_R5G6B5 | RGBFF_B8G8R8A8 | RGBFF_NONE;
  b->BitsPerCannon = 8;

  for (i = 0; i < MAXMODES; i++) {
    b->MaxHorValue[i] = 4096;
    b->MaxVerValue[i] = 2160;
    b->MaxHorResolution[i] = 4096;
    b->MaxVerResolution[i] = 2160;
    b->PixelClockCount[i] = 1;
  }
  b->MemoryClock = RTG64_DEFAULT_CLOCK_HZ;

  b->SetSwitch = (void *)SetSwitch;
  b->SetDAC = (void *)SetDAC;
  b->SetGC = (void *)SetGC;
  b->SetPanning = (void *)SetPanning;
  b->CalculateBytesPerRow = (void *)CalculateBytesPerRow;
  b->GetCompatibleFormats = (void *)GetCompatibleFormats;
  b->SetDisplay = (void *)SetDisplay;
  b->FillRect = (void *)FillRect;
  b->BlitRect = (void *)BlitRect;

  return 1;
}

void SetDAC(__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format)) {
  (void)b;
  (void)format;
}

void SetGC(__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(BOOL border)) {
  (void)mode_info;
  if (!b) {
    return;
  }
  b->Border = border;
}

void SetPanning(__REGA0(struct BoardInfo *b),
                __REGA1(UBYTE *addr),
                __REGD0(UWORD width),
                __REGD1(WORD x_offset),
                __REGD2(WORD y_offset),
                __REGD7(RGBFTYPE format)) {
  (void)addr;
  (void)width;
  (void)format;
  if (!b) {
    return;
  }
  b->XOffset = x_offset;
  b->YOffset = y_offset;
}

BOOL SetDisplay(__REGA0(struct BoardInfo *b), __REGD0(BOOL enabled)) {
  (void)b;
  return enabled;
}

BOOL SetSwitch(__REGA0(struct BoardInfo *b), __REGD0(BOOL enabled)) {
  (void)b;
  return enabled;
}

UWORD CalculateBytesPerRow(__REGA0(struct BoardInfo *b), __REGD0(UWORD width), __REGD7(RGBFTYPE format)) {
  (void)b;
  switch (format) {
  case RGBFB_CLUT:
    return width;
  case RGBFB_R5G6B5PC:
  case RGBFB_R5G5B5PC:
  case RGBFB_R5G6B5:
  case RGBFB_R5G5B5:
  case RGBFB_B5G6R5PC:
  case RGBFB_B5G5R5PC:
    return (UWORD)(width * 2u);
  default:
    return (UWORD)(width * 4u);
  }
}

ULONG GetCompatibleFormats(__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format)) {
  (void)format;
  if (!b) {
    return RGBFF_CLUT | RGBFF_R5G6B5 | RGBFF_B8G8R8A8;
  }
  return b->RGBFormats;
}

void FillRect(__REGA0(struct BoardInfo *b),
              __REGA1(struct RenderInfo *r),
              __REGD0(WORD x),
              __REGD1(WORD y),
              __REGD2(WORD w),
              __REGD3(WORD h),
              __REGD4(ULONG color),
              __REGD5(UBYTE mask),
              __REGD7(RGBFTYPE format)) {
  (void)b;
  (void)r;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)color;
  (void)mask;
  (void)format;
}

void BlitRect(__REGA0(struct BoardInfo *b),
              __REGA1(struct RenderInfo *r),
              __REGD0(WORD x),
              __REGD1(WORD y),
              __REGD2(WORD dx),
              __REGD3(WORD dy),
              __REGD4(WORD w),
              __REGD5(WORD h),
              __REGD6(UBYTE mask),
              __REGD7(RGBFTYPE format)) {
  (void)b;
  (void)r;
  (void)x;
  (void)y;
  (void)dx;
  (void)dy;
  (void)w;
  (void)h;
  (void)mask;
  (void)format;
}

static uint32_t device_vectors[] = {
    (uint32_t)OpenLib,
    (uint32_t)CloseLib,
    (uint32_t)ExpungeLib,
    0,
    (uint32_t)FindCard,
    (uint32_t)InitCard,
    (uint32_t)-1,
};

struct InitTable {
  ULONG LibBaseSize;
  APTR FunctionTable;
  APTR DataTable;
  APTR InitLibTable;
};

const uint32_t auto_init_tables[4] = {
    sizeof(struct GFXBase),
    (uint32_t)device_vectors,
    0,
    (uint32_t)InitLib,
};
