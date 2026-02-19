// SPDX-License-Identifier: MIT

#include <proto/exec.h>
#include <proto/expansion.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/resident.h>
#include <exec/initializers.h>
#include <clib/debug_protos.h>
#include <string.h>
#include <stdint.h>

#include "boardinfo.h"
#include "settings.h"
#include "rtg_enums.h"
#include "core-rtg.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#define DEVICE_VERSION 1
#define DEVICE_REVISION 0
#define DEVICE_PRIORITY 0
#define DEVICE_ID_STRING "PiStorm CoreRTG " XSTR(DEVICE_VERSION) "." XSTR(DEVICE_REVISION)
#define DEVICE_NAME "core-rtg.card"
#define DEVICE_DATE "(19 Feb 2026)"

struct GFXBase {
    struct Library libNode;
    BPTR segList;
    struct ExecBase* sysBase;
    struct ExpansionBase* expansionBase;
};

#define __saveds__
#define kprintf(...)

struct ExecBase *SysBase;
static struct GFXBase *_gfxbase;

int FindCard(__REGA0(struct BoardInfo* b));
int InitCard(__REGA0(struct BoardInfo* b));

void SetDAC (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
void SetGC (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(BOOL border));
void SetColorArray (__REGA0(struct BoardInfo *b), __REGD0(UWORD start), __REGD1(UWORD num));
void SetPanning (__REGA0(struct BoardInfo *b), __REGA1(UBYTE *addr), __REGD0(UWORD width), __REGD1(WORD x_offset), __REGD2(WORD y_offset), __REGD7(RGBFTYPE format));
UWORD SetSwitch (__REGA0(struct BoardInfo *b), __REGD0(UWORD enabled));
UWORD SetDisplay (__REGA0(struct BoardInfo *b), __REGD0(UWORD enabled));

UWORD CalculateBytesPerRow (__REGA0(struct BoardInfo *b), __REGD0(UWORD width), __REGD7(RGBFTYPE format));
APTR CalculateMemory (__REGA0(struct BoardInfo *b), __REGA1(unsigned long addr), __REGD7(RGBFTYPE format));
ULONG GetCompatibleFormats (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));

LONG ResolvePixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG pixel_clock), __REGD7(RGBFTYPE format));
ULONG GetPixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG index), __REGD7(RGBFTYPE format));
void SetClock (__REGA0(struct BoardInfo *b));

void SetMemoryMode (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
void SetWriteMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask));
void SetClearMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask));
void SetReadPlane (__REGA0(struct BoardInfo *b), __REGD0(UBYTE plane));

void WaitVerticalSync (__REGA0(struct BoardInfo *b), __REGD0(BOOL toggle));
BOOL GetVSyncState(__REGA0(struct BoardInfo *b), __REGD0(BOOL toggle));

void FillRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(ULONG color), __REGD5(UBYTE mask), __REGD7(RGBFTYPE format));
void InvertRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitRectNoMaskComplete (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *rs), __REGA2(struct RenderInfo *rt), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE minterm), __REGD7(RGBFTYPE format));
void BlitTemplate (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Template *t), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitPattern (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Pattern *p), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void DrawLine (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Line *l), __REGD0(UBYTE mask), __REGD7(RGBFTYPE format));

#define LOADLIB(a, b) if ((a = (struct a*)OpenLibrary((STRPTR)b,0L))==NULL) { \
        kprintf((STRPTR)"Failed to load %s.\n", b); \
        return 0; \
    } \

int __attribute__((no_reorder)) _start()
{
        return -1;
}

asm("romtag:                                    \n"
        "       dc.w    "XSTR(RTC_MATCHWORD)"   \n"
        "       dc.l    romtag                  \n"
        "       dc.l    endcode                 \n"
        "       dc.b    "XSTR(RTF_AUTOINIT)"    \n"
        "       dc.b    "XSTR(DEVICE_VERSION)"  \n"
        "       dc.b    "XSTR(NT_LIBRARY)"      \n"
        "       dc.b    "XSTR(DEVICE_PRIORITY)" \n"
        "       dc.l    _device_name            \n"
        "       dc.l    _device_id_string       \n"
        "       dc.l    _auto_init_tables       \n"
        "endcode:                               \n");

char device_name[] = DEVICE_NAME;
char device_id_string[] = DEVICE_ID_STRING;

