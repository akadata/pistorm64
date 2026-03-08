// SPDX-License-Identifier: MIT
#ifndef ARMACCEL_IOCMDS_H
#define ARMACCEL_IOCMDS_H

#include <exec/io.h>
#include <exec/types.h>

#define ARMACCEL_CMD_PROBE            (CMD_NONSTD + 0u)
#define ARMACCEL_CMD_PING             (CMD_NONSTD + 1u)
#define ARMACCEL_CMD_UPLOAD_ELF_PATH  (CMD_NONSTD + 2u)
#define ARMACCEL_CMD_RUN_ELF_JOB      (CMD_NONSTD + 3u)

typedef ULONG (*ArmAccelServiceHook)(volatile UBYTE *board_base, APTR context);

struct ArmAccelIORequest {
  struct IOStdReq io;
  STRPTR path;
  ULONG payload_size;
  ULONG transport_status;
  ULONG result0;
  ULONG result1;
  ULONG job_state;
  ULONG job_result;
  ULONG retval_lo;
  ULONG retval_hi;
  ArmAccelServiceHook service_hook;
  APTR service_ctx;
  ULONG service_dispatch_count;
  ULONG service_hook_status;
};

#endif
