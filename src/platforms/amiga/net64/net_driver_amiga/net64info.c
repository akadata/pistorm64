/* net64info.c - simple Net64 config/info tool for AmigaOS
 *
 * Place the compiled binary in Tools: and run from Workbench or CLI.
 *
 * Shows:
 *   - net64.device status
 *   - MAC address from SANA-II
 *   - configured IP, netmask, gateway, DNS
 *
 * Edits:
 *   - DEVS:NetInterfaces/net64
 *   - DEVS:Internet/routes
 *   - DEVS:Internet/resolv.conf
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/io.h>
#include <exec/ports.h>

#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <libraries/gadtools.h>
#include <libraries/expansion.h>


#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/stdio.h>
#include <stdio.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/expansion.h>

#include "net64-sana2.h"
#include "net64-regs.h"

/* ------------------------------------------------------------------ */
/* Configuration paths and constants                                   */
/* ------------------------------------------------------------------ */

#define NETIF_PATH      "DEVS:NetInterfaces/net64"
#define ROUTES_PATH     "DEVS:Internet/routes"
#define RESOLV_PATH     "DEVS:Internet/resolv.conf"
#define NET64_DEVICE    "net64.device"

#define MAX_STR_LEN     64

enum {
    GID_MAC = 1,
    GID_LINK,
    GID_IP,
    GID_MASK,
    GID_GATEWAY,
    GID_DNS,
    GID_SAVE,
    GID_RELOAD,
    GID_QUIT
};

struct Net64Config {
    char mac_display[MAX_STR_LEN];
    char link_display[MAX_STR_LEN];
    char ip[MAX_STR_LEN];
    char mask[MAX_STR_LEN];
    char gw[MAX_STR_LEN];
    char dns[MAX_STR_LEN];
    char mac[32];      /* formatted MAC string */
    BOOL device_ok;
};

/* ------------------------------------------------------------------ */
/* Utility string helpers                                             */
/* ------------------------------------------------------------------ */

static void TrimCRLF(char *s) {
    char *p = s;

    while (*p != '\0') {
        p++;
    }

    while (p > s) {
        p--;
        if (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') {
            *p = '\0';
        } else {
            break;
        }
    }
}

