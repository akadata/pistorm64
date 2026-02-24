// SPDX-License-Identifier: MIT

#include <exec/alerts.h>
#include <exec/devices.h>
#include <exec/errors.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/resident.h>
#include <exec/tasks.h>

#include <libraries/expansion.h>

#include <proto/exec.h>
#include <proto/expansion.h>

#include <clib/alib_protos.h>

#include <string.h>
#include <stdint.h>

#include "net64-regs.h"
#include "net64-sana2.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#define DEVICE_NAME "net64.device"
#define DEVICE_DATE "(13 Feb 2026)"
#define DEVICE_ID_STRING "net64.device 0.1 " DEVICE_DATE
#define DEVICE_VERSION 0
#define DEVICE_REVISION 1
#define DEVICE_PRIORITY 0
#define NET64_TRACKED_TYPES_MAX 32

#define kprintf(...)

struct ExecBase *SysBase = NULL;

struct BufferManagement {
  struct MinNode bm_Node;
  BOOL (*bm_CopyFromBuffer)(void *a __asm("a0"), void *b __asm("a1"), long c __asm("d0"));
  BOOL (*bm_CopyToBuffer)(void *a __asm("a0"), void *b __asm("a1"), long c __asm("d0"));
};

typedef struct net64_unit {
  struct Unit unit;
  struct List pending_onevent;
  ULONG board_base;
  UBYTE station_addr[6];
  ULONG online;
  ULONG connected;
  ULONG promisc;
  ULONG configured;
  ULONG rx_packets;
  ULONG tx_packets;
  ULONG rx_dropped;
  ULONG event_flags;
  ULONG tracked_count;
  ULONG tracked_types[NET64_TRACKED_TYPES_MAX];
  UBYTE tx_bounce[NET64_RAW_MTU];
  UBYTE rx_bounce[NET64_RAW_MTU];
} net64_unit_t;

typedef struct net64_base {
  struct Device *device;
  net64_unit_t unit;
} net64_base_t;

static net64_base_t *g_base = NULL;

#define WRITELONG(reg, val) *((volatile ULONG *)(g_base->unit.board_base + (reg))) = (ULONG)(val)
#define WRITEWORD(reg, val) *((volatile UWORD *)(g_base->unit.board_base + (reg))) = (UWORD)(val)
#define WRITEBYTE(reg, val) *((volatile UBYTE *)(g_base->unit.board_base + (reg))) = (UBYTE)(val)

#define READLONG(reg, out) out = *((volatile ULONG *)(g_base->unit.board_base + (reg)))
#define READWORD(reg, out) out = *((volatile UWORD *)(g_base->unit.board_base + (reg)))
#define READBYTE(reg, out) out = *((volatile UBYTE *)(g_base->unit.board_base + (reg)));

static void complete_io(struct IOSana2Req *ios2) {
  if (ios2->ios2_Req.io_Flags & SANA2IOF_QUICK) {
    ios2->ios2_Req.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
  } else {
    ReplyMsg(&ios2->ios2_Req.io_Message);
  }
}

static void raise_events(ULONG bits) {
  struct Node *node = NULL;
  ULONG pending_mask = 0;

  if (bits == 0) {
    return;
  }

  g_base->unit.event_flags |= bits;

  node = g_base->unit.pending_onevent.lh_Head;
  while (node != NULL && node->ln_Succ != NULL) {
    struct Node *next = node->ln_Succ;
    struct IOSana2Req *req = (struct IOSana2Req *)node;
    ULONG wanted = req->ios2_WireError & S2EVENT_ALL;
    ULONG hit = g_base->unit.event_flags & wanted;
    if (hit != 0) {
      Remove(node);
      req->ios2_Req.io_Error = S2ERR_NO_ERROR;
      req->ios2_WireError = hit;
      complete_io(req);
    } else {
      pending_mask |= wanted;
    }
    node = next;
  }

  g_base->unit.event_flags &= pending_mask;
}

