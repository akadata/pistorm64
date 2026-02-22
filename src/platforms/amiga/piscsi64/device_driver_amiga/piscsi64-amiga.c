// SPDX-License-Identifier: MIT

#include <exec/resident.h>
#include <exec/types.h>
#include <exec/errors.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <exec/alerts.h>
#include <exec/tasks.h>
#include <exec/io.h>
#include <exec/execbase.h>

#include <libraries/expansion.h>

#include <devices/trackdisk.h>
#include <devices/timer.h>
#include <devices/scsidisk.h>

#include <dos/filehandler.h>

#include <proto/exec.h>
#include <proto/disk.h>
#include <proto/expansion.h>
#include <clib/expansion_protos.h>

#include "newstyle.h"

#include "../piscsi64-enums.h"
#include <stdint.h>

#define STR(s) #s
#define XSTR(s) STR(s)

#define DEVICE_NAME "pi-scsi64.device"
#define DEVICE_DATE "(15 Feb 2026)"
#define DEVICE_ID_STRING "PiSCSI64 " XSTR(DEVICE_VERSION) "." XSTR(DEVICE_REVISION) " " DEVICE_DATE
#define DEVICE_VERSION 44
#define DEVICE_REVISION 0
#define DEVICE_PRIORITY 0

#pragma pack(4)
struct piscsi64_base {
  struct Device* pi_dev;
  struct piscsi64_unit {
    struct Unit unit;
    uint32_t regs_ptr;

    uint8_t enabled;
    uint8_t present;
    uint8_t valid;
    uint8_t read_only;
    uint8_t scsi_type;
    uint8_t motor;
    uint8_t unit_num;
    uint16_t scsi_num;
    uint16_t h, s;
    uint32_t c;

    uint32_t change_num;
    uint8_t sense_key;
    uint8_t sense_asc;
    uint8_t sense_ascq;
  } units[PISCSI64_NUM_UNITS];
};

struct ExecBase* SysBase;
struct ExpansionBase* ExpansionBase;
uint8_t* saved_seg_list;
uint8_t is_open;
static uint32_t piscsi64_base_addr = PISCSI64_OFFSET;

#define WRITESHORT(cmd, val) *(volatile unsigned short*)((unsigned long)(piscsi64_base_addr + (cmd))) = (val);
#define WRITELONG(cmd, val) *(volatile unsigned long*)((unsigned long)(piscsi64_base_addr + (cmd))) = (val);
#define WRITEBYTE(cmd, val) *(volatile unsigned char*)((unsigned long)(piscsi64_base_addr + (cmd))) = (val);

#define READSHORT(cmd, var) var = *(volatile unsigned short*)(piscsi64_base_addr + (cmd));
#define READLONG(cmd, var) var = *(volatile unsigned long*)(piscsi64_base_addr + (cmd));
asm(
"romtag:\n"
"    dc.w   " XSTR(RTC_MATCHWORD) "\n"   /* rt_MatchWord    */
"    dc.l   romtag\n"                    /* rt_MatchTag     */
"    dc.l   endcode\n"                   /* rt_EndSkip      */
"    dc.b   " XSTR(RTF_AUTOINIT) "\n"    /* rt_Flags        */
"    dc.b   " XSTR(DEVICE_VERSION) "\n"  /* rt_Version      */
"    dc.b   " XSTR(NT_DEVICE) "\n"       /* rt_Type         */
"    dc.b   " XSTR(DEVICE_PRIORITY) "\n" /* rt_Pri          */
"    dc.l   _device_name\n"              /* rt_Name         */
"    dc.l   _device_id_string\n"         /* rt_IdString     */
"    dc.l   _auto_init_tables\n"         /* rt_Init         */
"endcode:\n"
);


int __attribute__((no_reorder)) _start() {
  return -1;
}

char device_name[] = DEVICE_NAME;
char device_id_string[] = DEVICE_ID_STRING;

uint8_t piscsi64_perform_io(struct piscsi64_unit* u, struct IORequest* io);
uint8_t piscsi64_rw(struct piscsi64_unit* u, struct IORequest* io);
uint8_t piscsi64_scsi(struct piscsi64_unit* u, struct IORequest* io);
static void piscsi64_clear_sense(struct piscsi64_unit* u);

//#define uint32_t unsigned int
//#define uint16_t unsigned short

#define debug(...)
#define debugval(...)
//#define debug(c, v) WRITESHORT(c, v)
//#define debugval(c, v) WRITELONG(c, v)

void *memset(void *dst, int c, unsigned long len) {
  unsigned char *p = (unsigned char *)dst;
  unsigned char v = (unsigned char)c;
  while (len--) {
    *p++ = v;
  }
  return dst;
}

struct piscsi64_base* dev_base = NULL;

static uint16_t piscsi64_swap16(uint16_t v) {
  return (uint16_t)((v << 8) | (v >> 8));
}

static uint16_t piscsi64_normalize_drvtype(uint16_t raw) {
  uint16_t swapped = piscsi64_swap16(raw);

  if (raw == 0) {
    return 0;
  }
  if (raw == 0xFFFF || swapped == 0xFFFF) {
    return 0;
  }

  if (PISCSI64_DRVTYPE_IS_PRESENT(raw)) {
    return raw;
  }
  if (PISCSI64_DRVTYPE_IS_PRESENT(swapped)) {
    return swapped;
  }

  // Legacy protocol: non-zero means present direct-access disk.
  if (raw == 1 || swapped == 1) {
    return PISCSI64_DRVTYPE_BUILD(PISCSI64_SCSI_TYPE_DIRECT_ACCESS, 0);
  }

  // Last-resort compatibility for older firmware or lane-swizzled values.
  return PISCSI64_DRVTYPE_BUILD(PISCSI64_SCSI_TYPE_DIRECT_ACCESS, 0);
}

