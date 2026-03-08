// SPDX-License-Identifier: MIT
#ifndef ARMACCEL_LIBRARY_H
#define ARMACCEL_LIBRARY_H

#include <exec/types.h>
#include "armaccel_service_abi.h"

#define ARMACCEL_LIBRARY_SUPPORTED_ABI_MAJOR 1u

#define ARMACCEL_NOTE_NAME "ARMACCEL"
#define ARMACCEL_NOTE_TYPE 0x4143434Cu /* "ACCL" */
#define ARMACCEL_NOTE_DESC_MAGIC 0x414E5631u /* "ANV1" */
#define ARMACCEL_NOTE_DESC_VERSION 1u

#define ARMACCEL_PERSONALITY_ARM64 1u

#define ARMACCEL_SERVICE_WINDOW        ARMACCEL_CAP_WINDOW
#define ARMACCEL_SERVICE_MENU          ARMACCEL_CAP_MENU
#define ARMACCEL_SERVICE_REQUESTER     ARMACCEL_CAP_REQUESTER
#define ARMACCEL_SERVICE_PIXEL_SURFACE ARMACCEL_CAP_SURFACE
#define ARMACCEL_SERVICE_INPUT         ARMACCEL_CAP_INPUT
#define ARMACCEL_SERVICE_FILES         ARMACCEL_CAP_FILE
#define ARMACCEL_SERVICE_TIMERS        ARMACCEL_CAP_TIMER
#define ARMACCEL_SERVICE_CLIPBOARD     ARMACCEL_CAP_CLIPBOARD
#define ARMACCEL_SERVICE_DATATYPE      ARMACCEL_CAP_DATATYPE

#define ARMACCEL_APPCLASS_UNKNOWN    0u
#define ARMACCEL_APPCLASS_UI         1u
#define ARMACCEL_APPCLASS_BATCH      2u
#define ARMACCEL_APPCLASS_RENDERER   3u
#define ARMACCEL_APPCLASS_TOOL       4u

#define ARMACCEL_ELF_COMPAT_NOT_ARMACCEL      0u
#define ARMACCEL_ELF_COMPAT_RUNNABLE          1u
#define ARMACCEL_ELF_COMPAT_NEEDS_SERVICES    2u
#define ARMACCEL_ELF_COMPAT_WRONG_ABI         3u
#define ARMACCEL_ELF_COMPAT_INVALID_FILE      4u

#define ARMACCEL_EXEC_OK                      0
#define ARMACCEL_EXEC_ERR_INVALID_ARG         1
#define ARMACCEL_EXEC_ERR_NOT_ARMACCEL        2
#define ARMACCEL_EXEC_ERR_WRONG_ABI           3
#define ARMACCEL_EXEC_ERR_NEEDS_SERVICES      4
#define ARMACCEL_EXEC_ERR_DEVICE_OPEN         5
#define ARMACCEL_EXEC_ERR_UPLOAD              6
#define ARMACCEL_EXEC_ERR_RUN                 7

struct ArmAccelELFInfo {
  ULONG elf_size;
  ULONG compatibility;
  ULONG abi_major;
  ULONG required_services;
  ULONG available_services;
  ULONG app_class;
  ULONG has_personality_tag;
};

struct ArmAccelRunOpts {
  ULONG flags;
};

struct ArmAccelResult {
  ULONG compatibility;
  ULONG device_result0;
  ULONG device_result1;
  ULONG job_state;
  ULONG job_result;
  ULONG retval_lo;
  ULONG retval_hi;
  ULONG service_dispatch_count;
  ULONG service_hook_status;
};

LONG ARMACCEL_IsSupportedELF(const char *path);
LONG ARMACCEL_QueryELF(const char *path, struct ArmAccelELFInfo *out_info);
LONG ARMACCEL_ExecuteELF(const char *path, const struct ArmAccelRunOpts *opts,
                         struct ArmAccelResult *out_result);

#endif
