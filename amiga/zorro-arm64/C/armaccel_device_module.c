// SPDX-License-Identifier: MIT

#include <exec/devices.h>
#include <exec/errors.h>
#include <exec/io.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/types.h>

#include <proto/exec.h>

#include "armaccel_device.h"
#include "armaccel_iocmds.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#define DEVICE_NAME "armaccel.device"
#define DEVICE_DATE "(08 Mar 2026)"
#define DEVICE_ID_STRING "armaccel.device 1.0 " DEVICE_DATE
#define DEVICE_VERSION 1
#define DEVICE_REVISION 0
#define DEVICE_PRIORITY 0

#define ARMACCEL_IOERR_BADARG 20
#define ARMACCEL_IOERR_BOARD 21
#define ARMACCEL_IOERR_UPLOAD 22
#define ARMACCEL_IOERR_RUN 23

struct ExecBase *SysBase = NULL;
static BPTR g_seglist = 0;

int __attribute__((no_reorder)) _start(void) {
  return -1;
}

void _exit(int rc) {
  (void)rc;
  for (;;) {
  }
}

asm("romtag:                                \n"
    "       dc.w    " XSTR(RTC_MATCHWORD) "   \n"
    "       dc.l    romtag                  \n"
    "       dc.l    endcode                 \n"
    "       dc.b    " XSTR(RTF_AUTOINIT) "    \n"
    "       dc.b    " XSTR(DEVICE_VERSION) "  \n"
    "       dc.b    " XSTR(NT_DEVICE) "       \n"
    "       dc.b    " XSTR(DEVICE_PRIORITY) " \n"
    "       dc.l    _armaccel_device_name    \n"
    "       dc.l    _armaccel_device_id      \n"
    "       dc.l    _armaccel_auto_init_tables\n"
    "endcode:\n");

char armaccel_device_name[] = DEVICE_NAME;
char armaccel_device_id[] = DEVICE_ID_STRING;

static struct Library * __attribute__((used))
init_device(struct Device *dev asm("d0"), BPTR seglist asm("a0")) {
  SysBase = *(struct ExecBase **)4L;
  g_seglist = seglist;

  dev->dd_Library.lib_Node.ln_Type = NT_DEVICE;
  dev->dd_Library.lib_Node.ln_Name = armaccel_device_name;
  dev->dd_Library.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
  dev->dd_Library.lib_Version = DEVICE_VERSION;
  dev->dd_Library.lib_Revision = DEVICE_REVISION;
  dev->dd_Library.lib_IdString = armaccel_device_id;
  return (struct Library *)dev;
}

static BPTR __attribute__((used))
expunge_dev(struct Library *dev asm("a6")) {
  BPTR seglist = 0;

  if (dev->lib_OpenCnt != 0) {
    dev->lib_Flags |= LIBF_DELEXP;
    return 0;
  }

  seglist = g_seglist;
  g_seglist = 0;
  Remove(&dev->lib_Node);
  FreeMem((UBYTE *)dev - dev->lib_NegSize, dev->lib_NegSize + dev->lib_PosSize);
  return seglist;
}

static void __attribute__((used))
open_dev(struct Library *dev asm("a6"), struct IORequest *ior asm("a1"), ULONG unit_num asm("d0"),
         ULONG flags asm("d1")) {
  (void)flags;

  if (unit_num != 0u) {
    ior->io_Error = IOERR_OPENFAIL;
    ior->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    return;
  }

  dev->lib_OpenCnt++;
  ior->io_Device = (struct Device *)dev;
  ior->io_Unit = (struct Unit *)dev;
  ior->io_Error = 0;
  ior->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
}

static BPTR __attribute__((used))
close_dev(struct Library *dev asm("a6"), struct IORequest *ior asm("a1")) {
  ior->io_Device = NULL;
  ior->io_Unit = NULL;

  if (dev->lib_OpenCnt > 0) {
    dev->lib_OpenCnt--;
  }

  if ((dev->lib_OpenCnt == 0) && ((dev->lib_Flags & LIBF_DELEXP) != 0)) {
    return expunge_dev(dev);
  }
  return 0;
}

static BYTE handle_probe(struct ArmAccelIORequest *req) {
  struct armaccel_board board;
  int rc;

  rc = armaccel_device_open(&board);
  if (rc != 0) {
    req->transport_status = (ULONG)rc;
    return ARMACCEL_IOERR_BOARD;
  }
  armaccel_device_close(&board);
  return 0;
}