static void piscsi64_refresh_unit_state(struct piscsi64_unit* u) {
  uint16_t drvtype_raw = 0;
  uint16_t drvtype = 0;
  uint8_t present = 0;
  uint8_t read_only = 0;
  uint8_t scsi_type = (uint8_t)PISCSI64_SCSI_TYPE_DIRECT_ACCESS;
  uint8_t changed = 0;

  if (!u) {
    return;
  }

  WRITESHORT(PISCSI64_CMD_DRVNUMX, u->unit_num);
  READSHORT(PISCSI64_CMD_DRVTYPE, drvtype_raw);
  drvtype = piscsi64_normalize_drvtype(drvtype_raw);
  present = PISCSI64_DRVTYPE_IS_PRESENT(drvtype) ? 1 : 0;
  read_only = PISCSI64_DRVTYPE_IS_READONLY(drvtype) ? 1 : 0;
  scsi_type = PISCSI64_DRVTYPE_SCSI_TYPE(drvtype);

  if (present != u->present || read_only != u->read_only || scsi_type != u->scsi_type) {
    changed = 1;
  }

  if (present) {
    READLONG(PISCSI64_CMD_CYLS, u->c);
    READSHORT(PISCSI64_CMD_HEADS, u->h);
    READSHORT(PISCSI64_CMD_SECS, u->s);
    u->enabled = 1;
    u->valid = 1;
  } else {
    u->c = 0;
    u->h = 0;
    u->s = 0;
    u->valid = 0;
  }

  u->present = present;
  u->read_only = read_only;
  u->scsi_type = scsi_type;
  if (changed) {
    u->change_num++;
  }
}

static void piscsi64_media_control(struct piscsi64_unit* u, uint16_t cmd) {
  if (!u) {
    return;
  }

  WRITESHORT(PISCSI64_CMD_DRVNUMX, u->unit_num);
  WRITESHORT(cmd, u->unit_num);
  piscsi64_refresh_unit_state(u);
}

static uint32_t find_piscsi64_board_base(void) {
  uint32_t board_addr = 0;
  struct Library* expansion_lib = OpenLibrary((STRPTR)"expansion.library", 0L);

  if (expansion_lib != NULL) {
    struct ExpansionBase* prev_expansion_base = ExpansionBase;
    ExpansionBase = (struct ExpansionBase*)expansion_lib;
    struct ConfigDev* cd = NULL;
    while ((cd = (struct ConfigDev*)FindConfigDev(cd, 2011, 18)) != NULL) {
      if (cd->cd_BoardAddr != NULL) {
        board_addr = (uint32_t)(uintptr_t)cd->cd_BoardAddr;
        break;
      }
    }
    if (board_addr == 0) {
      cd = NULL;
      while ((cd = (struct ConfigDev*)FindConfigDev(cd, 2011, -1)) != NULL) {
        if (cd->cd_BoardAddr != NULL && cd->cd_BoardSize >= 0x10000UL) {
          board_addr = (uint32_t)(uintptr_t)cd->cd_BoardAddr;
          break;
        }
      }
    }
    ExpansionBase = prev_expansion_base;
    CloseLibrary(expansion_lib);
  }

  if (board_addr == 0) {
    board_addr = PISCSI64_OFFSET; // compatibility fallback for static mappings
  }
  return board_addr;
}

static struct Library __attribute__((used)) *
    init_device(uint8_t* seg_list asm("a0"), struct Library* dev asm("d0")) {
  SysBase = *(struct ExecBase**)4L;
  (void)seg_list;

  debug(PISCSI64_DBG_MSG, DBG_INIT);

  dev_base = AllocMem(sizeof(struct piscsi64_base), MEMF_PUBLIC | MEMF_CLEAR);
  dev_base->pi_dev = (struct Device*)dev;
  piscsi64_base_addr = find_piscsi64_board_base();

  for (int i = 0; i < PISCSI64_NUM_UNITS; i++) {
    dev_base->units[i].regs_ptr = piscsi64_base_addr;
    dev_base->units[i].enabled = 0;
    dev_base->units[i].present = 0;
    dev_base->units[i].valid = 0;
    dev_base->units[i].read_only = 0;
    dev_base->units[i].scsi_type = PISCSI64_SCSI_TYPE_DIRECT_ACCESS;
    dev_base->units[i].unit_num = i;
    dev_base->units[i].scsi_num = i;
    dev_base->units[i].change_num = 0;
    piscsi64_clear_sense(&dev_base->units[i]);
    piscsi64_refresh_unit_state(&dev_base->units[i]);
    if (dev_base->units[i].present) {
      debugval(PISCSI64_DBG_VAL1, dev_base->units[i].c);
      debugval(PISCSI64_DBG_VAL2, dev_base->units[i].h);
      debugval(PISCSI64_DBG_VAL3, dev_base->units[i].s);
      debug(PISCSI64_DBG_MSG, DBG_CHS);
    }
  }

  return dev;
}

static uint8_t* __attribute__((used)) expunge(struct Library* dev asm("a6")) {
  debug(PISCSI64_DBG_MSG, DBG_CLEANUP);
  /*if (dev_base->open_count)
      return 0;
  FreeMem(dev_base, sizeof(struct piscsi64_base));*/
  return 0;
}

static void __attribute__((used))
open(struct Library* dev asm("a6"), struct IOExtTD* iotd asm("a1"), uint32_t num asm("d0"),
     uint32_t flags asm("d1")) {
  // struct Node* node = (struct Node*)iotd;
  int io_err = TDERR_BadUnitNum;

  // WRITESHORT(PISCSI64_CMD_DEBUGME, 1);

  int unit_num = num;
  // WRITELONG(PISCSI64_CMD_DRVNUM, num);
  // READLONG(PISCSI64_CMD_DRVNUM, unit_num);

  debugval(PISCSI64_DBG_VAL1, unit_num);
  debugval(PISCSI64_DBG_VAL2, flags);
  debugval(PISCSI64_DBG_VAL3, num);
  debug(PISCSI64_DBG_MSG, DBG_OPENDEV);

  if (iotd && unit_num < PISCSI64_NUM_UNITS) {
    piscsi64_refresh_unit_state(&dev_base->units[unit_num]);
    if (dev_base->units[unit_num].enabled && dev_base->units[unit_num].present) {
      io_err = 0;
      iotd->iotd_Req.io_Unit = (struct Unit*)&dev_base->units[unit_num].unit;
      iotd->iotd_Req.io_Unit->unit_flags = UNITF_ACTIVE;
      iotd->iotd_Req.io_Unit->unit_OpenCnt = 1;
    }
  }

  iotd->iotd_Req.io_Error = io_err;
  ((struct Library*)dev_base->pi_dev)->lib_OpenCnt++;
}