static void CopySafe(char *dst, const char *src, LONG maxlen) {
    LONG i = 0;

    while (i < maxlen - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Simple starts-with test, case sensitive */
static BOOL StartsWith(const char *line, const char *prefix) {
    const char *p = line;
    const char *q = prefix;

    while (*q != '\0' && *p != '\0') {
        if (*p != *q) {
            return FALSE;
        }
        p++;
        q++;
    }

    if (*q == '\0') {
        return TRUE;
    }

    return FALSE;
}

/* After "key=" part, copy rest into dst */
static void ExtractAfterEquals(const char *line, const char *key, char *dst, LONG maxlen) {
    LONG klen = 0;
    const char *p = key;

    while (*p != '\0') {
        klen++;
        p++;
    }

    if (!StartsWith(line, key)) {
        dst[0] = '\0';
        return;
    }

    const char *v = line + klen;

    while (*v == ' ' || *v == '\t') {
        v++;
    }

    CopySafe(dst, v, maxlen);
}

/* After first space, copy remaining token */
static void ExtractAfterSpace(const char *line, char *dst, LONG maxlen) {
    const char *p = line;

    while (*p != '\0' && *p != ' ' && *p != '\t') {
        p++;
    }

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    CopySafe(dst, p, maxlen);
}

/* ------------------------------------------------------------------ */
/* SANA-II: query MAC from net64.device                                */
/* ------------------------------------------------------------------ */

static BOOL GetNet64MAC(char *out, LONG outlen) {
    struct MsgPort *port = NULL;
    struct IOSana2Req *ios2 = NULL;
    BOOL ok = FALSE;

    out[0] = '\0';

    port = CreateMsgPort();
    if (port == NULL) {
        return FALSE;
    }

    ios2 = (struct IOSana2Req *)CreateIORequest(port, sizeof(struct IOSana2Req));
    if (ios2 == NULL) {
        DeleteMsgPort(port);
        return FALSE;
    }

    if (OpenDevice((CONST_STRPTR)NET64_DEVICE, 0, (struct IORequest *)ios2, 0) == 0) {
        ios2->ios2_Req.io_Command = S2_GETSTATIONADDRESS;
        ios2->ios2_Req.io_Flags = 0;
        ios2->ios2_Data = NULL;
        ios2->ios2_StatData = NULL;

        DoIO((struct IORequest *)ios2);

        if (ios2->ios2_Req.io_Error == 0) {
            snprintf(out, outlen, "%02x:%02x:%02x:%02x:%02x:%02x",
                     (unsigned int)ios2->ios2_SrcAddr[0], (unsigned int)ios2->ios2_SrcAddr[1], (unsigned int)ios2->ios2_SrcAddr[2],
                     (unsigned int)ios2->ios2_SrcAddr[3], (unsigned int)ios2->ios2_SrcAddr[4], (unsigned int)ios2->ios2_SrcAddr[5]);

            ok = TRUE;
        }

        CloseDevice((struct IORequest *)ios2);
    }

    DeleteIORequest((struct IORequest *)ios2);
    DeleteMsgPort(port);

    return ok;
}

static ULONG find_net64_board_base(void) {
    ULONG board_addr = 0;
    struct Library *exp_lib = OpenLibrary((CONST_STRPTR)"expansion.library", 0L);

    if (exp_lib != NULL) {
        struct ExpansionBase *saved = ExpansionBase;
        struct ConfigDev *cd = NULL;

        ExpansionBase = (struct ExpansionBase *)exp_lib;
        while ((cd = (struct ConfigDev *)FindConfigDev(cd, NET64_VENDOR_ID, NET64_PRODUCT_ID)) != NULL) {
            if (cd->cd_BoardAddr != NULL) {
                board_addr = (ULONG)cd->cd_BoardAddr;
                break;
            }
        }

        ExpansionBase = saved;
        CloseLibrary(exp_lib);
    }

    return board_addr;
}

static BOOL ReadNet64HardwareStatus(char *mac_out, LONG mac_len, char *link_out, LONG link_len) {
    ULONG board = find_net64_board_base();
    volatile ULONG *regs = (volatile ULONG *)board;
    ULONG lo = 0;
    ULONG hi = 0;
    ULONG features = 0;
    ULONG link = 0;
    ULONG rx_pending = 0;
    ULONG mac0, mac1, mac2, mac3, mac4, mac5;

    if (board == 0) {
        return FALSE;
    }

    lo = regs[NET64_REG_MAC_LO / 4];
    hi = regs[NET64_REG_MAC_HI / 4];
    features = regs[NET64_REG_FEATURES / 4];
    link = regs[NET64_REG_LINK / 4];
    rx_pending = regs[0x0038 / 4];

    mac0 = (hi >> 8) & 0xFFu;
    mac1 = hi & 0xFFu;
    mac2 = (lo >> 24) & 0xFFu;
    mac3 = (lo >> 16) & 0xFFu;
    mac4 = (lo >> 8) & 0xFFu;
    mac5 = lo & 0xFFu;

    snprintf(mac_out, mac_len, "%02x:%02x:%02x:%02x:%02x:%02x",
             (unsigned int)mac0, (unsigned int)mac1, (unsigned int)mac2,
             (unsigned int)mac3, (unsigned int)mac4, (unsigned int)mac5);

    snprintf(link_out, link_len, "%s %luMb promisc=%lu rxq=%lu",
             (link & 0x80000000u) ? "UP" : "DOWN",
             (unsigned long)(link & 0x7FFFFFFFu),
             (unsigned long)((features & NET64_FEATURE_PROMISC) ? 1u : 0u),
             (unsigned long)rx_pending);

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Read configuration files                                           */
/* ------------------------------------------------------------------ */

static void InitDefaultConfig(struct Net64Config *cfg) {
    cfg->mac_display[0] = '\0';
    cfg->link_display[0] = '\0';
    cfg->ip[0] = '\0';
    cfg->mask[0] = '\0';
    cfg->gw[0] = '\0';
    cfg->dns[0] = '\0';
    cfg->mac[0] = '\0';
    cfg->device_ok = FALSE;
}

/* Read DEVS:NetInterfaces/net64 for address and netmask */
static VOID ReadInterfaceFile(struct Net64Config *cfg) {
    BPTR fh;
    char buf[256];

    fh = Open((CONST_STRPTR)NETIF_PATH, MODE_OLDFILE);
    if (fh == 0) {
        return;
    }

    while (FGets(fh, (STRPTR)buf, sizeof(buf)) != NULL) {
        TrimCRLF(buf);

        if (StartsWith(buf, "address=")) {
            ExtractAfterEquals(buf, "address=", cfg->ip, MAX_STR_LEN);
        } else if (StartsWith(buf, "netmask=")) {
            ExtractAfterEquals(buf, "netmask=", cfg->mask, MAX_STR_LEN);
        }
    }

    Close(fh);
}

/* Read DEVS:Internet/routes for default gateway */
static VOID ReadRoutesFile(struct Net64Config *cfg) {
    BPTR fh;
    char buf[256];

    fh = Open((CONST_STRPTR)ROUTES_PATH, MODE_OLDFILE);
    if (fh == 0) {
        return;
    }

    while (FGets(fh, (STRPTR)buf, sizeof(buf)) != NULL) {
        TrimCRLF(buf);

        if (StartsWith(buf, "default")) {
            ExtractAfterSpace(buf, cfg->gw, MAX_STR_LEN);
        } else if (StartsWith(buf, "nameserver")) {
            ExtractAfterSpace(buf, cfg->dns, MAX_STR_LEN);
        }
    }

    Close(fh);
}

/* Read DEVS:Internet/resolv.conf for nameserver */
static VOID ReadResolvFile(struct Net64Config *cfg) {
    BPTR fh;
    char buf[256];

    fh = Open((CONST_STRPTR)RESOLV_PATH, MODE_OLDFILE);
    if (fh == 0) {
        return;
    }

    while (FGets(fh, (STRPTR)buf, sizeof(buf)) != NULL) {
        TrimCRLF(buf);

        if (StartsWith(buf, "nameserver")) {
            ExtractAfterSpace(buf, cfg->dns, MAX_STR_LEN);
        }
    }

    Close(fh);
}

static VOID LoadConfig(struct Net64Config *cfg) {
    InitDefaultConfig(cfg);

    cfg->device_ok = GetNet64MAC(cfg->mac, sizeof(cfg->mac));

    ReadInterfaceFile(cfg);
    ReadRoutesFile(cfg);
    ReadResolvFile(cfg);

    if (!cfg->device_ok) {
        cfg->device_ok = ReadNet64HardwareStatus(cfg->mac, sizeof(cfg->mac), cfg->link_display, sizeof(cfg->link_display));
    } else {
        (void)ReadNet64HardwareStatus(cfg->mac_display, sizeof(cfg->mac_display), cfg->link_display, sizeof(cfg->link_display));
    }

    if (cfg->mac[0] != '\0') {
        CopySafe(cfg->mac_display, cfg->mac, MAX_STR_LEN);
    } else if (cfg->mac_display[0] == '\0') {
        CopySafe(cfg->mac_display, "Unavailable", MAX_STR_LEN);
    }

    if (cfg->link_display[0] == '\0') {
        CopySafe(cfg->link_display, "Unavailable", MAX_STR_LEN);
    }
}

/* ------------------------------------------------------------------ */
/* Write configuration files                                          */
/* ------------------------------------------------------------------ */

static BOOL WriteInterfaceFile(const struct Net64Config *cfg) {
    BPTR fh;
    char line[128];

    fh = Open((CONST_STRPTR)NETIF_PATH, MODE_NEWFILE);
    if (fh == 0) {
        return FALSE;
    }

    snprintf(line, sizeof(line), "device=%s\n", NET64_DEVICE);
    FPuts(fh, (CONST_STRPTR)line);

    snprintf(line, sizeof(line), "address=%s\n", cfg->ip[0] ? cfg->ip : "0.0.0.0");
    FPuts(fh, (CONST_STRPTR)line);

    snprintf(line, sizeof(line), "netmask=%s\n", cfg->mask[0] ? cfg->mask : "255.255.255.0");
    FPuts(fh, (CONST_STRPTR)line);

    Close(fh);
    return TRUE;
}

static BOOL WriteRoutesFile(const struct Net64Config *cfg) {
    BPTR fh;
    char line[128];

    fh = Open((CONST_STRPTR)ROUTES_PATH, MODE_NEWFILE);
    if (fh == 0) {
        return FALSE;
    }

    if (cfg->gw[0] != '\0') {
        snprintf(line, sizeof(line), "default %s\n", cfg->gw);
        FPuts(fh, (CONST_STRPTR)line);
    }
    if (cfg->dns[0] != '\0') {
        snprintf(line, sizeof(line), "nameserver %s\n", cfg->dns);
        FPuts(fh, (CONST_STRPTR)line);
    }

    Close(fh);
    return TRUE;
}

static BOOL WriteResolvFile(const struct Net64Config *cfg) {
    BPTR fh;
    char line[128];

    fh = Open((CONST_STRPTR)RESOLV_PATH, MODE_NEWFILE);
    if (fh == 0) {
        return FALSE;
    }

    if (cfg->dns[0] != '\0') {
        snprintf(line, sizeof(line), "nameserver %s\n", cfg->dns);
        FPuts(fh, (CONST_STRPTR)line);
    }

    Close(fh);
    return TRUE;
}

static BOOL SaveConfig(const struct Net64Config *cfg) {
    BOOL ok1 = WriteInterfaceFile(cfg);
    BOOL ok2 = WriteRoutesFile(cfg);
    BOOL ok3 = WriteResolvFile(cfg);

    return (ok1 && ok2 && ok3);
}

/* ------------------------------------------------------------------ */
/* GUI setup                                                           */
/* ------------------------------------------------------------------ */

struct Library *GadToolsBase = NULL;

static struct Window *OpenNet64Window(struct Screen *screen,
                                      struct Net64Config *cfg,
                                      struct Gadget **outGList,
                                      APTR *outVisualInfo,
                                      struct Gadget **outMAC,
                                      struct Gadget **outLink,
                                      struct Gadget **outIP,
                                      struct Gadget **outMask,
                                      struct Gadget **outGW,
                                      struct Gadget **outDNS,
                                      struct Gadget **outSave,
                                      struct Gadget **outReload,
                                      struct Gadget **outQuit) {
    struct Window *win = NULL;
    struct Gadget *glist = NULL;
    struct Gadget *gad = NULL;
    APTR vi = NULL;
    struct NewGadget ng;
    LONG left = 10;
    LONG top = 12;
    LONG gap = 14;

    vi = GetVisualInfoA(screen, NULL);
    if (vi == NULL) {
        return NULL;
    }

    gad = CreateContext(&glist);
    if (gad == NULL) {
        FreeVisualInfo(vi);
        return NULL;
    }

    /* IP */
    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = top;
    ng.ng_Width = 260;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (UBYTE *)"MAC";
    ng.ng_TextAttr = screen->Font;
    ng.ng_VisualInfo = vi;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_GadgetID = GID_MAC;
    ng.ng_UserData = NULL;

    gad = CreateGadget(STRING_KIND, gad, &ng,
                       GTST_MaxChars, MAX_STR_LEN - 1,
                       GTST_String, (ULONG)cfg->mac_display,
                       GA_Disabled, TRUE,
                       TAG_DONE);
    if (gad == NULL) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        return NULL;
    }
    *outMAC = gad;

    top += gap;

    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = top;
    ng.ng_Width = 260;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (UBYTE *)"Link";
    ng.ng_TextAttr = screen->Font;
    ng.ng_VisualInfo = vi;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_GadgetID = GID_LINK;
    ng.ng_UserData = NULL;

    gad = CreateGadget(STRING_KIND, gad, &ng,
                       GTST_MaxChars, MAX_STR_LEN - 1,
                       GTST_String, (ULONG)cfg->link_display,
                       GA_Disabled, TRUE,
                       TAG_DONE);
    if (gad == NULL) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        return NULL;
    }
    *outLink = gad;

    top += gap;

    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = top;
    ng.ng_Width = 260;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (UBYTE *)"IP Address";
    ng.ng_TextAttr = screen->Font;
    ng.ng_VisualInfo = vi;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_GadgetID = GID_IP;
    ng.ng_UserData = NULL;

    gad = CreateGadget(STRING_KIND, gad, &ng,
                       GTST_MaxChars, MAX_STR_LEN - 1,
                       GTST_String, (ULONG)cfg->ip,
                       TAG_DONE);
    if (gad == NULL) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        return NULL;
    }
    *outIP = gad;

    top += gap;

    /* Netmask */
    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = top;
    ng.ng_GadgetText = (UBYTE *)"Netmask";
    ng.ng_TextAttr = screen->Font;
    ng.ng_GadgetID = GID_MASK;

    gad = CreateGadget(STRING_KIND, gad, &ng,
                       GTST_MaxChars, MAX_STR_LEN - 1,
                       GTST_String, (ULONG)cfg->mask,
                       TAG_DONE);
    if (gad == NULL) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        return NULL;
    }
    *outMask = gad;

    top += gap;

    /* Gateway */
    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = top;
    ng.ng_GadgetText = (UBYTE *)"Gateway";
    ng.ng_TextAttr = screen->Font;
    ng.ng_GadgetID = GID_GATEWAY;

    gad = CreateGadget(STRING_KIND, gad, &ng,
                       GTST_MaxChars, MAX_STR_LEN - 1,
                       GTST_String, (ULONG)cfg->gw,
                       TAG_DONE);
    if (gad == NULL) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        return NULL;
    }
    *outGW = gad;

    top += gap;

    /* DNS */
    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = top;
    ng.ng_GadgetText = (UBYTE *)"DNS";
    ng.ng_TextAttr = screen->Font;
    ng.ng_GadgetID = GID_DNS;

    gad = CreateGadget(STRING_KIND, gad, &ng,
                       GTST_MaxChars, MAX_STR_LEN - 1,
                       GTST_String, (ULONG)cfg->dns,
                       TAG_DONE);
    if (gad == NULL) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        return NULL;
    }
    *outDNS = gad;

    top += gap + 8;

    /* Save button */
    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = top;
    ng.ng_Width = 60;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (UBYTE *)"Save";
    ng.ng_TextAttr = screen->Font;
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_GadgetID = GID_SAVE;

    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
    if (gad == NULL) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        return NULL;
    }
    *outSave = gad;

    /* Reload button */
    ng.ng_LeftEdge = left + 70;
    ng.ng_GadgetText = (UBYTE *)"Reload";
    ng.ng_TextAttr = screen->Font;
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_GadgetID = GID_RELOAD;

    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
    if (gad == NULL) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        return NULL;
    }
    *outReload = gad;

    /* Quit button */
    ng.ng_LeftEdge = left + 140;
    ng.ng_GadgetText = (UBYTE *)"Quit";
    ng.ng_TextAttr = screen->Font;
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_GadgetID = GID_QUIT;

    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
    if (gad == NULL) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        return NULL;
    }
    *outQuit = gad;

    win = OpenWindowTags(NULL,
                         WA_Left,         40,
                         WA_Top,          30,
                         WA_Width,        320,
                         WA_Height,       174,
                         WA_Title,        (ULONG)"Net64 Info",
                         WA_Activate,     TRUE,
                         WA_CloseGadget,  TRUE,
                         WA_DragBar,      TRUE,
                         WA_DepthGadget,  TRUE,
                         WA_SizeGadget,   FALSE,
                         WA_Gadgets,      (ULONG)glist,
                         WA_IDCMP,        IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW,
                         WA_Flags,        WFLG_SMART_REFRESH | WFLG_ACTIVATE | WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET,
                         TAG_DONE);

    if (win == NULL) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        return NULL;
    }

    *outGList = glist;
    *outVisualInfo = vi;

    return win;
}