static BYTE handle_ping(struct ArmAccelIORequest *req) {
  struct armaccel_board board;
  ULONG ping_result;
  int rc;

  rc = armaccel_device_open(&board);
  if (rc != 0) {
    req->transport_status = (ULONG)rc;
    return ARMACCEL_IOERR_BOARD;
  }

  ping_result = armaccel_device_ping(&board);
  armaccel_device_close(&board);

  if ((ping_result == ARMACCEL_DEVICE_BUSY_RESULT) ||
      (ping_result == ARMACCEL_DEVICE_ERROR_RESULT)) {
    req->transport_status = ping_result;
    return ARMACCEL_IOERR_BOARD;
  }

  req->result0 = ping_result;
  return 0;
}

static BYTE handle_upload_path(struct ArmAccelIORequest *req) {
  struct armaccel_board board;
  ULONG elf_size = 0u;
  int rc;

  if (req->path == NULL) {
    return ARMACCEL_IOERR_BADARG;
  }

  rc = armaccel_device_open(&board);
  if (rc != 0) {
    req->transport_status = (ULONG)rc;
    return ARMACCEL_IOERR_BOARD;
  }

  rc = armaccel_device_load_elf(&board, (const char *)req->path, &elf_size);
  armaccel_device_close(&board);
  if (rc != 0) {
    req->transport_status = (ULONG)rc;
    return ARMACCEL_IOERR_UPLOAD;
  }

  req->payload_size = elf_size;
  return 0;
}

static BYTE handle_run_job(struct ArmAccelIORequest *req) {
  struct armaccel_board board;
  int rc;

  rc = armaccel_device_open(&board);
  if (rc != 0) {
    req->transport_status = (ULONG)rc;
    return ARMACCEL_IOERR_BOARD;
  }

  rc = armaccel_device_run_elf_job(&board, &req->result0, &req->result1, &req->job_state,
                                   &req->job_result, &req->retval_lo, &req->retval_hi,
                                   req->service_hook, req->service_ctx,
                                   &req->service_dispatch_count, &req->service_hook_status);
  armaccel_device_close(&board);
  if (rc != 0) {
    req->transport_status = (ULONG)rc;
    return ARMACCEL_IOERR_RUN;
  }

  return 0;
}

static void __attribute__((used))
begin_io(struct Library *dev asm("a6"), struct IORequest *ior asm("a1")) {
  struct ArmAccelIORequest *req = (struct ArmAccelIORequest *)ior;
  BYTE io_error = 0;
  ULONG cmd;

  (void)dev;

  req->transport_status = 0u;
  req->payload_size = 0u;
  req->result0 = 0u;
  req->result1 = 0u;
  req->job_state = 0u;
  req->job_result = 0u;
  req->retval_lo = 0u;
  req->retval_hi = 0u;
  req->service_dispatch_count = 0u;
  req->service_hook_status = 0u;

  if (ior->io_Message.mn_Length < sizeof(struct ArmAccelIORequest)) {
    io_error = ARMACCEL_IOERR_BADARG;
  } else {
    cmd = (ULONG)ior->io_Command;
    if (cmd == ARMACCEL_CMD_PROBE) {
      io_error = handle_probe(req);
    } else if (cmd == ARMACCEL_CMD_PING) {
      io_error = handle_ping(req);
    } else if (cmd == ARMACCEL_CMD_UPLOAD_ELF_PATH) {
      io_error = handle_upload_path(req);
    } else if (cmd == ARMACCEL_CMD_RUN_ELF_JOB) {
      io_error = handle_run_job(req);
    } else {
      io_error = IOERR_NOCMD;
    }
  }

  ior->io_Error = io_error;
  if ((ior->io_Flags & IOF_QUICK) == 0) {
    ReplyMsg(&ior->io_Message);
  }
}

static ULONG __attribute__((used))
abort_io(struct Library *dev asm("a6"), struct IORequest *ior asm("a1")) {
  (void)dev;
  ior->io_Error = IOERR_ABORTED;
  ReplyMsg(&ior->io_Message);
  return 0;
}

static ULONG armaccel_device_vectors[] = {
    (ULONG)open_dev,
    (ULONG)close_dev,
    (ULONG)expunge_dev,
    0,
    (ULONG)begin_io,
    (ULONG)abort_io,
    (ULONG)-1,
};

ULONG armaccel_auto_init_tables[] = {
    sizeof(struct Device),
    (ULONG)armaccel_device_vectors,
    0,
    (ULONG)init_device,
};