static ULONG queue_onevent_request(struct IOSana2Req *ios2) {
  ULONG wanted = ios2->ios2_WireError;
  ULONG invalid = wanted & ~S2EVENT_ALL;
  ULONG hit = 0;

  if (wanted == 0 || invalid != 0) {
    ios2->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
    ios2->ios2_WireError = S2WERR_BAD_EVENT;
    return 0;
  }

  wanted &= S2EVENT_ALL;
  hit = g_base->unit.event_flags & wanted;
  if (hit != 0) {
    ios2->ios2_Req.io_Error = S2ERR_NO_ERROR;
    ios2->ios2_WireError = hit;
    g_base->unit.event_flags &= ~hit;
    return 0;
  }

  ios2->ios2_WireError = wanted;
  AddTail(&g_base->unit.pending_onevent, &ios2->ios2_Req.io_Message.mn_Node);
  return 1;
}

static ULONG find_net64_board_base(void) {
  ULONG board_addr = 0;
  struct Library *exp_lib = OpenLibrary((STRPTR)"expansion.library", 0L);
  if (exp_lib != NULL) {
    struct ExpansionBase *saved = ExpansionBase;
    ExpansionBase = (struct ExpansionBase *)exp_lib;

    struct ConfigDev *cd = NULL;
    while ((cd = (struct ConfigDev *)FindConfigDev(cd, NET64_VENDOR_ID, NET64_PRODUCT_ID)) != NULL) {
      if (cd->cd_BoardAddr != NULL) {
        board_addr = (ULONG)(uintptr_t)cd->cd_BoardAddr;
        break;
      }
    }

    ExpansionBase = saved;
    CloseLibrary(exp_lib);
  }

  if (board_addr == 0) {
    board_addr = NET64_DEFAULT_BASE;
  }

  return board_addr;
}

static void read_station_address(UBYTE mac[6]) {
  ULONG lo = 0;
  ULONG hi = 0;
  READLONG(NET64_REG_MAC_LO, lo);
  READLONG(NET64_REG_MAC_HI, hi);

  mac[0] = (UBYTE)((hi >> 8) & 0xFF);
  mac[1] = (UBYTE)(hi & 0xFF);
  mac[2] = (UBYTE)((lo >> 24) & 0xFF);
  mac[3] = (UBYTE)((lo >> 16) & 0xFF);
  mac[4] = (UBYTE)((lo >> 8) & 0xFF);
  mac[5] = (UBYTE)(lo & 0xFF);
}

static void write_station_address(const UBYTE mac[6]) {
  ULONG lo = ((ULONG)mac[2] << 24) | ((ULONG)mac[3] << 16) | ((ULONG)mac[4] << 8) | (ULONG)mac[5];
  ULONG hi = ((ULONG)mac[0] << 8) | (ULONG)mac[1];

  WRITELONG(NET64_REG_MAC_LO, lo);
  WRITELONG(NET64_REG_MAC_HI, hi);
  WRITELONG(NET64_REG_CMD, NET64_CMD_APPLY_CFG);
}

static void apply_promisc(ULONG enabled) {
  ULONG features = 0;
  READLONG(NET64_REG_FEATURES, features);
  if (enabled) {
    features |= NET64_FEATURE_PROMISC;
  } else {
    features &= ~NET64_FEATURE_PROMISC;
  }
  WRITELONG(NET64_REG_FEATURES, features);
  WRITELONG(NET64_REG_CMD, NET64_CMD_APPLY_CFG);
}