/* ------------------------------------------------------------------ */
/* Sync gadgets <-> config                                            */
/* ------------------------------------------------------------------ */

static VOID UpdateConfigFromGadgets(struct Net64Config *cfg,
                                    struct Window *win,
                                    struct Gadget *macG,
                                    struct Gadget *linkG,
                                    struct Gadget *ipG,
                                    struct Gadget *maskG,
                                    struct Gadget *gwG,
                                    struct Gadget *dnsG) {
    struct StringInfo *si;
    (void)win;
    (void)macG;
    (void)linkG;

    si = (struct StringInfo *)ipG->SpecialInfo;
    CopySafe(cfg->ip, (char *)si->Buffer, MAX_STR_LEN);

    si = (struct StringInfo *)maskG->SpecialInfo;
    CopySafe(cfg->mask, (char *)si->Buffer, MAX_STR_LEN);

    si = (struct StringInfo *)gwG->SpecialInfo;
    CopySafe(cfg->gw, (char *)si->Buffer, MAX_STR_LEN);

    si = (struct StringInfo *)dnsG->SpecialInfo;
    CopySafe(cfg->dns, (char *)si->Buffer, MAX_STR_LEN);
}

static VOID UpdateGadgetsFromConfig(struct Net64Config *cfg,
                                    struct Window *win,
                                    struct Gadget *macG,
                                    struct Gadget *linkG,
                                    struct Gadget *ipG,
                                    struct Gadget *maskG,
                                    struct Gadget *gwG,
                                    struct Gadget *dnsG) {
    struct StringInfo *si;
    LONG maxchars;

    si = (struct StringInfo *)macG->SpecialInfo;
    maxchars = (LONG)si->MaxChars;
    if (maxchars > 0) {
        CopySafe((char *)si->Buffer, cfg->mac_display, maxchars);
    }
    RefreshGList(macG, win, NULL, 1);

    si = (struct StringInfo *)linkG->SpecialInfo;
    maxchars = (LONG)si->MaxChars;
    if (maxchars > 0) {
        CopySafe((char *)si->Buffer, cfg->link_display, maxchars);
    }
    RefreshGList(linkG, win, NULL, 1);

    si = (struct StringInfo *)ipG->SpecialInfo;
    maxchars = (LONG)si->MaxChars;
    if (maxchars > 0) {
        CopySafe((char *)si->Buffer, cfg->ip, maxchars);
    }
    RefreshGList(ipG, win, NULL, 1);

    si = (struct StringInfo *)maskG->SpecialInfo;
    maxchars = (LONG)si->MaxChars;
    if (maxchars > 0) {
        CopySafe((char *)si->Buffer, cfg->mask, maxchars);
    }
    RefreshGList(maskG, win, NULL, 1);

    si = (struct StringInfo *)gwG->SpecialInfo;
    maxchars = (LONG)si->MaxChars;
    if (maxchars > 0) {
        CopySafe((char *)si->Buffer, cfg->gw, maxchars);
    }
    RefreshGList(gwG, win, NULL, 1);

    si = (struct StringInfo *)dnsG->SpecialInfo;
    maxchars = (LONG)si->MaxChars;
    if (maxchars > 0) {
        CopySafe((char *)si->Buffer, cfg->dns, maxchars);
    }
    RefreshGList(dnsG, win, NULL, 1);
}

