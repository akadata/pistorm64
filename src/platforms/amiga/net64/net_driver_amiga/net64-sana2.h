#ifndef NET64_SANA2_H
#define NET64_SANA2_H 1

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#ifndef EXEC_PORTS_H
#include <exec/ports.h>
#endif

#ifndef EXEC_IO_H
#include <exec/io.h>
#endif

#ifndef EXEC_ERRORS_H
#include <exec/errors.h>
#endif

#ifndef DEVICES_TIMER_H
#include <devices/timer.h>
#endif

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

#define SANA2_MAX_ADDR_BITS (128)
#define SANA2_MAX_ADDR_BYTES ((SANA2_MAX_ADDR_BITS + 7) / 8)

struct IOSana2Req {
  struct IORequest ios2_Req;
  ULONG ios2_WireError;
  ULONG ios2_PacketType;
  UBYTE ios2_SrcAddr[SANA2_MAX_ADDR_BYTES];
  UBYTE ios2_DstAddr[SANA2_MAX_ADDR_BYTES];
  ULONG ios2_DataLength;
  VOID *ios2_Data;
  VOID *ios2_StatData;
  VOID *ios2_BufferManagement;
};

#define SANA2IOB_RAW (7)
#define SANA2IOF_RAW (1 << SANA2IOB_RAW)
#define SANA2IOB_BCAST (6)
#define SANA2IOF_BCAST (1 << SANA2IOB_BCAST)
#define SANA2IOB_MCAST (5)
#define SANA2IOF_MCAST (1 << SANA2IOB_MCAST)
#define SANA2IOB_QUICK (IOB_QUICK)
#define SANA2IOF_QUICK (IOF_QUICK)

#define SANA2OPB_MINE (0)
#define SANA2OPF_MINE (1 << SANA2OPB_MINE)
#define SANA2OPB_PROM (1)
#define SANA2OPF_PROM (1 << SANA2OPB_PROM)

#define S2_Dummy (TAG_USER + 0xB0000)
#define S2_CopyToBuff (S2_Dummy + 1)
#define S2_CopyFromBuff (S2_Dummy + 2)
#define S2_PacketFilter (S2_Dummy + 3)

struct Sana2DeviceQuery {
  ULONG SizeAvailable;
  ULONG SizeSupplied;
  ULONG DevQueryFormat;
  ULONG DeviceLevel;
  UWORD AddrFieldSize;
  ULONG MTU;
  ULONG BPS;
  ULONG HardwareType;
};

#define S2WireType_Ethernet 1

struct Sana2PacketTypeStats {
  ULONG PacketsSent;
  ULONG PacketsReceived;
  ULONG BytesSent;
  ULONG BytesReceived;
  ULONG PacketsDropped;
};

struct Sana2SpecialStatRecord {
  ULONG Type;
  ULONG Count;
  char *String;
};

struct Sana2SpecialStatHeader {
  ULONG RecordCountMax;
  ULONG RecordCountSupplied;
};

struct Sana2DeviceStats {
  ULONG PacketsReceived;
  ULONG PacketsSent;
  ULONG BadData;
  ULONG Overruns;
  ULONG Unused;
  ULONG UnknownTypesReceived;
  ULONG Reconfigurations;
  struct timeval LastStart;
};

#define S2_START (CMD_NONSTD)

#define S2_DEVICEQUERY (S2_START + 0)
#define S2_GETSTATIONADDRESS (S2_START + 1)
#define S2_CONFIGINTERFACE (S2_START + 2)
#define S2_ADDMULTICASTADDRESS (S2_START + 5)
#define S2_DELMULTICASTADDRESS (S2_START + 6)
#define S2_MULTICAST (S2_START + 7)
#define S2_BROADCAST (S2_START + 8)
#define S2_TRACKTYPE (S2_START + 9)
#define S2_UNTRACKTYPE (S2_START + 10)
#define S2_GETTYPESTATS (S2_START + 11)
#define S2_GETSPECIALSTATS (S2_START + 12)
#define S2_GETGLOBALSTATS (S2_START + 13)
#define S2_ONEVENT (S2_START + 14)
#define S2_READORPHAN (S2_START + 15)
#define S2_ONLINE (S2_START + 16)
#define S2_OFFLINE (S2_START + 17)

#define S2_GETPEERADDRESS 0xC002
#define S2_GETDNSADDRESS 0xC003
#define S2_GETEXTENDEDGLOBALSTATS 0xC004
#define S2_CONNECT 0xC005
#define S2_DISCONNECT 0xC006
#define S2_SAMPLE_THROUGHPUT 0xC007
#define S2_SANA2HOOK 0xC008
#define S2_GETSIGNALQUALITY 0xC010
#define S2_GETNETWORKS 0xC011
#define S2_SETOPTIONS 0xC012
#define S2_SETKEY 0xC013
#define S2_GETNETWORKINFO 0xC014
#define S2_READMGMT 0xC015
#define S2_WRITEMGMT 0xC016

#define S2_END (S2_START + 18)

#define S2ERR_NO_ERROR 0
#define S2ERR_NO_RESOURCES 1
#define S2ERR_BAD_ARGUMENT 3
#define S2ERR_BAD_STATE 4
#define S2ERR_BAD_ADDRESS 5
#define S2ERR_MTU_EXCEEDED 6
#define S2ERR_NOT_SUPPORTED 8
#define S2ERR_SOFTWARE 9
#define S2ERR_OUTOFSERVICE 10
#define S2ERR_TX_FAILURE 11

#define S2WERR_GENERIC_ERROR 0
#define S2WERR_NOT_CONFIGURED 1
#define S2WERR_UNIT_ONLINE 2
#define S2WERR_UNIT_OFFLINE 3
#define S2WERR_UNIT_CONNECTED 18
#define S2WERR_UNIT_DISCONNECTED 19
#define S2WERR_ALREADY_TRACKED 4
#define S2WERR_NOT_TRACKED 5
#define S2WERR_BUFF_ERROR 6
#define S2WERR_SRC_ADDRESS 7
#define S2WERR_DST_ADDRESS 8
#define S2WERR_BAD_BROADCAST 9
#define S2WERR_BAD_EVENT 17

#define S2EVENT_ERROR (1UL << 0)
#define S2EVENT_TX (1UL << 1)
#define S2EVENT_RX (1UL << 2)
#define S2EVENT_ONLINE (1UL << 3)
#define S2EVENT_OFFLINE (1UL << 4)
#define S2EVENT_BUFF (1UL << 5)
#define S2EVENT_HARDWARE (1UL << 6)
#define S2EVENT_SOFTWARE (1UL << 7)
#define S2EVENT_CONFIGCHANGED (1UL << 8)
#define S2EVENT_CONNECT (1UL << 9)
#define S2EVENT_DISCONNECT (1UL << 10)
#define S2EVENT_ALL (S2EVENT_ERROR | S2EVENT_TX | S2EVENT_RX | S2EVENT_ONLINE | S2EVENT_OFFLINE | S2EVENT_BUFF | \
                     S2EVENT_HARDWARE | S2EVENT_SOFTWARE | S2EVENT_CONFIGCHANGED | S2EVENT_CONNECT | \
                     S2EVENT_DISCONNECT)

#endif