static void fill_device_query(struct IOSana2Req *ios2) {
  struct Sana2DeviceQuery *query = (struct Sana2DeviceQuery *)ios2->ios2_StatData;
  if (query == NULL) {
    ios2->ios2_Req.io_Error = S2ERR_BAD_ARGUMENT;
    ios2->ios2_WireError = S2WERR_GENERIC_ERROR;
    return;
  }

  query->DevQueryFormat = 0;
  query->DeviceLevel = 0;

  if (query->SizeAvailable >= 18) {
    query->AddrFieldSize = 48;
  }
  if (query->SizeAvailable >= 22) {
    query->MTU = NET64_ETH_MTU;
  }
  if (query->SizeAvailable >= 26) {
    query->BPS = 1000000000UL;
  }
  if (query->SizeAvailable >= 30) {
    query->HardwareType = S2WireType_Ethernet;
  }

  query->SizeSupplied = (query->SizeAvailable < 30) ? query->SizeAvailable : 30;
}

static void fill_global_stats(struct IOSana2Req *ios2) {
  struct Sana2DeviceStats *stats = (struct Sana2DeviceStats *)ios2->ios2_StatData;
  if (stats == NULL) {
    ios2->ios2_Req.io_Error = S2ERR_BAD_ARGUMENT;
    return;
  }

  memset(stats, 0, sizeof(*stats));
  stats->PacketsReceived = g_base->unit.rx_packets;
  stats->PacketsSent = g_base->unit.tx_packets;
  stats->Overruns = g_base->unit.rx_dropped;
}

static ULONG is_tracked_type(ULONG ptype) {
  ULONG i = 0;
  for (i = 0; i < g_base->unit.tracked_count; i++) {
    if (g_base->unit.tracked_types[i] == ptype) {
      return 1;
    }
  }
  return 0;
}

static ULONG add_tracked_type(ULONG ptype) {
  if (is_tracked_type(ptype)) {
    return S2WERR_ALREADY_TRACKED;
  }
  if (g_base->unit.tracked_count >= NET64_TRACKED_TYPES_MAX) {
    return S2WERR_GENERIC_ERROR;
  }
  g_base->unit.tracked_types[g_base->unit.tracked_count++] = ptype;
  return 0;
}

static ULONG remove_tracked_type(ULONG ptype) {
  ULONG i = 0;
  for (i = 0; i < g_base->unit.tracked_count; i++) {
    if (g_base->unit.tracked_types[i] == ptype) {
      ULONG j = i;
      while (j + 1 < g_base->unit.tracked_count) {
        g_base->unit.tracked_types[j] = g_base->unit.tracked_types[j + 1];
        j++;
      }
      g_base->unit.tracked_count--;
      return 0;
    }
  }
  return S2WERR_NOT_TRACKED;
}

static ULONG net64_write_frame(struct IOSana2Req *ios2) {
  ULONG frame_len = 0;
  UBYTE *frame = g_base->unit.tx_bounce;
  struct BufferManagement *bm = (struct BufferManagement *)ios2->ios2_BufferManagement;

  if (bm == NULL || bm->bm_CopyFromBuffer == NULL) {
    ios2->ios2_Req.io_Error = S2ERR_BAD_ARGUMENT;
    ios2->ios2_WireError = S2WERR_BUFF_ERROR;
    return 1;
  }

  if (ios2->ios2_Req.io_Flags & SANA2IOF_RAW) {
    frame_len = ios2->ios2_DataLength;
    if (frame_len > NET64_RAW_MTU) {
      ios2->ios2_Req.io_Error = S2ERR_MTU_EXCEEDED;
      return 1;
    }
    if (!bm->bm_CopyFromBuffer(frame, ios2->ios2_Data, frame_len)) {
      ios2->ios2_Req.io_Error = S2ERR_SOFTWARE;
      ios2->ios2_WireError = S2WERR_BUFF_ERROR;
      return 1;
    }
  } else {
    frame_len = ios2->ios2_DataLength + 14;
    if (frame_len > NET64_RAW_MTU) {
      ios2->ios2_Req.io_Error = S2ERR_MTU_EXCEEDED;
      return 1;
    }

    memcpy(frame + 0, ios2->ios2_DstAddr, 6);
    memcpy(frame + 6, g_base->unit.station_addr, 6);
    frame[12] = (UBYTE)((ios2->ios2_PacketType >> 8) & 0xFF);
    frame[13] = (UBYTE)(ios2->ios2_PacketType & 0xFF);

    if (!bm->bm_CopyFromBuffer(frame + 14, ios2->ios2_Data, ios2->ios2_DataLength)) {
      ios2->ios2_Req.io_Error = S2ERR_SOFTWARE;
      ios2->ios2_WireError = S2WERR_BUFF_ERROR;
      return 1;
    }
  }

  WRITELONG(NET64_REG_TX_ADDR, (ULONG)(uintptr_t)frame);
  WRITELONG(NET64_REG_TX_LEN, frame_len);
  WRITELONG(NET64_REG_CMD, NET64_CMD_TX_KICK);

  g_base->unit.tx_packets++;
  raise_events(S2EVENT_TX);
  return 0;
}

