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
#include <graphics/gfx.h>
#include <string.h>
#include <stdint.h>

#include "boardinfo.h"
#include "settings.h"
#include "rtg_enums.h"
#include "core-rtg.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#define DEVICE_VERSION 1
#define DEVICE_REVISION 1
#define DEVICE_PRIORITY 0
#define DEVICE_ID_STRING "$VER: core-rtg.card " XSTR(DEVICE_VERSION) "." XSTR(DEVICE_REVISION) " " DEVICE_DATE
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
static UWORD trace_budget = 0;
#define TRACE_BUDGET_MAX 0xFFFFu
static UBYTE *card_mem_base = NULL;
static ULONG card_mem_size = 0;
static ULONG card_mem_offset = 0;
static volatile ULONG *trace_regs = NULL;

static inline void TraceCmd(struct BoardInfo *b, ULONG code) {
    if (trace_budget >= TRACE_BUDGET_MAX)
        return;
    volatile ULONG *regs = NULL;
    if (b && b->RegisterBase) {
        regs = (volatile ULONG *)b->RegisterBase;
    } else if (trace_regs) {
        regs = trace_regs;
    }
    if (!regs) {
        return;
    }
    regs[CORE_RTG_REG_CMD / 4] = code;
    trace_budget++;
}

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
APTR AllocCardMem(__REGA0(struct BoardInfo *b), __REGD0(ULONG size), __REGD1(BOOL force), __REGD2(BOOL system));
BOOL FreeCardMem(__REGA0(struct BoardInfo *b), __REGA1(APTR membase));

LONG ResolvePixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG pixel_clock), __REGD7(RGBFTYPE format));
ULONG GetPixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG index), __REGD7(RGBFTYPE format));
void SetClock (__REGA0(struct BoardInfo *b));

void SetMemoryMode (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
void SetWriteMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask));
void SetClearMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask));
void SetReadPlane (__REGA0(struct BoardInfo *b), __REGD0(UBYTE plane));

void WaitVerticalSync (__REGA0(struct BoardInfo *b), __REGD0(BOOL toggle));
BOOL GetVSyncState(__REGA0(struct BoardInfo *b), __REGD0(BOOL toggle));
void WaitBlitter (__REGA0(struct BoardInfo *b));

void FillRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(ULONG color), __REGD5(UBYTE mask), __REGD7(RGBFTYPE format));
void InvertRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitRectNoMaskComplete (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *rs), __REGA2(struct RenderInfo *rt), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE minterm), __REGD7(RGBFTYPE format));
void BlitTemplate (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Template *t), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitPattern (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Pattern *p), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void DrawLine (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Line *l), __REGD0(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitPlanar2Chunky(__REGA0(struct BoardInfo *b), __REGA1(struct BitMap *bm), __REGA2(struct RenderInfo *r), __REGD0(SHORT x), __REGD1(SHORT y), __REGD2(SHORT dx), __REGD3(SHORT dy), __REGD4(SHORT w), __REGD5(SHORT h), __REGD6(UBYTE minterm), __REGD7(UBYTE mask));
void BlitPlanar2Direct(__REGA0(struct BoardInfo *b), __REGA1(struct BitMap *bm), __REGA2(struct RenderInfo *r), __REGA3(struct ColorIndexMapping *clut), __REGD0(SHORT x), __REGD1(SHORT y), __REGD2(SHORT dx), __REGD3(SHORT dy), __REGD4(SHORT w), __REGD5(SHORT h), __REGD6(UBYTE minterm), __REGD7(UBYTE mask));
void ScrollPlanar(__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(UWORD dx), __REGD1(UWORD dy), __REGD2(UWORD w), __REGD3(UWORD h), __REGD4(UWORD total_w), __REGD5(UWORD total_h), __REGD6(UBYTE mask));
void UpdatePlanar(__REGA0(struct BoardInfo *b), __REGA1(struct BitMap *bm), __REGA2(struct RenderInfo *r), __REGD0(SHORT x), __REGD1(SHORT y), __REGD2(SHORT w), __REGD3(SHORT h), __REGD4(UBYTE mask));

static inline UWORD rtg_bytes_per_pixel(RGBFTYPE format);
static inline ULONG rtg_mask_for_bpp(UWORD bpp);
static inline void rtg_store_pixel(UBYTE *dst, UWORD bpp, RGBFTYPE format, ULONG color);
static inline void rtg_xor_pixel(UBYTE *dst, UWORD bpp, ULONG mask);

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
    _gfxbase->libNode.lib_Version = DEVICE_VERSION;
    _gfxbase->libNode.lib_Revision = DEVICE_REVISION;
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
    b->MemorySpaceBase = b->MemoryBase;
    b->MemorySpaceSize = b->MemorySize;

    card_mem_base = (UBYTE *)b->MemoryBase;
    card_mem_size = b->MemorySize;
    card_mem_offset = 0;
    trace_regs = (volatile ULONG *)b->RegisterBase;

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

    /*
     * Picasso96 expects direct framebuffer access for normal Workbench
     * rendering paths (icons/menus/gadgets). Without this flag, rendering can
     * fall back to callback paths that don't end up touching VRAM.
     */
    b->Flags |= BIF_GRANTDIRECTACCESS | BIF_HARDWARESPRITE | BIF_FLICKERFIXER;
    b->RGBFormats =
        RGBFF_TRUEALPHA |
        RGBFF_TRUECOLOR |
        RGBFF_R5G6B5PC | RGBFF_R5G5B5PC | RGBFF_B5G6R5PC | RGBFF_B5G5R5PC |
        RGBFF_CLUT;
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
    b->MaxMemorySize = b->MemorySize;
    b->MaxChunkSize = b->MemorySize;

    /*
     * Keep Picasso96 default memory allocator behavior.
     * Forcing AllocCardMem() here moved internal/system allocations into VRAM
     * and correlated with missing icon/menu updates.
     */

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
    /*
     * Provide full hook table to Picasso96, but route operations through
     * default implementations for now.
     */
    b->FillRect = (void *)FillRect;
    b->InvertRect = (void *)InvertRect;
    b->BlitRect = (void *)BlitRect;
    b->BlitRectNoMaskComplete = (void *)BlitRectNoMaskComplete;
    b->BlitTemplate = (void *)BlitTemplate;
    b->BlitPattern = (void *)BlitPattern;
    b->DrawLine = (void *)DrawLine;
    b->ScrollPlanar = (void *)ScrollPlanar;
    b->UpdatePlanar = (void *)UpdatePlanar;
    b->BlitPlanar2Chunky = (void *)BlitPlanar2Chunky;
    b->BlitPlanar2Direct = (void *)BlitPlanar2Direct;

    /* Build signature for host-side log verification. */
    TraceCmd(b, 0xDEAD0001u);

    return 1;
}

void BlitPlanar2Chunky(__REGA0(struct BoardInfo *b), __REGA1(struct BitMap *bm), __REGA2(struct RenderInfo *r), __REGD0(SHORT x), __REGD1(SHORT y), __REGD2(SHORT dx), __REGD3(SHORT dy), __REGD4(SHORT w), __REGD5(SHORT h), __REGD6(UBYTE minterm), __REGD7(UBYTE mask)) {
    TraceCmd(b, 0x10000001);
    if (b && b->BlitPlanar2ChunkyDefault) {
        b->BlitPlanar2ChunkyDefault(b, bm, r, x, y, dx, dy, w, h, minterm, mask);
    }
}

void BlitPlanar2Direct(__REGA0(struct BoardInfo *b), __REGA1(struct BitMap *bm), __REGA2(struct RenderInfo *r), __REGA3(struct ColorIndexMapping *clut), __REGD0(SHORT x), __REGD1(SHORT y), __REGD2(SHORT dx), __REGD3(SHORT dy), __REGD4(SHORT w), __REGD5(SHORT h), __REGD6(UBYTE minterm), __REGD7(UBYTE mask)) {
    TraceCmd(b, 0x10000002);
    if (b && b->BlitPlanar2DirectDefault) {
        b->BlitPlanar2DirectDefault(b, bm, r, clut, x, y, dx, dy, w, h, minterm, mask);
    }
}

void ScrollPlanar(__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(UWORD dx), __REGD1(UWORD dy), __REGD2(UWORD w), __REGD3(UWORD h), __REGD4(UWORD total_w), __REGD5(UWORD total_h), __REGD6(UBYTE mask)) {
    TraceCmd(b, 0x10000003);
    if (b && b->ScrollPlanarDefault) {
        b->ScrollPlanarDefault(b, r, dx, dy, w, h, total_w, total_h, mask);
    }
}

void UpdatePlanar(__REGA0(struct BoardInfo *b), __REGA1(struct BitMap *bm), __REGA2(struct RenderInfo *r), __REGD0(SHORT x), __REGD1(SHORT y), __REGD2(SHORT w), __REGD3(SHORT h), __REGD4(UBYTE mask)) {
    TraceCmd(b, 0x10000004);
    if (b && b->UpdatePlanarDefault) {
        b->UpdatePlanarDefault(b, bm, r, x, y, w, h, mask);
    }
}