__saveds struct GFXBase* OpenLib(__REGA6(struct GFXBase *gfxbase));
BPTR __saveds CloseLib(__REGA6(struct GFXBase *gfxbase));
BPTR __saveds ExpungeLib(__REGA6(struct GFXBase *exb));
ULONG ExtFuncLib(void);
__saveds struct GFXBase* InitLib(__REGA6(struct ExecBase *sysbase),
                                                                 __REGA0(BPTR seglist),
                                                                 __REGD0(struct GFXBase *exb));

__saveds struct GFXBase* __attribute__((used)) InitLib(__REGA6(struct ExecBase *sysbase),
                                                       __REGA0(BPTR seglist),
                                                       __REGD0(struct GFXBase *exb))
{
    _gfxbase = exb;
    SysBase = *(struct ExecBase **)4L;
    return _gfxbase;
}

__saveds struct GFXBase* __attribute__((used)) OpenLib(__REGA6(struct GFXBase *gfxbase))
{
    gfxbase->libNode.lib_OpenCnt++;
    gfxbase->libNode.lib_Flags &= ~LIBF_DELEXP;
    return gfxbase;
}

BPTR __saveds __attribute__((used)) CloseLib(__REGA6(struct GFXBase *gfxbase))
{
    gfxbase->libNode.lib_OpenCnt--;
    if (!gfxbase->libNode.lib_OpenCnt) {
        if (gfxbase->libNode.lib_Flags & LIBF_DELEXP) {
            return (ExpungeLib(gfxbase));
        }
    }
    return 0;
}

BPTR __saveds __attribute__((used)) ExpungeLib(__REGA6(struct GFXBase *exb))
{
    BPTR seglist;
    struct ExecBase *SysBase = *(struct ExecBase **)4L;

    if(!exb->libNode.lib_OpenCnt) {
        ULONG negsize, possize, fullsize;
        UBYTE *negptr = (UBYTE *)exb;

        seglist = exb->segList;

        Remove((struct Node *)exb);

        negsize  = exb->libNode.lib_NegSize;
        possize  = exb->libNode.lib_PosSize;
        fullsize = negsize + possize;
        negptr  -= negsize;

        FreeMem(negptr, fullsize);
        return(seglist);
    }

    exb->libNode.lib_Flags |= LIBF_DELEXP;
    return 0;
}

ULONG ExtFuncLib(void)
{
    return 0;
}

int __attribute__((used)) FindCard(__REGA0(struct BoardInfo* b)) {
    struct ConfigDev* cd = NULL;
    struct ExpansionBase *ExpansionBase = NULL;
    struct DOSBase *DOSBase = NULL;
    struct IntuitionBase *IntuitionBase = NULL;

    LOADLIB(ExpansionBase, "expansion.library");
    LOADLIB(DOSBase, "dos.library");
    LOADLIB(IntuitionBase, "intuition.library");

    cd = (struct ConfigDev*)FindConfigDev(cd, 0x07DB, 0x0041);
    if (!cd) {
        cd = (struct ConfigDev*)FindConfigDev(cd, 0x07DB, 0x0040);
    }
    if (!cd) {
        return 0;
    }

    b->RegisterBase = (void *)(cd->cd_BoardAddr);
    b->MemoryBase = (void *)(cd->cd_BoardAddr + CORE_RTG_REG_SIZE);
    b->MemorySize = cd->cd_BoardSize - CORE_RTG_REG_SIZE;

    return 1;
}