static uint8_t* __attribute__((used))
close(struct Library* dev asm("a6"), struct IOExtTD* iotd asm("a1")) {
  ((struct Library*)dev_base->pi_dev)->lib_OpenCnt--;
  return 0;
}

static void __attribute__((used))
begin_io(struct Library* dev asm("a6"), struct IORequest* io asm("a1")) {
  if (dev_base == NULL || io == NULL)
    return;

  struct piscsi64_unit* u;
  struct Node* node = (struct Node*)io;
  u = (struct piscsi64_unit*)io->io_Unit;

  if (node == NULL || u == NULL)
    return;

  debugval(PISCSI64_DBG_VAL1, io->io_Command);
  debugval(PISCSI64_DBG_VAL2, io->io_Flags);
  debugval(PISCSI64_DBG_VAL3, (io->io_Flags & IOF_QUICK));
  debug(PISCSI64_DBG_MSG, DBG_BEGINIO);
  io->io_Error = piscsi64_perform_io(u, io);

  if (!(io->io_Flags & IOF_QUICK)) {
    ReplyMsg(&io->io_Message);
  }
}

static uint32_t __attribute__((used))
abort_io(struct Library* dev asm("a6"), struct IORequest* io asm("a1")) {
  debug(PISCSI64_DBG_MSG, DBG_ABORTIO);
  if (!io)
    return IOERR_NOCMD;
  io->io_Error = IOERR_ABORTED;

  return IOERR_ABORTED;
}

uint8_t piscsi64_rw(struct piscsi64_unit* u, struct IORequest* io) {
  struct IOStdReq* iostd = (struct IOStdReq*)io;
  struct IOExtTD* iotd = (struct IOExtTD*)io;

  uint8_t* data;
  uint32_t len;
  // uint32_t block, num_blocks;
  uint8_t sderr = 0;

  if (u->read_only) {
    switch (io->io_Command) {
    case CMD_WRITE:
    case TD_FORMAT:
    case TD_FORMAT64:
    case NSCMD_TD_FORMAT64:
    case TD_WRITE64:
    case NSCMD_TD_WRITE64:
      iostd->io_Actual = 0;
      return TDERR_WriteProt;
    default:
      break;
    }
  }
  uint32_t block_size = 512;

  data = iotd->iotd_Req.io_Data;
  len = iotd->iotd_Req.io_Length;

  WRITESHORT(PISCSI64_CMD_DRVNUMX, u->unit_num);
  READLONG(PISCSI64_CMD_BLOCKSIZE, block_size);
  if (!u->present) {
    iostd->io_Actual = 0;
    return TDERR_DiskChanged;
  }

  if (data == 0) {
    return IOERR_BADADDRESS;
  }
  if (len < block_size) {
    iostd->io_Actual = 0;
    return IOERR_BADLENGTH;
  }

  switch (io->io_Command) {
  case TD_WRITE64:
  case NSCMD_TD_WRITE64:
  case TD_FORMAT64:
  case NSCMD_TD_FORMAT64:
    WRITELONG(PISCSI64_CMD_ADDR1, iostd->io_Offset);
    WRITELONG(PISCSI64_CMD_ADDR2, len);
    WRITELONG(PISCSI64_CMD_ADDR3, (uint32_t)data);
    WRITELONG(PISCSI64_CMD_ADDR4, iostd->io_Actual);
    WRITESHORT(PISCSI64_CMD_WRITE64, u->unit_num);
    break;
  case TD_READ64:
  case NSCMD_TD_READ64:
    WRITELONG(PISCSI64_CMD_ADDR1, iostd->io_Offset);
    WRITELONG(PISCSI64_CMD_ADDR2, len);
    WRITELONG(PISCSI64_CMD_ADDR3, (uint32_t)data);
    WRITELONG(PISCSI64_CMD_ADDR4, iostd->io_Actual);
    WRITESHORT(PISCSI64_CMD_READ64, u->unit_num);
    break;
  case TD_FORMAT:
  case CMD_WRITE:
    WRITELONG(PISCSI64_CMD_ADDR1, iostd->io_Offset);
    WRITELONG(PISCSI64_CMD_ADDR2, len);
    WRITELONG(PISCSI64_CMD_ADDR3, (uint32_t)data);
    WRITESHORT(PISCSI64_CMD_WRITEBYTES, u->unit_num);
    break;
  case CMD_READ:
    WRITELONG(PISCSI64_CMD_ADDR1, iostd->io_Offset);
    WRITELONG(PISCSI64_CMD_ADDR2, len);
    WRITELONG(PISCSI64_CMD_ADDR3, (uint32_t)data);
    WRITESHORT(PISCSI64_CMD_READBYTES, u->unit_num);
    break;
  }

  if (sderr) {
    iostd->io_Actual = 0;

    if (sderr & SCSIERR_TIMEOUT)
      return TDERR_DiskChanged;
    if (sderr & SCSIERR_PARAM)
      return TDERR_SeekError;
    if (sderr & SCSIERR_ADDRESS)
      return TDERR_SeekError;
    if (sderr & (SCSIERR_ERASESEQ | SCSIERR_ERASERES))
      return TDERR_BadSecPreamble;
    if (sderr & SCSIERR_CRC)
      return TDERR_BadSecSum;
    if (sderr & SCSIERR_ILLEGAL)
      return TDERR_TooFewSecs;
    if (sderr & SCSIERR_IDLE)
      return TDERR_PostReset;

    return TDERR_SeekError;
  } else {
    iostd->io_Actual = iotd->iotd_Req.io_Length;
  }

  return 0;
}