static inline UWORD rtg_bytes_per_pixel(RGBFTYPE format) {
    switch (format) {
    case RGBFB_CLUT:
        return 1;
    case RGBFB_R5G6B5PC: case RGBFB_R5G5B5PC:
    case RGBFB_R5G6B5:   case RGBFB_R5G5B5:
    case RGBFB_B5G6R5PC: case RGBFB_B5G5R5PC:
        return 2;
    case RGBFB_R8G8B8:
    case RGBFB_B8G8R8:
        return 3;
    case RGBFB_B8G8R8A8: case RGBFB_R8G8B8A8:
    case RGBFB_A8B8G8R8: case RGBFB_A8R8G8B8:
        return 4;
    default:
        return 0;
    }
}

static inline ULONG rtg_mask_for_bpp(UWORD bpp) {
    switch (bpp) {
    case 1: return 0xFFu;
    case 2: return 0xFFFFu;
    case 3: return 0xFFFFFFu;
    default: return 0xFFFFFFFFu;
    }
}

static inline void rtg_store_pixel(UBYTE *dst, UWORD bpp, RGBFTYPE format, ULONG color) {
    switch (bpp) {
    case 1:
        *dst = (UBYTE)color;
        return;
    case 2:
        *(UWORD *)dst = (UWORD)color;
        return;
    case 3:
        if (format == RGBFB_R8G8B8) {
            dst[0] = (UBYTE)(color >> 16);
            dst[1] = (UBYTE)(color >> 8);
            dst[2] = (UBYTE)(color);
        } else {
            dst[0] = (UBYTE)(color);
            dst[1] = (UBYTE)(color >> 8);
            dst[2] = (UBYTE)(color >> 16);
        }
        return;
    default:
        *(ULONG *)dst = color;
        return;
    }
}

static inline void rtg_xor_pixel(UBYTE *dst, UWORD bpp, ULONG mask) {
    switch (bpp) {
    case 1:
        *dst ^= (UBYTE)mask;
        return;
    case 2:
        *(UWORD *)dst ^= (UWORD)mask;
        return;
    case 3:
        dst[0] ^= (UBYTE)(mask >> 16);
        dst[1] ^= (UBYTE)(mask >> 8);
        dst[2] ^= (UBYTE)(mask);
        return;
    default:
        *(ULONG *)dst ^= mask;
        return;
    }
}

static __attribute__((unused)) void rtg_fill_rect(struct RenderInfo *r, WORD x, WORD y, WORD w, WORD h, ULONG color, RGBFTYPE format) {
    if (!r || !r->Memory || w <= 0 || h <= 0)
        return;
    if (format == RGBFB_NONE || format == RGBFB_PLANAR) {
        format = r->RGBFormat;
    }
    UWORD bpp = rtg_bytes_per_pixel(format);
    if (bpp == 0)
        return;
    UBYTE *base = (UBYTE *)r->Memory;
    ULONG pitch = (ULONG)r->BytesPerRow;
    UBYTE *row = base + (ULONG)y * pitch + (ULONG)x * bpp;

    if (bpp == 1) {
        UBYTE c = (UBYTE)color;
        for (WORD yy = 0; yy < h; yy++) {
            memset(row, c, (size_t)w);
            row += pitch;
        }
        return;
    }

    if (bpp == 2) {
        UWORD c = (UWORD)color;
        for (WORD yy = 0; yy < h; yy++) {
            UWORD *dst = (UWORD *)row;
            for (WORD xx = 0; xx < w; xx++) {
                dst[xx] = c;
            }
            row += pitch;
        }
        return;
    }

    if (bpp == 4) {
        ULONG c = color;
        for (WORD yy = 0; yy < h; yy++) {
            ULONG *dst = (ULONG *)row;
            for (WORD xx = 0; xx < w; xx++) {
                dst[xx] = c;
            }
            row += pitch;
        }
        return;
    }

    UBYTE cbytes[4];
    memcpy(cbytes, &color, sizeof(cbytes));
    for (WORD yy = 0; yy < h; yy++) {
        UBYTE *dst = row;
        for (WORD xx = 0; xx < w; xx++) {
            dst[0] = cbytes[1];
            dst[1] = cbytes[2];
            dst[2] = cbytes[3];
            dst += 3;
        }
        row += pitch;
    }
}

