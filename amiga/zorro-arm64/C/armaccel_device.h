// SPDX-License-Identifier: MIT
#ifndef ARMACCEL_DEVICE_H
#define ARMACCEL_DEVICE_H

#include <exec/types.h>
#include <libraries/expansionbase.h>
#include "armaccel_iocmds.h"

#define ARMACCEL_DEVICE_WAIT_SPINS_CMD 200000u
#define ARMACCEL_DEVICE_WAIT_SPINS_RUN_ELF 200000000u

#define ARMACCEL_DEVICE_BUSY_RESULT 0xFFFFFFFEu
#define ARMACCEL_DEVICE_ERROR_RESULT 0xFFFFFFFFu

struct armaccel_board {
  struct ExpansionBase *expansion_base;
  struct ConfigDev *config_dev;
  volatile UBYTE *base;
};

int armaccel_device_open(struct armaccel_board *out_board);
void armaccel_device_close(struct armaccel_board *board);

int armaccel_device_dump_identity(struct armaccel_board *board);
ULONG armaccel_device_ping(struct armaccel_board *board);
int armaccel_device_irq_test(struct armaccel_board *board);

ULONG armaccel_device_max_payload_size(void);
int armaccel_device_load_elf(struct armaccel_board *board, const char *path, ULONG *out_elf_size);
int armaccel_device_run_elf_job(struct armaccel_board *board, ULONG *out_result0, ULONG *out_result1,
                                ULONG *out_job_state, ULONG *out_job_result, ULONG *out_ret_lo,
                                ULONG *out_ret_hi, ArmAccelServiceHook service_hook,
                                APTR service_ctx, ULONG *out_service_dispatch_count,
                                ULONG *out_service_hook_status);

#endif