int __attribute__((used)) InitCard(__REGA0(struct BoardInfo* b)) {
    int i;

    b->CardBase = (struct CardBase *)_gfxbase;
    b->ExecBase = SysBase;
    b->BoardName = "PiStorm CoreRTG";
    b->BoardType = BT_PiStormCore;
    b->PaletteChipType = PCT_S3ViRGE;
    b->GraphicsControllerType = GCT_S3ViRGE;

    b->Flags |= BIF_GRANTDIRECTACCESS | BIF_HARDWARESPRITE | BIF_FLICKERFIXER;
    b->RGBFormats = RGBFF_HICOLOR | RGBFF_TRUECOLOR | RGBFF_TRUEALPHA | RGBFF_CLUT | RGBFF_NONE;
    b->SoftSpriteFlags = 0;
    b->BitsPerCannon = 8;

    for(i = 0; i < MAXMODES; i++) {
        b->MaxHorValue[i] = 8192;
        b->MaxVerValue[i] = 8192;
        b->MaxHorResolution[i] = 8192;
        b->MaxVerResolution[i] = 8192;
        b->PixelClockCount[i] = 1;
    }

    b->MemoryClock = 100000000;

    b->SetSwitch = (void *)SetSwitch;
    b->SetColorArray = (void *)SetColorArray;
    b->SetDAC = (void *)SetDAC;
    b->SetGC = (void *)SetGC;
    b->SetPanning = (void *)SetPanning;
    b->CalculateBytesPerRow = (void *)CalculateBytesPerRow;
    b->CalculateMemory = (void *)CalculateMemory;
    b->GetCompatibleFormats = (void *)GetCompatibleFormats;
    b->SetDisplay = (void *)SetDisplay;

    b->ResolvePixelClock = (void *)ResolvePixelClock;
    b->GetPixelClock = (void *)GetPixelClock;
    b->SetClock = (void *)SetClock;

    b->SetMemoryMode = (void *)SetMemoryMode;
    b->SetWriteMask = (void *)SetWriteMask;
    b->SetClearMask = (void *)SetClearMask;
    b->SetReadPlane = (void *)SetReadPlane;

    b->WaitVerticalSync = (void *)WaitVerticalSync;
    b->GetVSyncState = (void *)GetVSyncState;

    b->FillRect = (void *)FillRect;
    b->InvertRect = (void *)InvertRect;
    b->BlitRect = (void *)BlitRect;
    b->BlitTemplate = (void *)BlitTemplate;
    b->BlitPattern = (void *)BlitPattern;
    b->DrawLine = (void *)DrawLine;
    b->BlitRectNoMaskComplete = (void *)BlitRectNoMaskComplete;

    return 1;
}

void SetDAC (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format)) {
    (void)b;
    (void)format;
}

void SetGC (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(BOOL border)) {
    (void)border;
    if (!b || !mode_info) {
        return;
    }
    b->ModeInfo = mode_info;
    volatile uint32_t *regs = (volatile uint32_t *)b->RegisterBase;
    regs[CORE_RTG_REG_FB_WIDTH / 4] = mode_info->Width;
    regs[CORE_RTG_REG_FB_HEIGHT / 4] = mode_info->Height;
    regs[CORE_RTG_REG_FB_FORMAT / 4] = rgbf_to_rtg[b->RGBFormat];
}

void SetColorArray (__REGA0(struct BoardInfo *b), __REGD0(UWORD start), __REGD1(UWORD num)) {
    (void)b;
    (void)start;
    (void)num;
}

UWORD CalculateBytesPerRow(__REGA0(struct BoardInfo *b),
                           __REGD0(UWORD width),
                           __REGD7(RGBFTYPE format))
{
    if (!b)
        return 0;
    switch (format) {
    case RGBFB_CLUT:
        return width;
    case RGBFB_R5G6B5PC: case RGBFB_R5G5B5PC:
    case RGBFB_R5G6B5:   case RGBFB_R5G5B5:
    case RGBFB_B5G6R5PC: case RGBFB_B5G5R5PC:
        return (UWORD)(width * 2);
    case RGBFB_R8G8B8:
    case RGBFB_B8G8R8:
        return (UWORD)(width * 3);
    case RGBFB_B8G8R8A8: case RGBFB_R8G8B8A8:
    case RGBFB_A8B8G8R8: case RGBFB_A8R8G8B8:
        return (UWORD)(width * 4);
    default:
        return (UWORD)(width * 2);
    }
}

void SetPanning (__REGA0(struct BoardInfo *b), __REGA1(UBYTE *addr), __REGD0(UWORD width), __REGD1(WORD x_offset), __REGD2(WORD y_offset), __REGD7(RGBFTYPE format)) {
    if (!b)
        return;
    b->XOffset = x_offset;
    b->YOffset = y_offset;

    volatile uint32_t *regs = (volatile uint32_t *)b->RegisterBase;
    regs[CORE_RTG_REG_FB_ADDR / 4] = (uint32_t)addr;
    regs[CORE_RTG_REG_FB_PITCH / 4] = CalculateBytesPerRow(b, width, format);
    regs[CORE_RTG_REG_PAN_X / 4] = (uint16_t)b->XOffset;
    regs[CORE_RTG_REG_PAN_Y / 4] = (uint16_t)b->YOffset;
}