static __attribute__((unused)) void rtg_blit_rect(struct RenderInfo *r, WORD x, WORD y, WORD dx, WORD dy, WORD w, WORD h, RGBFTYPE format) {
    if (!r || !r->Memory || w <= 0 || h <= 0)
        return;
    if (format == RGBFB_NONE || format == RGBFB_PLANAR) {
        format = r->RGBFormat;
    }
    UWORD bpp = rtg_bytes_per_pixel(format);
    if (bpp == 0)
        return;
    UBYTE *base = (UBYTE *)r->Memory;
    ULONG pitch = (ULONG)r->BytesPerRow;
    ULONG row_bytes = (ULONG)w * bpp;

    if (dy > y) {
        for (WORD yy = h - 1; yy >= 0; yy--) {
            UBYTE *src = base + (ULONG)(y + yy) * pitch + (ULONG)x * bpp;
            UBYTE *dst = base + (ULONG)(dy + yy) * pitch + (ULONG)dx * bpp;
            memmove(dst, src, row_bytes);
            if (yy == 0) break;
        }
    } else {
        for (WORD yy = 0; yy < h; yy++) {
            UBYTE *src = base + (ULONG)(y + yy) * pitch + (ULONG)x * bpp;
            UBYTE *dst = base + (ULONG)(dy + yy) * pitch + (ULONG)dx * bpp;
            memmove(dst, src, row_bytes);
        }
    }
}

static __attribute__((unused)) void rtg_blit_rect_nomask(struct RenderInfo *rs, struct RenderInfo *rt, WORD x, WORD y, WORD dx, WORD dy,
                                 WORD w, WORD h, RGBFTYPE format) {
    if (!rs || !rt || !rs->Memory || !rt->Memory || w <= 0 || h <= 0)
        return;
    if (format == RGBFB_NONE || format == RGBFB_PLANAR) {
        format = rt->RGBFormat;
    }
    UWORD bpp = rtg_bytes_per_pixel(format);
    if (bpp == 0)
        return;
    UBYTE *src_base = (UBYTE *)rs->Memory;
    UBYTE *dst_base = (UBYTE *)rt->Memory;
    ULONG src_pitch = (ULONG)rs->BytesPerRow;
    ULONG dst_pitch = (ULONG)rt->BytesPerRow;
    ULONG row_bytes = (ULONG)w * bpp;

    for (WORD yy = 0; yy < h; yy++) {
        UBYTE *src = src_base + (ULONG)(y + yy) * src_pitch + (ULONG)x * bpp;
        UBYTE *dst = dst_base + (ULONG)(dy + yy) * dst_pitch + (ULONG)dx * bpp;
        memmove(dst, src, row_bytes);
    }
}

void FillRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(ULONG color), __REGD5(UBYTE mask), __REGD7(RGBFTYPE format)) {
    TraceCmd(b, 0x10000005);
    if (b && b->FillRectDefault) {
        b->FillRectDefault(b, r, x, y, w, h, color, mask, format);
    }
}

void InvertRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format)) {
    TraceCmd(b, 0x10000006);
    if (b && b->InvertRectDefault) {
        b->InvertRectDefault(b, r, x, y, w, h, mask, format);
    }
}

void BlitRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE mask), __REGD7(RGBFTYPE format)) {
    TraceCmd(b, 0x10000007);
    if (b && b->BlitRectDefault) {
        b->BlitRectDefault(b, r, x, y, dx, dy, w, h, mask, format);
    }
}

void BlitRectNoMaskComplete (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *rs), __REGA2(struct RenderInfo *rt), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE minterm), __REGD7(RGBFTYPE format)) {
    TraceCmd(b, 0x10000008);
    if (b && b->BlitRectNoMaskCompleteDefault) {
        b->BlitRectNoMaskCompleteDefault(b, rs, rt, x, y, dx, dy, w, h, minterm, format);
    }
}

void BlitTemplate (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Template *t), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format)) {
    TraceCmd(b, 0x10000009);
    if (b && b->BlitTemplateDefault) {
        b->BlitTemplateDefault(b, r, t, x, y, w, h, mask, format);
    }
}

void BlitPattern (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Pattern *p), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format)) {
    TraceCmd(b, 0x1000000A);
    if (b && b->BlitPatternDefault) {
        b->BlitPatternDefault(b, r, p, x, y, w, h, mask, format);
    }
}