static ULONG net64_read_frame(struct IOSana2Req *ios2) {
  UBYTE *frame = g_base->unit.rx_bounce;
  struct BufferManagement *bm = (struct BufferManagement *)ios2->ios2_BufferManagement;
  ULONG frame_len = 0;

  if (bm == NULL || bm->bm_CopyToBuffer == NULL) {
    ios2->ios2_Req.io_Error = S2ERR_BAD_ARGUMENT;
    ios2->ios2_WireError = S2WERR_BUFF_ERROR;
    return 1;
  }

  WRITELONG(NET64_REG_RX_ADDR, (ULONG)(uintptr_t)frame);
  WRITELONG(NET64_REG_RX_LEN, NET64_RAW_MTU);
  WRITELONG(NET64_REG_CMD, NET64_CMD_RX_POP);
  READLONG(NET64_REG_RX_ACTUAL, frame_len);

  if (frame_len == 0) {
    ios2->ios2_Req.io_Error = S2ERR_NO_ERROR;
    ios2->ios2_WireError = S2WERR_GENERIC_ERROR;
    ios2->ios2_DataLength = 0;
    return 0;
  }

  if (frame_len < 14 || frame_len > NET64_RAW_MTU) {
    g_base->unit.rx_dropped++;
    ios2->ios2_Req.io_Error = S2ERR_SOFTWARE;
    ios2->ios2_WireError = S2WERR_GENERIC_ERROR;
    return 1;
  }

  memcpy(ios2->ios2_DstAddr, frame + 0, 6);
  memcpy(ios2->ios2_SrcAddr, frame + 6, 6);
  ios2->ios2_PacketType = ((ULONG)frame[12] << 8) | (ULONG)frame[13];

  if (ios2->ios2_Req.io_Flags & SANA2IOF_RAW) {
    ios2->ios2_DataLength = frame_len;
    ios2->ios2_Req.io_Flags |= ((frame[0] == 0xFF && frame[1] == 0xFF && frame[2] == 0xFF &&
                                 frame[3] == 0xFF && frame[4] == 0xFF && frame[5] == 0xFF)
                                    ? SANA2IOF_BCAST
                                    : 0);
    if (!bm->bm_CopyToBuffer(ios2->ios2_Data, frame, frame_len)) {
      ios2->ios2_Req.io_Error = S2ERR_SOFTWARE;
      ios2->ios2_WireError = S2WERR_BUFF_ERROR;
      return 1;
    }
  } else {
    ULONG payload_len = frame_len - 14;
    ios2->ios2_DataLength = payload_len;
    if (!bm->bm_CopyToBuffer(ios2->ios2_Data, frame + 14, payload_len)) {
      ios2->ios2_Req.io_Error = S2ERR_SOFTWARE;
      ios2->ios2_WireError = S2WERR_BUFF_ERROR;
      return 1;
    }
  }

  g_base->unit.rx_packets++;
  raise_events(S2EVENT_RX);
  return 0;
}

