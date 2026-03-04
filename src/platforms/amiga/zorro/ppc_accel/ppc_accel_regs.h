// SPDX-License-Identifier: MIT
#ifndef Z2_PPC_ACCEL_REGS_H
#define Z2_PPC_ACCEL_REGS_H

#include <stdint.h>

/*
 * Canonical Stage 6 ABI for PiStorm PPC accelerator board v1 (Z2).
 * This header is shared by emulator-side board code and Amiga-side tools.
 *
 * - 0x0000-0x0FFF: register window
 * - 0x1000-0x1FFF: mailbox page
 * - 0x2000-...   : shared RAM window
 */
#define PPC_ACCEL_MANUFACTURER_ID      0x07DBu
#define PPC_ACCEL_PRODUCT_ID           0x0040u

#define PPC_ACCEL_Z2_SIZE             (64u * 1024u)
#define PPC_ACCEL_REG_WINDOW_SIZE     0x1000u
#define PPC_ACCEL_MAILBOX_OFFSET      0x1000u
#define PPC_ACCEL_MAILBOX_SIZE        0x1000u
#define PPC_ACCEL_SHARED_OFFSET       0x2000u
#define PPC_ACCEL_SHARED_SIZE         (PPC_ACCEL_Z2_SIZE - PPC_ACCEL_SHARED_OFFSET)

/*
 * Shared info block lives at the start of shared window and is read-only from
 * Amiga side. All fields are 32-bit big-endian.
 */
#define PPC_ACCEL_SHARED_INFO_OFFSET          PPC_ACCEL_SHARED_OFFSET
#define PPC_ACCEL_SHARED_INFO_SIZE            0x20u
#define PPC_ACCEL_SHARED_INFO_OFF_SIGNATURE   0x00u
#define PPC_ACCEL_SHARED_INFO_OFF_ABI_VERSION 0x04u
#define PPC_ACCEL_SHARED_INFO_OFF_MB_OFFSET   0x08u
#define PPC_ACCEL_SHARED_INFO_OFF_MB_SIZE     0x0Cu
#define PPC_ACCEL_SHARED_INFO_OFF_DB_REG      0x10u
#define PPC_ACCEL_SHARED_INFO_OFF_FEATURES    0x14u
#define PPC_ACCEL_SHARED_INFO_OFF_RESERVED0   0x18u
#define PPC_ACCEL_SHARED_INFO_OFF_RESERVED1   0x1Cu

#define PPC_ACCEL_SHARED_INFO_SIGNATURE       0x50504341u /* "PPCA" */
#define PPC_ACCEL_SHARED_INFO_ABI_VERSION     1u
#define PPC_ACCEL_SHARED_INFO_FEAT_HOSTSVC    0x00000001u
#define PPC_ACCEL_SHARED_INFO_FEAT_IRQ        0x00000002u
#define PPC_ACCEL_SHARED_INFO_FEAT_DOORBELL   0x00000004u

#define PPC_ACCEL_REG_MAGIC           0x0000u
#define PPC_ACCEL_REG_ABI_VERSION     0x0004u
#define PPC_ACCEL_REG_CONTROL         0x0008u
#define PPC_ACCEL_REG_STATUS          0x000Cu
#define PPC_ACCEL_REG_DOORBELL        0x0010u
#define PPC_ACCEL_REG_IRQ_STATUS      0x0014u
#define PPC_ACCEL_REG_IRQ_ACK         0x0018u
#define PPC_ACCEL_REG_MAILBOX_OFFSET  0x001Cu
#define PPC_ACCEL_REG_MAILBOX_SIZE    0x0020u
#define PPC_ACCEL_REG_SHARED_OFFSET   0x0024u
#define PPC_ACCEL_REG_SHARED_SIZE     0x0028u

#define PPC_ACCEL_MAGIC               0x50504341u /* "PPCA" */
#define PPC_ACCEL_ABI_VERSION         1u

#define PPC_ACCEL_CTRL_START          0x00000001u
#define PPC_ACCEL_CTRL_RESET          0x00000002u
#define PPC_ACCEL_CTRL_IRQ_ENABLE     0x00000004u

#define PPC_ACCEL_STATUS_RUNNING      0x00000001u
#define PPC_ACCEL_STATUS_FAULT        0x00000002u

#define PPC_ACCEL_IRQ_CMD_DONE        0x00000001u
#define PPC_ACCEL_IRQ_HOST_DOORBELL   0x00000002u

/* Mailbox offsets from PPC_ACCEL_MAILBOX_OFFSET */
#define PPC_ACCEL_MB_OFF_MAGIC        0x0000u
#define PPC_ACCEL_MB_OFF_VERSION      0x0004u
#define PPC_ACCEL_MB_OFF_SEQ          0x0008u
#define PPC_ACCEL_MB_OFF_ACK_SEQ      0x000Cu
#define PPC_ACCEL_MB_OFF_CMD          0x0010u
#define PPC_ACCEL_MB_OFF_STATUS       0x0014u
#define PPC_ACCEL_MB_OFF_ARG0         0x0018u
#define PPC_ACCEL_MB_OFF_ARG1         0x001Cu
#define PPC_ACCEL_MB_OFF_RESULT0      0x0020u
#define PPC_ACCEL_MB_OFF_RESULT1      0x0024u

/*
 * Single in-flight command contract:
 * - Command is in-flight while SEQ != ACK_SEQ.
 * - Host must not publish a new command until ACK_SEQ == SEQ.
 * - Host writes cmd/args/result placeholders first, then writes SEQ last.
 * - Responder publishes RESULT/STATUS first, then ACK_SEQ last.
 */

/* Mailbox values */
#define PPC_ACCEL_MB_MAGIC            0x504D4241u /* "PMBA" */
#define PPC_ACCEL_MB_VERSION          1u

#define PPC_ACCEL_MB_CMD_NONE         0u
#define PPC_ACCEL_MB_CMD_PING         1u
#define PPC_ACCEL_MB_CMD_HOST_TIME32  3u

#define PPC_ACCEL_MB_STATUS_IDLE      0u
#define PPC_ACCEL_MB_STATUS_BUSY      1u
#define PPC_ACCEL_MB_STATUS_DONE      2u
#define PPC_ACCEL_MB_STATUS_ERR       3u

#endif