void DrawLine (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Line *l), __REGD0(UBYTE mask), __REGD7(RGBFTYPE format)) {
    TraceCmd(b, 0x1000000B);
    if (b && b->DrawLineDefault) {
        b->DrawLineDefault(b, r, l, mask, format);
    }
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
    regs[CORE_RTG_REG_DISP_W / 4] = mode_info->Width;
    regs[CORE_RTG_REG_DISP_H / 4] = mode_info->Height;
    regs[CORE_RTG_REG_SCALE_X / 4] = 0x00010000u;
    regs[CORE_RTG_REG_SCALE_Y / 4] = 0x00010000u;
    TraceCmd(b, 0x11000001u);
}

void SetColorArray (__REGA0(struct BoardInfo *b), __REGD0(UWORD start), __REGD1(UWORD num)) {
    if (!b || !b->CLUT)
        return;

    volatile uint32_t *regs = (volatile uint32_t *)b->RegisterBase;
    UWORD end = start + num;
    for (UWORD i = start; i < end; i++) {
        ULONG xrgb = (b->CLUT[i].Red << 16) | (b->CLUT[i].Green << 8) | (b->CLUT[i].Blue);
        regs[CORE_RTG_REG_CLUT_INDEX / 4] = i;
        regs[CORE_RTG_REG_CLUT_RGB / 4] = xrgb;
    }
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
    ULONG bytes_per_row = CalculateBytesPerRow(b, width, format);
    ULONG bytes_per_pix = bytes_per_row / width;
    ULONG base = (ULONG)b->MemoryBase;
    ULONG a = (ULONG)addr;
    if (a < base) {
        a += base;
    }
    TraceCmd(b, 0xC1000000u | ((a >> 16) & 0xFFFFu));
    TraceCmd(b, 0xC2000000u | (a & 0xFFFFu));
    TraceCmd(b, 0xC3000000u | (width & 0xFFFFu));
    UBYTE *start = (UBYTE *)a + (y_offset * bytes_per_row) + (x_offset * bytes_per_pix);

    volatile uint32_t *regs = (volatile uint32_t *)b->RegisterBase;
    regs[CORE_RTG_REG_FB_ADDR / 4] = (uint32_t)start;
    regs[CORE_RTG_REG_FB_PITCH / 4] = bytes_per_row;
    regs[CORE_RTG_REG_FB_FORMAT / 4] = rgbf_to_rtg[format];
    regs[CORE_RTG_REG_PAN_X / 4] = (uint16_t)b->XOffset;
    regs[CORE_RTG_REG_PAN_Y / 4] = (uint16_t)b->YOffset;
}

UWORD SetDisplay (__REGA0(struct BoardInfo *b), __REGD0(UWORD enabled)) {
    (void)b;
    volatile uint32_t *regs = (volatile uint32_t *)b->RegisterBase;
    regs[CORE_RTG_REG_STATUS / 4] = enabled ? 1u : 0u;
    TraceCmd(b, 0x11000002u | (enabled & 1u));
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
    TraceCmd(b, 0x11000010u | (enabled & 1u));
    return 1 - enabled;
}

APTR CalculateMemory (__REGA0(struct BoardInfo *b), __REGA1(unsigned long addr), __REGD7(RGBFTYPE format)) {
    (void)format;
    if (!b) {
        return (APTR)addr;
    }
    ULONG base = (ULONG)b->MemoryBase;
    ULONG a = (ULONG)addr;
    if (a < base) {
        a += base;
    }
    return (APTR)a;
}

APTR AllocCardMem(__REGA0(struct BoardInfo *b), __REGD0(ULONG size), __REGD1(BOOL force), __REGD2(BOOL system)) {
    (void)force;
    if (b) {
        TraceCmd(b, (system ? 0xA1000000u : 0xA0000000u) | ((force ? 1u : 0u) << 16) | (size & 0xFFFFu));
    }
    if (!card_mem_base || card_mem_size == 0 || size == 0) {
        return NULL;
    }
    ULONG aligned = (size + 15) & ~15UL;
    if (card_mem_offset + aligned > card_mem_size) {
        return NULL;
    }
    UBYTE *ptr = card_mem_base + card_mem_offset;
    card_mem_offset += aligned;
    if (b) {
        uint32_t p = (uint32_t)ptr;
        TraceCmd(b, 0xA2000000u | ((p >> 16) & 0xFFFFu));
        TraceCmd(b, 0xA3000000u | (p & 0xFFFFu));
    }
    return (APTR)ptr;
}

BOOL FreeCardMem(__REGA0(struct BoardInfo *b), __REGA1(APTR membase)) {
    (void)b;
    (void)membase;
    return TRUE;
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

void WaitBlitter (__REGA0(struct BoardInfo *b)) {
    TraceCmd(b, 0x1000000C);
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