static ULONG net64_read_orphan_frame(struct IOSana2Req *ios2) {
  ULONG tries = 8;
  while (tries--) {
    ULONG rc = net64_read_frame(ios2);
    if (rc != 0) {
      return rc;
    }
    if (ios2->ios2_DataLength == 0) {
      return 0;
    }
    if (!is_tracked_type(ios2->ios2_PacketType)) {
      return 0;
    }
  }

  ios2->ios2_DataLength = 0;
  ios2->ios2_Req.io_Error = S2ERR_NO_ERROR;
  ios2->ios2_WireError = S2WERR_GENERIC_ERROR;
  return 0;
}

int __attribute__((no_reorder)) _start(void) {
  return -1;
}

asm("romtag:                                \n"
    "       dc.w    " XSTR(RTC_MATCHWORD) "   \n"
    "       dc.l    romtag                  \n"
    "       dc.l    endcode                 \n"
    "       dc.b    " XSTR(RTF_AUTOINIT) "    \n"
    "       dc.b    " XSTR(DEVICE_VERSION) "  \n"
    "       dc.b    " XSTR(NT_DEVICE) "       \n"
    "       dc.b    " XSTR(DEVICE_PRIORITY) " \n"
    "       dc.l    _device_name             \n"
    "       dc.l    _device_id_string        \n"
    "       dc.l    _auto_init_tables        \n"
    "endcode:\n");

char device_name[] = DEVICE_NAME;
char device_id_string[] = DEVICE_ID_STRING;

static struct Library * __attribute__((used)) init_device(struct Device *dev asm("d0"), UBYTE *seglist asm("a0")) {
  (void)seglist;
  SysBase = *(struct ExecBase **)4L;

  g_base = (net64_base_t *)AllocMem(sizeof(net64_base_t), MEMF_PUBLIC | MEMF_CLEAR);
  if (g_base == NULL) {
    return NULL;
  }

  g_base->device = dev;
  NewList(&g_base->unit.pending_onevent);
  g_base->unit.board_base = find_net64_board_base();
  g_base->unit.event_flags = S2EVENT_CONFIGCHANGED;
  g_base->unit.tracked_count = 0;

  read_station_address(g_base->unit.station_addr);
  WRITELONG(NET64_REG_CMD, NET64_CMD_RESET);

  return (struct Library *)dev;
}

static UBYTE * __attribute__((used)) expunge(struct Library *dev asm("a6")) {
  (void)dev;
  if (g_base != NULL) {
    FreeMem(g_base, sizeof(net64_base_t));
    g_base = NULL;
  }
  return 0;
}

static void __attribute__((used))
open_dev(struct Library *dev asm("a6"), struct IOSana2Req *ios2 asm("a1"), ULONG unit_num asm("d0"), ULONG flags asm("d1")) {
  (void)unit_num;

  dev->lib_OpenCnt++;
  ios2->ios2_Req.io_Device = (struct Device *)dev;
  ios2->ios2_Req.io_Unit = (struct Unit *)&g_base->unit;
  ios2->ios2_Req.io_Error = 0;

  g_base->unit.promisc = (flags & SANA2OPF_PROM) ? 1u : 0u;
  apply_promisc(g_base->unit.promisc);
  raise_events(S2EVENT_CONFIGCHANGED);
}

static UBYTE * __attribute__((used))
close_dev(struct Library *dev asm("a6"), struct IOSana2Req *ios2 asm("a1")) {
  ios2->ios2_Req.io_Device = NULL;
  ios2->ios2_Req.io_Unit = NULL;

  if (dev->lib_OpenCnt > 0) {
    dev->lib_OpenCnt--;
  }

  if (dev->lib_OpenCnt == 0 && (dev->lib_Flags & LIBF_DELEXP)) {
    return expunge(dev);
  }

  return 0;
}