#define PISCSI64_VENDOR_ID            "_PISTORM"
#define PISCSI64_DISK_PRODID          "_Virtual_Disk___"
#define PISCSI64_CD_PRODID            "_Virtual_CD-ROM_"
#define PISCSI64_DISK_PRODID_REMOTE   "_Remote_Disk____"
#define PISCSI64_CD_PRODID_REMOTE     "_Remote_CD-ROM__"
#define PISCSI64_FLOPPY_PRODID        "_Virtual_Floppy_"
#define PISCSI64_ZIP_PRODID           "_Virtual_ZIP____"
#define PISCSI64_USB_PRODID           "_Virtual_USB____"
#define PISCSI64_FLOPPY_PRODID_REMOTE "_Remote_Floppy__"
#define PISCSI64_ZIP_PRODID_REMOTE    "_Remote_ZIP_____"
#define PISCSI64_USB_PRODID_REMOTE    "_Remote_USB_____"
#define PISCSI64_REV_ID      "0.11"

#define PISCSI64_SENSE_NO_SENSE        0x00u
#define PISCSI64_SENSE_NOT_READY       0x02u
#define PISCSI64_SENSE_MEDIUM_ERROR    0x03u
#define PISCSI64_SENSE_ILLEGAL_REQUEST 0x05u
#define PISCSI64_SENSE_DATA_PROTECT    0x07u
#define PISCSI64_SENSE_HARDWARE_ERROR  0x04u

#define PISCSI64_ASC_MEDIUM_NOT_PRESENT 0x3Au
#define PISCSI64_ASC_WRITE_PROTECTED    0x27u
#define PISCSI64_ASC_INVALID_COMMAND    0x20u
#define PISCSI64_ASC_INVALID_CDB_FIELD  0x24u
#define PISCSI64_ASC_LBA_OUT_OF_RANGE   0x21u
#define PISCSI64_ASC_UNRECOVERED_READ   0x11u
#define PISCSI64_ASC_INTERNAL_TARGET    0x44u

static void piscsi64_set_sense(struct piscsi64_unit* u, uint8_t key, uint8_t asc, uint8_t ascq) {
  if (!u) {
    return;
  }
  u->sense_key = key;
  u->sense_asc = asc;
  u->sense_ascq = ascq;
}

static void piscsi64_clear_sense(struct piscsi64_unit* u) {
  piscsi64_set_sense(u, PISCSI64_SENSE_NO_SENSE, 0, 0);
}

static uint8_t piscsi64_scsi_fail(struct piscsi64_unit* u, uint8_t ioerr, uint8_t key, uint8_t asc, uint8_t ascq) {
  piscsi64_set_sense(u, key, asc, ascq);
  return ioerr;
}

static void piscsi64_store_be16(uint8_t* dst, uint16_t value) {
  dst[0] = (uint8_t)((value >> 8) & 0xFF);
  dst[1] = (uint8_t)(value & 0xFF);
}

static void piscsi64_store_be32(uint8_t* dst, uint32_t value) {
  dst[0] = (uint8_t)((value >> 24) & 0xFF);
  dst[1] = (uint8_t)((value >> 16) & 0xFF);
  dst[2] = (uint8_t)((value >> 8) & 0xFF);
  dst[3] = (uint8_t)(value & 0xFF);
}

static uint32_t piscsi64_min_u32(uint32_t a, uint32_t b) {
  return (a < b) ? a : b;
}

/*
 * Some legacy Amiga tools multiply TotalSectors*SectorSize in 32-bit and
 * display 0 for exact wrap points (common with 32/64/128GiB media at 512B).
 * Report one sector less in those edge cases to keep UI math non-zero.
 */
static uint32_t piscsi64_report_blocks_compat(uint32_t blocks, uint32_t block_size) {
  uint64_t bytes;
  if (blocks <= 1) {
    return blocks;
  }
  if (block_size == 0) {
    block_size = 512;
  }
  bytes = ((uint64_t)blocks) * ((uint64_t)block_size);
  if ((bytes & 0xFFFFFFFFULL) == 0ULL) {
    return blocks - 1u;
  }
  return blocks;
}

static void piscsi64_copy_ascii_field(uint8_t* dst, uint32_t len, const char* src) {
  uint32_t i = 0;
  for (i = 0; i < len; i++) {
    dst[i] = ' ';
  }
  if (!src) {
    return;
  }
  for (i = 0; i < len && src[i] != '\0'; i++) {
    dst[i] = (uint8_t)src[i];
  }
}

static void piscsi64_lba_to_msf(uint32_t lba, uint8_t* dst) {
  uint32_t f = lba + 150; // 2 second pregap
  dst[0] = 0;
  dst[1] = (uint8_t)(f / (75 * 60));
  dst[2] = (uint8_t)((f / 75) % 60);
  dst[3] = (uint8_t)(f % 75);
}