/* Simple title update showing device state + MAC */
static VOID UpdateWindowTitle(struct Window *win, const struct Net64Config *cfg) {
    char title[128];

    if (cfg->device_ok) {
        snprintf(title, sizeof(title), "Net64 Info - %s - MAC %s", NET64_DEVICE, cfg->mac);
    } else {
        snprintf(title, sizeof(title), "Net64 Info - %s unavailable", NET64_DEVICE);
    }

    SetWindowTitles(win, (UBYTE *)title, (UBYTE *)-1);
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    struct Net64Config cfg;
    struct Screen *scr = NULL;
    struct Window *win = NULL;
    struct Gadget *glist = NULL;
    APTR vi = NULL;

    struct Gadget *macG = NULL;
    struct Gadget *linkG = NULL;
    struct Gadget *ipG = NULL;
    struct Gadget *maskG = NULL;
    struct Gadget *gwG = NULL;
    struct Gadget *dnsG = NULL;
    struct Gadget *saveG = NULL;
    struct Gadget *reloadG = NULL;
    struct Gadget *quitG = NULL;

    struct IntuiMessage *msg;
    BOOL done = FALSE;

    LoadConfig(&cfg);

    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 37);
    if (GadToolsBase == NULL) {
        return RETURN_FAIL;
    }

    scr = LockPubScreen(NULL);
    if (scr == NULL) {
        CloseLibrary(GadToolsBase);
        return RETURN_FAIL;
    }

    win = OpenNet64Window(scr, &cfg,
                          &glist, &vi,
                          &macG, &linkG,
                          &ipG, &maskG, &gwG, &dnsG,
                          &saveG, &reloadG, &quitG);
    if (win == NULL) {
        UnlockPubScreen(NULL, scr);
        CloseLibrary(GadToolsBase);
        return RETURN_FAIL;
    }

    UpdateWindowTitle(win, &cfg);
    GT_RefreshWindow(win, NULL);

    while (!done) {
        Wait(1 << win->UserPort->mp_SigBit);

        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
            ULONG class = msg->Class;
            struct Gadget *g = (struct Gadget *)msg->IAddress;

            ReplyMsg((struct Message *)msg);

            switch (class) {
                case IDCMP_CLOSEWINDOW:
                    done = TRUE;
                    break;

                case IDCMP_GADGETUP:
                    switch (g->GadgetID) {
                        case GID_SAVE:
                            UpdateConfigFromGadgets(&cfg, win, macG, linkG, ipG, maskG, gwG, dnsG);
                            SaveConfig(&cfg);
                            break;

                        case GID_RELOAD:
                            LoadConfig(&cfg);
                            UpdateGadgetsFromConfig(&cfg, win, macG, linkG, ipG, maskG, gwG, dnsG);
                            UpdateWindowTitle(win, &cfg);
                            break;

                        case GID_QUIT:
                            done = TRUE;
                            break;

                        default:
                            break;
                    }
                    break;

                case IDCMP_REFRESHWINDOW:
                    GT_BeginRefresh(win);
                    GT_RefreshWindow(win, NULL);
                    GT_EndRefresh(win, TRUE);
                    break;

                default:
                    break;
            }
        }
    }

    CloseWindow(win);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    CloseLibrary(GadToolsBase);

    return RETURN_OK;
}