UWORD SetDisplay (__REGA0(struct BoardInfo *b), __REGD0(UWORD enabled)) {
    (void)b;
    volatile uint32_t *regs = (volatile uint32_t *)b->RegisterBase;
    regs[CORE_RTG_REG_STATUS / 4] = enabled ? 1u : 0u;
    return 1;
}

int setswitch = -1;
UWORD SetSwitch (__REGA0(struct BoardInfo *b), __REGD0(UWORD enabled)) {
    (void)b;
    if (setswitch != enabled) {
        setswitch = enabled;
    }
    volatile uint32_t *regs = (volatile uint32_t *)b->RegisterBase;
    regs[CORE_RTG_REG_STATUS / 4] = (uint32_t)(setswitch & 1);
    return 1 - enabled;
}

APTR CalculateMemory (__REGA0(struct BoardInfo *b), __REGA1(unsigned long addr), __REGD7(RGBFTYPE format)) {
    (void)b;
    (void)format;
    return (APTR)addr;
}

ULONG GetCompatibleFormats (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format)) {
    (void)b;
    (void)format;
    return 0xFFFFFFFF;
}

LONG ResolvePixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG pixel_clock), __REGD7(RGBFTYPE format)) {
    (void)b;
    (void)pixel_clock;
    (void)format;
    mode_info->PixelClock = 100000000;
    mode_info->pll1.Clock = 0;
    mode_info->pll2.ClockDivide = 1;
    return 0;
}

ULONG GetPixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG index), __REGD7(RGBFTYPE format)) {
    (void)b;
    (void)mode_info;
    (void)index;
    (void)format;
    return 100000000;
}

void SetClock (__REGA0(struct BoardInfo *b)) {
    (void)b;
}

void SetMemoryMode (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format)) {
    (void)b;
    (void)format;
}

void SetWriteMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask)) {
    (void)b;
    (void)mask;
}

void SetClearMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask)) {
    (void)b;
    (void)mask;
}

void SetReadPlane (__REGA0(struct BoardInfo *b), __REGD0(UBYTE plane)) {
    (void)b;
    (void)plane;
}

void WaitVerticalSync (__REGA0(struct BoardInfo *b), __REGD0(BOOL toggle)) {
    (void)b;
    (void)toggle;
}

BOOL GetVSyncState(__REGA0(struct BoardInfo *b), __REGD0(BOOL toggle)) {
    (void)b;
    (void)toggle;
    return 1;
}

void FillRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(ULONG color), __REGD5(UBYTE mask), __REGD7(RGBFTYPE format)) {
    (void)b; (void)r; (void)x; (void)y; (void)w; (void)h; (void)color; (void)mask; (void)format;
}

void InvertRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format)) {
    (void)b; (void)r; (void)x; (void)y; (void)w; (void)h; (void)mask; (void)format;
}

void BlitRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE mask), __REGD7(RGBFTYPE format)) {
    (void)b; (void)r; (void)x; (void)y; (void)dx; (void)dy; (void)w; (void)h; (void)mask; (void)format;
}

void BlitRectNoMaskComplete (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *rs), __REGA2(struct RenderInfo *rt), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE minterm), __REGD7(RGBFTYPE format)) {
    (void)b; (void)rs; (void)rt; (void)x; (void)y; (void)dx; (void)dy; (void)w; (void)h; (void)minterm; (void)format;
}

void BlitTemplate (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Template *t), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format)) {
    (void)b; (void)r; (void)t; (void)x; (void)y; (void)w; (void)h; (void)mask; (void)format;
}

void BlitPattern (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Pattern *p), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format)) {
    (void)b; (void)r; (void)p; (void)x; (void)y; (void)w; (void)h; (void)mask; (void)format;
}

void DrawLine (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Line *l), __REGD0(UBYTE mask), __REGD7(RGBFTYPE format)) {
    (void)b; (void)r; (void)l; (void)mask; (void)format;
}

static uint32_t device_vectors[] = {
    (uint32_t)OpenLib,
    (uint32_t)CloseLib,
    (uint32_t)ExpungeLib,
    0,
    (uint32_t)FindCard,
    (uint32_t)InitCard,
    -1
};

struct InitTable
{
    ULONG LibBaseSize;
    APTR  FunctionTable;
    APTR  DataTable;
    APTR  InitLibTable;
};

const uint32_t auto_init_tables[4] __attribute__((used)) = {
    sizeof(struct GFXBase),
    (uint32_t)device_vectors,
    0,
    (uint32_t)InitLib,
};