static void __attribute__((used))
begin_io(struct Library *dev asm("a6"), struct IOSana2Req *ios2 asm("a1")) {
  (void)dev;
  ULONG reply_now = 1;

  ios2->ios2_Req.io_Error = S2ERR_NO_ERROR;
  ios2->ios2_WireError = S2WERR_GENERIC_ERROR;

  switch (ios2->ios2_Req.io_Command) {
  case CMD_READ:
    if (g_base->unit.online) {
      (void)net64_read_frame(ios2);
    } else {
      ios2->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
      ios2->ios2_WireError = S2WERR_UNIT_OFFLINE;
    }
    break;

  case S2_BROADCAST:
    memset(ios2->ios2_DstAddr, 0xFF, 6);
    if (g_base->unit.online) {
      (void)net64_write_frame(ios2);
    } else {
      ios2->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
      ios2->ios2_WireError = S2WERR_UNIT_OFFLINE;
    }
    break;

  case CMD_WRITE:
  case S2_MULTICAST:
    if (g_base->unit.online) {
      if (ios2->ios2_Req.io_Command == S2_MULTICAST) {
        ios2->ios2_Req.io_Flags |= SANA2IOF_MCAST;
      }
      (void)net64_write_frame(ios2);
    } else {
      ios2->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
      ios2->ios2_WireError = S2WERR_UNIT_OFFLINE;
    }
    break;

  case S2_DEVICEQUERY:
    fill_device_query(ios2);
    break;

  case S2_GETSTATIONADDRESS:
    memcpy(ios2->ios2_SrcAddr, g_base->unit.station_addr, 6);
    memcpy(ios2->ios2_DstAddr, g_base->unit.station_addr, 6);
    break;

  case S2_CONFIGINTERFACE:
    memcpy(g_base->unit.station_addr, ios2->ios2_SrcAddr, 6);
    g_base->unit.station_addr[0] = (g_base->unit.station_addr[0] & (UBYTE)~0x01u) | 0x02u;
    write_station_address(g_base->unit.station_addr);
    g_base->unit.configured = 1;
    raise_events(S2EVENT_CONFIGCHANGED);
    break;

  case S2_ONLINE:
    if (g_base->unit.online) {
      ios2->ios2_Req.io_Error = S2ERR_BAD_STATE;
      ios2->ios2_WireError = S2WERR_UNIT_ONLINE;
    } else {
      g_base->unit.online = 1;
      raise_events(S2EVENT_ONLINE);
    }
    break;

  case S2_OFFLINE:
    if (!g_base->unit.online) {
      ios2->ios2_Req.io_Error = S2ERR_BAD_STATE;
      ios2->ios2_WireError = S2WERR_UNIT_OFFLINE;
    } else {
      g_base->unit.online = 0;
      raise_events(S2EVENT_OFFLINE);
    }
    break;

  case S2_GETGLOBALSTATS:
    fill_global_stats(ios2);
    break;

  case S2_GETTYPESTATS:
    if (ios2->ios2_StatData != NULL) {
      struct Sana2PacketTypeStats *pts = (struct Sana2PacketTypeStats *)ios2->ios2_StatData;
      memset(pts, 0, sizeof(*pts));
      pts->PacketsSent = g_base->unit.tx_packets;
      pts->PacketsReceived = g_base->unit.rx_packets;
      pts->PacketsDropped = g_base->unit.rx_dropped;
    }
    break;

  case S2_GETSPECIALSTATS:
    if (ios2->ios2_StatData != NULL) {
      struct Sana2SpecialStatHeader *hdr = (struct Sana2SpecialStatHeader *)ios2->ios2_StatData;
      hdr->RecordCountSupplied = 0;
    }
    break;

  case S2_GETPEERADDRESS:
  case S2_GETDNSADDRESS:
    memset(ios2->ios2_SrcAddr, 0, 16);
    memset(ios2->ios2_DstAddr, 0, 16);
    break;

  case S2_GETEXTENDEDGLOBALSTATS:
  case S2_SAMPLE_THROUGHPUT:
    if (ios2->ios2_StatData != NULL) {
      memset(ios2->ios2_StatData, 0, 64);
    }
    break;

  case S2_CONNECT: {
    ULONG ev = S2EVENT_CONNECT;
    if (g_base->unit.connected) {
      ios2->ios2_Req.io_Error = S2ERR_BAD_STATE;
      ios2->ios2_WireError = S2WERR_UNIT_CONNECTED;
      break;
    }
    g_base->unit.connected = 1;
    if (!g_base->unit.online) {
      g_base->unit.online = 1;
      ev |= S2EVENT_ONLINE;
    }
    raise_events(ev);
    break;
  }

  case S2_DISCONNECT: {
    ULONG ev = S2EVENT_DISCONNECT;
    if (!g_base->unit.connected) {
      ios2->ios2_Req.io_Error = S2ERR_BAD_STATE;
      ios2->ios2_WireError = S2WERR_UNIT_DISCONNECTED;
      break;
    }
    g_base->unit.connected = 0;
    if (g_base->unit.online) {
      g_base->unit.online = 0;
      ev |= S2EVENT_OFFLINE;
    }
    raise_events(ev);
    break;
  }

  case S2_ONEVENT:
    reply_now = !queue_onevent_request(ios2);
    break;

  case S2_TRACKTYPE: {
    ULONG w = add_tracked_type(ios2->ios2_PacketType);
    if (w != 0) {
      ios2->ios2_Req.io_Error = (w == S2WERR_ALREADY_TRACKED) ? S2ERR_BAD_STATE : S2ERR_NO_RESOURCES;
      ios2->ios2_WireError = w;
    }
    break;
  }

  case S2_UNTRACKTYPE: {
    ULONG w = remove_tracked_type(ios2->ios2_PacketType);
    if (w != 0) {
      ios2->ios2_Req.io_Error = S2ERR_BAD_STATE;
      ios2->ios2_WireError = w;
    }
    break;
  }

  case S2_READORPHAN:
    if (g_base->unit.online) {
      (void)net64_read_orphan_frame(ios2);
    } else {
      ios2->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
      ios2->ios2_WireError = S2WERR_UNIT_OFFLINE;
    }
    break;

  case S2_ADDMULTICASTADDRESS:
  case S2_DELMULTICASTADDRESS:
  case S2_GETSIGNALQUALITY:
  case S2_GETNETWORKS:
  case S2_SETOPTIONS:
  case S2_SETKEY:
  case S2_GETNETWORKINFO:
  case S2_READMGMT:
  case S2_WRITEMGMT:
  case S2_SANA2HOOK:
    ios2->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
    ios2->ios2_WireError = S2WERR_GENERIC_ERROR;
    break;

  default:
    ios2->ios2_Req.io_Error = IOERR_NOCMD;
    ios2->ios2_WireError = S2WERR_GENERIC_ERROR;
    break;
  }

  if (reply_now) {
    complete_io(ios2);
  }
}

static ULONG __attribute__((used))
abort_io(struct Library *dev asm("a6"), struct IOSana2Req *ios2 asm("a1")) {
  (void)dev;

  ios2->ios2_Req.io_Error = IOERR_ABORTED;
  ios2->ios2_WireError = 0;
  ReplyMsg(&ios2->ios2_Req.io_Message);
  return 0;
}

static ULONG device_vectors[] = {
    (ULONG)open_dev,
    (ULONG)close_dev,
    (ULONG)expunge,
    0,
    (ULONG)begin_io,
    (ULONG)abort_io,
    (ULONG)-1,
};

ULONG auto_init_tables[] = {
    sizeof(struct Library),
    (ULONG)device_vectors,
    0,
    (ULONG)init_device,
};