uint8_t piscsi64_scsi(struct piscsi64_unit* u, struct IORequest* io) {
  struct IOStdReq* iostd = (struct IOStdReq*)io;
  struct SCSICmd* scsi = iostd->io_Data;
  uint8_t* data = scsi ? (uint8_t*)scsi->scsi_Data : NULL;
  uint32_t block = 0;
  uint32_t blocks = 0;
  uint32_t maxblocks = 0;
  uint8_t err = 0;
  uint8_t write = 0;
  uint8_t rw_is_6byte = 0;
  uint32_t block_size = 512;
  uint8_t scsi_type = (u->scsi_type == PISCSI64_SCSI_TYPE_CDROM)
                        ? (uint8_t)PISCSI64_SCSI_TYPE_CDROM
                        : (uint8_t)PISCSI64_SCSI_TYPE_DIRECT_ACCESS;

  WRITESHORT(PISCSI64_CMD_DRVNUMX, u->unit_num);
  READLONG(PISCSI64_CMD_BLOCKSIZE, block_size);
  if (block_size == 0) {
    block_size = (scsi_type == PISCSI64_SCSI_TYPE_CDROM) ? 2048u : 512u;
  }

  if (!scsi || !scsi->scsi_Command) {
    return IOERR_BADADDRESS;
  }
  if (scsi->scsi_CmdLength < 6) {
    return IOERR_BADLENGTH;
  }

  debugval(PISCSI64_DBG_VAL1, iostd->io_Length);
  debugval(PISCSI64_DBG_VAL2, scsi->scsi_Command[0]);
  debugval(PISCSI64_DBG_VAL3, scsi->scsi_Command[1]);
  debugval(PISCSI64_DBG_VAL4, scsi->scsi_Command[2]);
  debugval(PISCSI64_DBG_VAL5, scsi->scsi_CmdLength);
  debug(PISCSI64_DBG_MSG, DBG_SCSICMD);

  scsi->scsi_Actual = 0;

  switch (scsi->scsi_Command[0]) {
  case SCSICMD_TEST_UNIT_READY:
    if (u->present) {
      piscsi64_clear_sense(u);
      err = 0;
    } else {
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_NOT_READY, PISCSI64_ASC_MEDIUM_NOT_PRESENT, 0);
    }
    break;

  case SCSICMD_REQUEST_SENSE: {
    uint32_t sense_len = piscsi64_min_u32(scsi->scsi_Length, 18u);
    uint32_t i = 0;
    if (!data || scsi->scsi_Length == 0) {
      err = IOERR_BADADDRESS;
      break;
    }
    for (i = 0; i < sense_len; i++) {
      data[i] = 0;
    }
    if (sense_len > 0) {
      data[0] = 0x70; // current errors, fixed format
    }
    if (sense_len > 2) {
      data[2] = u->sense_key & 0x0F;
    }
    if (sense_len > 7) {
      data[7] = 10;   // additional sense length
    }
    if (sense_len > 12) {
      data[12] = u->sense_asc;
    }
    if (sense_len > 13) {
      data[13] = u->sense_ascq;
    }
    scsi->scsi_Actual = sense_len;
    piscsi64_clear_sense(u);
    err = 0;
    break;
  }

  case SCSICMD_INQUIRY: {
    uint32_t i = 0;
    if (!data || scsi->scsi_Length == 0) {
      err = IOERR_BADADDRESS;
      break;
    }
    for (i = 0; i < scsi->scsi_Length; i++) {
      data[i] = 0;
    }
    if (scsi->scsi_Length > 0) {
      data[0] = (0 << 5) | (scsi_type & 0x1F);
    }
    if (scsi->scsi_Length > 1) {
      data[1] = (scsi_type == PISCSI64_SCSI_TYPE_CDROM) ? 0x80 : 0x00; // removable for CD
    }
    if (scsi->scsi_Length > 2) {
      data[2] = 2; // SCSI-2
    }
    if (scsi->scsi_Length > 3) {
      data[3] = 2; // response format
    }
    if (scsi->scsi_Length > 4) {
      data[4] = 31; // additional length (36-byte inquiry)
    }
    if (scsi->scsi_Length > 8) {
      piscsi64_copy_ascii_field(&data[8], 8, PISCSI64_VENDOR_ID);
    }
    if (scsi->scsi_Length > 16) {
      const char* prodid = (scsi_type == PISCSI64_SCSI_TYPE_CDROM)
                             ? PISCSI64_CD_PRODID
                             : PISCSI64_DISK_PRODID;
      uint32_t backend_info = 0;
      uint32_t prefix_type = 0;
      uint8_t is_remote = 0;

      WRITESHORT(PISCSI64_CMD_DRVNUMX, u->unit_num);
      READLONG(PISCSI64_CMD_BACKEND_INFO, backend_info);
      prefix_type = (backend_info & PISCSI64_BACKEND_INFO_PREFIX_MASK);
      is_remote = ((backend_info & PISCSI64_BACKEND_INFO_REMOTE) != 0u) ? 1 : 0;

      if (is_remote) {
        prodid = (scsi_type == PISCSI64_SCSI_TYPE_CDROM)
                   ? PISCSI64_CD_PRODID_REMOTE
                   : PISCSI64_DISK_PRODID_REMOTE;
      }

      if (scsi_type != PISCSI64_SCSI_TYPE_CDROM) {
        switch (prefix_type) {
          case PISCSI64_PREFIX_FLOPPY:
            prodid = is_remote ? PISCSI64_FLOPPY_PRODID_REMOTE : PISCSI64_FLOPPY_PRODID;
            break;
          case PISCSI64_PREFIX_ZIP:
            prodid = is_remote ? PISCSI64_ZIP_PRODID_REMOTE : PISCSI64_ZIP_PRODID;
            break;
          case PISCSI64_PREFIX_USB:
            prodid = is_remote ? PISCSI64_USB_PRODID_REMOTE : PISCSI64_USB_PRODID;
            break;
          case PISCSI64_PREFIX_DISK:
          case PISCSI64_PREFIX_CDROM:
          case PISCSI64_PREFIX_UNKNOWN:
          default:
            break;
        }
      }

      piscsi64_copy_ascii_field(&data[16], 16, prodid);
    }
    if (scsi->scsi_Length > 32) {
      piscsi64_copy_ascii_field(&data[32], 4, PISCSI64_REV_ID);
    }
    scsi->scsi_Actual = piscsi64_min_u32(scsi->scsi_Length, 36u);
    piscsi64_clear_sense(u);
    err = 0;
    break;
  }

  case SCSICMD_WRITE_6:
    write = 1;
  case SCSICMD_READ_6:
    if (!scsi->scsi_Command || scsi->scsi_CmdLength < 6) {
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    rw_is_6byte = 1;
    block = scsi->scsi_Command[1] & 0x1f;
    block = (block << 8) | scsi->scsi_Command[2];
    block = (block << 8) | scsi->scsi_Command[3];
    blocks = scsi->scsi_Command[4];
    if (blocks == 0) {
      blocks = 256; // SCSI-1/2 READ/WRITE(6) semantics
    }
    debugval(PISCSI64_DBG_VAL1, (uint32_t)scsi->scsi_Command);
    debug(PISCSI64_DBG_MSG, DBG_SCSICMD_RW6);
    goto scsireadwrite;

  case SCSICMD_WRITE_10:
    write = 1;
  case SCSICMD_READ_10:
    if (!scsi->scsi_Command || scsi->scsi_CmdLength < 10) {
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    debugval(PISCSI64_DBG_VAL1, (uint32_t)scsi->scsi_Command);
    debug(PISCSI64_DBG_MSG, DBG_SCSICMD_RW10);
    block = scsi->scsi_Command[2];
    block = (block << 8) | scsi->scsi_Command[3];
    block = (block << 8) | scsi->scsi_Command[4];
    block = (block << 8) | scsi->scsi_Command[5];
    blocks = scsi->scsi_Command[7];
    blocks = (blocks << 8) | scsi->scsi_Command[8];
    goto scsireadwrite;

  case SCSICMD_WRITE_12:
    write = 1;
  case SCSICMD_READ_12:
    if (!scsi->scsi_Command || scsi->scsi_CmdLength < 12) {
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    block = scsi->scsi_Command[2];
    block = (block << 8) | scsi->scsi_Command[3];
    block = (block << 8) | scsi->scsi_Command[4];
    block = (block << 8) | scsi->scsi_Command[5];
    blocks = scsi->scsi_Command[6];
    blocks = (blocks << 8) | scsi->scsi_Command[7];
    blocks = (blocks << 8) | scsi->scsi_Command[8];
    blocks = (blocks << 8) | scsi->scsi_Command[9];
    goto scsireadwrite;

  scsireadwrite: {
    uint32_t bytes = 0;
    WRITESHORT(PISCSI64_CMD_DRVNUM, (u->scsi_num));
    READLONG(PISCSI64_CMD_BLOCKS, maxblocks);

    if (!u->present) {
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_NOT_READY, PISCSI64_ASC_MEDIUM_NOT_PRESENT, 0);
      break;
    }
    if (write && (u->read_only || scsi_type == PISCSI64_SCSI_TYPE_CDROM)) {
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_DATA_PROTECT, PISCSI64_ASC_WRITE_PROTECTED, 0);
      break;
    }
    if (!rw_is_6byte && blocks == 0) {
      // READ/WRITE(10/12) with transfer length 0 means no data.
      scsi->scsi_Actual = 0;
      piscsi64_clear_sense(u);
      err = 0;
      break;
    }
    if (!data) {
      err = piscsi64_scsi_fail(u, IOERR_BADADDRESS, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    if (block >= maxblocks || blocks > (maxblocks - block)) {
      err = piscsi64_scsi_fail(u, IOERR_BADADDRESS, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_LBA_OUT_OF_RANGE, 0);
      break;
    }
    if (blocks > (0xFFFFFFFFu / block_size)) {
      err = piscsi64_scsi_fail(u, IOERR_BADLENGTH, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    bytes = blocks * block_size;
    if (bytes > scsi->scsi_Length) {
      err = piscsi64_scsi_fail(u, IOERR_BADLENGTH, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }

    WRITELONG(PISCSI64_CMD_ADDR1, block);
    WRITELONG(PISCSI64_CMD_ADDR2, bytes);
    WRITELONG(PISCSI64_CMD_ADDR3, (uint32_t)data);
    if (!write) {
      WRITESHORT(PISCSI64_CMD_READ, u->unit_num);
    } else {
      WRITESHORT(PISCSI64_CMD_WRITE, u->unit_num);
    }

    scsi->scsi_Actual = bytes;
    piscsi64_clear_sense(u);
    err = 0;
    break;
  }

  case SCSICMD_READ_CAPACITY_10:
    if (scsi->scsi_CmdLength < 10) {
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    if (!data || scsi->scsi_Length < 8) {
      err = piscsi64_scsi_fail(u, IOERR_BADLENGTH, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    if (!u->present) {
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_NOT_READY, PISCSI64_ASC_MEDIUM_NOT_PRESENT, 0);
      break;
    }
    WRITESHORT(PISCSI64_CMD_DRVNUM, (u->scsi_num));
    READLONG(PISCSI64_CMD_BLOCKS, blocks);
    blocks = piscsi64_report_blocks_compat(blocks, block_size);
    piscsi64_store_be32(&data[0], (blocks == 0) ? 0 : (blocks - 1));
    piscsi64_store_be32(&data[4], block_size);
    scsi->scsi_Actual = 8;
    piscsi64_clear_sense(u);
    err = 0;
    break;

  case SCSICMD_MODE_SENSE_6:
    if (!data || scsi->scsi_Length < 4) {
      err = piscsi64_scsi_fail(u, IOERR_BADLENGTH, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    if (scsi_type == PISCSI64_SCSI_TYPE_CDROM) {
      data[0] = 3;
      data[1] = 0;
      data[2] = 0x80; // write protected
      data[3] = 0;
      scsi->scsi_Actual = 4;
      piscsi64_clear_sense(u);
      err = 0;
      break;
    }

    debugval(PISCSI64_DBG_VAL1, ((uint32_t)scsi->scsi_Command));
    debug(PISCSI64_DBG_MSG, DBG_SCSI_DEBUG_MODESENSE_6);

    WRITESHORT(PISCSI64_CMD_DRVNUM, (u->scsi_num));
    READLONG(PISCSI64_CMD_BLOCKS, maxblocks);
    maxblocks = piscsi64_report_blocks_compat(maxblocks, block_size);
    {
      uint8_t block_desc_len = (maxblocks > 0xFFFFFFu) ? 0 : 8;
      uint32_t blocks24 = (maxblocks == 0) ? 0 : (maxblocks - 1);

      /*
       * MODE SENSE(6) short block descriptor has only 24-bit block count.
       * For larger media, omit descriptor entirely so tools use READ CAPACITY
       * / geometry page values instead of truncating to ~8GB-scale numbers.
       */
      data[0] = (uint8_t)(3 + block_desc_len + 0x18);
      data[1] = 0;
      data[2] = 0;
      data[3] = block_desc_len;

      if (block_desc_len == 8) {
        data[4] = 0x00; /* density code */
        data[5] = (uint8_t)((blocks24 >> 16) & 0xFF);
        data[6] = (uint8_t)((blocks24 >> 8) & 0xFF);
        data[7] = (uint8_t)(blocks24 & 0xFF);
        data[8] = 0x00; /* reserved */
        data[9] = (uint8_t)((block_size >> 16) & 0xFF);
        data[10] = (uint8_t)((block_size >> 8) & 0xFF);
        data[11] = (uint8_t)(block_size & 0xFF);
      }
    }

    switch (((UWORD)scsi->scsi_Command[2] << 8) | scsi->scsi_Command[3]) {
    case 0x0300: { // Format Device Mode
      uint8_t* datext = data + 4 + data[3];
      debug(PISCSI64_DBG_MSG, DBG_SCSI_FORMATDEVICE);
      datext[0] = 0x03;
      datext[1] = 0x16;
      datext[2] = 0x00;
      datext[3] = 0x01;
      piscsi64_store_be32(&datext[4], 0);
      piscsi64_store_be32(&datext[8], 0);
      piscsi64_store_be16(&datext[10], u->s);
      piscsi64_store_be16(&datext[12], (uint16_t)block_size);
      datext[14] = 0x00;
      datext[15] = 0x01;
      piscsi64_store_be32(&datext[16], 0);
      datext[20] = 0x80;

      scsi->scsi_Actual = data[0] + 1;
      piscsi64_clear_sense(u);
      err = 0;
      break;
    }
    case 0x0400: { // Rigid Drive Geometry
      uint8_t* datext = data + 4 + data[3];
      debug(PISCSI64_DBG_MSG, DBG_SCSI_RDG);
      datext[0] = 0x04;
      datext[1] = 0x16;
      datext[2] = (uint8_t)((u->c >> 16) & 0xFF);
      datext[3] = (uint8_t)((u->c >> 8) & 0xFF);
      datext[4] = (uint8_t)(u->c & 0xFF);
      datext[5] = u->h;
      datext[6] = 0x00;
      piscsi64_store_be32(&datext[6], 0);
      piscsi64_store_be32(&datext[10], 0);
      datext[13] = (uint8_t)((u->c >> 16) & 0xFF);
      datext[14] = (uint8_t)((u->c >> 8) & 0xFF);
      datext[15] = (uint8_t)(u->c & 0xFF);
      datext[17] = 0;
      piscsi64_store_be32(&datext[18], 0);
      piscsi64_store_be16(&datext[20], 5400);

      scsi->scsi_Actual = data[0] + 1;
      piscsi64_clear_sense(u);
      err = 0;
      break;
    }
    default:
      debugval(PISCSI64_DBG_VAL1, (((UWORD)scsi->scsi_Command[2] << 8) | scsi->scsi_Command[3]));
      debug(PISCSI64_DBG_MSG, DBG_SCSI_UNKNOWN_MODESENSE);
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    break;

  case SCSICMD_READ_TOC_PMA_ATIP: {
    uint8_t use_msf = (scsi->scsi_Command[1] & 0x02) ? 1 : 0;
    if (scsi_type != PISCSI64_SCSI_TYPE_CDROM) {
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    if (!data || scsi->scsi_Length < 4) {
      err = piscsi64_scsi_fail(u, IOERR_BADLENGTH, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_CDB_FIELD, 0);
      break;
    }
    WRITESHORT(PISCSI64_CMD_DRVNUM, (u->scsi_num));
    READLONG(PISCSI64_CMD_BLOCKS, maxblocks);

    {
      uint32_t i = 0;
      uint32_t out_len = piscsi64_min_u32(scsi->scsi_Length, 20u);
      for (i = 0; i < out_len; i++) {
        data[i] = 0;
      }
      if (out_len > 0) data[0] = 0x00;
      if (out_len > 1) data[1] = 0x12;
      if (out_len > 2) data[2] = 0x01;
      if (out_len > 3) data[3] = 0x01;
      if (out_len > 5) data[5] = 0x14;
      if (out_len > 6) data[6] = 0x01;
      if (out_len > 13) data[13] = 0x14;
      if (out_len > 14) data[14] = 0xAA;
      if (out_len >= 12) {
        if (use_msf) {
          piscsi64_lba_to_msf(0, &data[8]);
        } else {
          piscsi64_store_be32(&data[8], 0);
        }
      }
      if (out_len >= 20) {
        if (use_msf) {
          piscsi64_lba_to_msf(maxblocks, &data[16]);
        } else {
          piscsi64_store_be32(&data[16], maxblocks);
        }
      }
      scsi->scsi_Actual = out_len;
      piscsi64_clear_sense(u);
      err = 0;
    }
    break;
  }

  case SCSICMD_START_STOP_UNIT:
    if (scsi->scsi_CmdLength >= 5) {
      uint8_t loej = (scsi->scsi_Command[4] & 0x02) ? 1 : 0;
      uint8_t start = (scsi->scsi_Command[4] & 0x01) ? 1 : 0;
      if (loej && !start) {
        piscsi64_media_control(u, PISCSI64_CMD_MEDIA_EJECT);
      } else if (loej && start) {
        piscsi64_media_control(u, PISCSI64_CMD_MEDIA_INSERT);
      }
    }
    scsi->scsi_Actual = 0;
    if (u->present || ((scsi->scsi_Command[4] & 0x02) && !(scsi->scsi_Command[4] & 0x01))) {
      piscsi64_clear_sense(u);
      err = 0;
    } else {
      err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_NOT_READY, PISCSI64_ASC_MEDIUM_NOT_PRESENT, 0);
    }
    break;

  case SCSICMD_READ_DEFECT_DATA_10:
  case SCSICMD_CHANGE_DEFINITION:
    piscsi64_clear_sense(u);
    err = 0;
    break;

  default:
    debugval(PISCSI64_DBG_VAL1, scsi->scsi_Command[0]);
    debug(PISCSI64_DBG_MSG, DBG_SCSI_UNKNOWN_COMMAND);
    err = piscsi64_scsi_fail(u, HFERR_BadStatus, PISCSI64_SENSE_ILLEGAL_REQUEST, PISCSI64_ASC_INVALID_COMMAND, 0);
    break;
  }

  if (err != 0) {
    debugval(PISCSI64_DBG_VAL1, err);
    debug(PISCSI64_DBG_MSG, DBG_SCSIERR);
    scsi->scsi_Actual = 0;
  }

  return err;
}

uint16_t ns_support[] = {
    NSCMD_DEVICEQUERY, CMD_RESET,
    CMD_READ,          CMD_WRITE,
    CMD_UPDATE,        CMD_CLEAR,
    CMD_START,         CMD_STOP,
    CMD_FLUSH,         TD_MOTOR,
    TD_SEEK,           TD_FORMAT,
    TD_REMOVE,         TD_CHANGENUM,
    TD_CHANGESTATE,    TD_PROTSTATUS,
    TD_GETDRIVETYPE,   TD_GETGEOMETRY,
    TD_ADDCHANGEINT,   TD_REMCHANGEINT,
    TD_EJECT,
    HD_SCSICMD,        NSCMD_TD_READ64,
    NSCMD_TD_WRITE64,  NSCMD_TD_SEEK64,
    NSCMD_TD_FORMAT64, 0,
};

#define DUMMYCMD                                                                                   \
  iostd->io_Actual = 0;                                                                            \
  break;

uint8_t piscsi64_perform_io(struct piscsi64_unit* u, struct IORequest* io) {
  struct IOStdReq* iostd = (struct IOStdReq*)io;
  struct IOExtTD* iotd = (struct IOExtTD*)io;

  // uint8_t *data;
  // uint32_t len;
  // uint32_t offset;
  uint8_t err = 0;

  // data = iotd->iotd_Req.io_Data;
  // len = iotd->iotd_Req.io_Length;

  if (io->io_Error == IOERR_ABORTED) {
    return io->io_Error;
  }

  piscsi64_refresh_unit_state(u);
  if (!u->enabled) {
    return IOERR_OPENFAIL;
  }

  debugval(PISCSI64_DBG_VAL1, io->io_Command);
  debugval(PISCSI64_DBG_VAL2, io->io_Flags);
  debugval(PISCSI64_DBG_VAL3, iostd->io_Length);
  debug(PISCSI64_DBG_MSG, DBG_IOCMD);

  switch (io->io_Command) {
  case NSCMD_DEVICEQUERY: {
    struct NSDeviceQueryResult* res = (struct NSDeviceQueryResult*)iotd->iotd_Req.io_Data;
    res->DevQueryFormat = 0;
    res->SizeAvailable = 16;
    res->DeviceType = NSDEVTYPE_TRACKDISK;
    res->DeviceSubType = 0;
    res->SupportedCommands = ns_support;

    iostd->io_Actual = 16;
    return 0;
    break;
  }
  case CMD_CLEAR:
    /* Invalidate read buffer */
    DUMMYCMD;
  case CMD_UPDATE:
    /* Flush write buffer */
    DUMMYCMD;
  case CMD_FLUSH:
    DUMMYCMD;
  case CMD_STOP:
    iostd->io_Actual = 0;
    break;
  case CMD_START:
    piscsi64_media_control(u, PISCSI64_CMD_MEDIA_INSERT);
    iostd->io_Actual = 0;
    err = u->present ? 0 : TDERR_DiskChanged;
    break;
  case TD_PROTSTATUS:
    iostd->io_Actual = u->read_only ? 1 : 0;
    break;
  case TD_CHANGENUM:
    iostd->io_Actual = u->change_num;
    break;
  case TD_REMOVE:
    /* Trackdisk semantics: wait/probe for removal. Do not eject media here. */
    iostd->io_Actual = 0;
    break;
  case TD_CHANGESTATE:
    iostd->io_Actual = u->present ? 0 : 1;
    break;
  case TD_EJECT:
    piscsi64_media_control(u, PISCSI64_CMD_MEDIA_EJECT);
    iostd->io_Actual = 0;
    break;
  case TD_GETDRIVETYPE:
    iostd->io_Actual = (u->scsi_type == PISCSI64_SCSI_TYPE_CDROM) ? DG_CDROM : DG_DIRECT_ACCESS;
    break;
  case TD_MOTOR:
    iostd->io_Actual = u->motor;
    u->motor = iostd->io_Length ? 1 : 0;
    break;
  case TD_GETGEOMETRY: {
    struct DriveGeometry* res = (struct DriveGeometry*)iostd->io_Data;
    WRITESHORT(PISCSI64_CMD_DRVNUMX, u->unit_num);
    READLONG(PISCSI64_CMD_BLOCKSIZE, res->dg_SectorSize);
    READLONG(PISCSI64_CMD_BLOCKS, res->dg_TotalSectors);
    res->dg_TotalSectors = piscsi64_report_blocks_compat(res->dg_TotalSectors, res->dg_SectorSize);
    res->dg_Cylinders = u->c;
    res->dg_CylSectors = u->s * u->h;
    res->dg_Heads = u->h;
    res->dg_TrackSectors = u->s;
    res->dg_BufMemType = MEMF_PUBLIC;
    res->dg_DeviceType = (u->scsi_type == PISCSI64_SCSI_TYPE_CDROM) ? DG_CDROM : DG_DIRECT_ACCESS;
    res->dg_Flags = 0;

    return 0;
    break;
  }

  case TD_FORMAT:
  case TD_FORMAT64:
  case NSCMD_TD_FORMAT64:
  case TD_READ64:
  case NSCMD_TD_READ64:
  case TD_WRITE64:
  case NSCMD_TD_WRITE64:
  case CMD_WRITE:
  case CMD_READ:
    err = piscsi64_rw(u, io);
    break;
  case HD_SCSICMD:
    // err = 0;
    err = piscsi64_scsi(u, io);
    break;
  default: {
    // int cmd = io->io_Command;
    debug(PISCSI64_DBG_MSG, DBG_IOCMD_UNHANDLED);
    err = IOERR_NOCMD;
    break;
  }
  }

  return err;
}
#undef DUMMYCMD

static uint32_t device_vectors[] = {(uint32_t)open,
                                    (uint32_t)close,
                                    (uint32_t)expunge,
                                    0, // extFunc not used here
                                    (uint32_t)begin_io,
                                    (uint32_t)abort_io,
                                    -1};

const uint32_t auto_init_tables[4] = {sizeof(struct Library), (uint32_t)device_vectors, 0,
                                      (uint32_t)init_device};
