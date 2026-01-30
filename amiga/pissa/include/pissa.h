// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#define PISSA_REG_CMD      0x00u
#define PISSA_REG_STATUS   0x04u
#define PISSA_REG_KEY_OFF  0x08u
#define PISSA_REG_IV_OFF   0x0Cu
#define PISSA_REG_AAD_OFF  0x10u
#define PISSA_REG_AAD_LEN  0x14u
#define PISSA_REG_SRC_OFF  0x18u
#define PISSA_REG_DST_OFF  0x1Cu
#define PISSA_REG_LEN      0x20u
#define PISSA_REG_TAG_OFF  0x24u

#define PISSA_CMD_AES_GCM_ENC 0x01u
#define PISSA_CMD_AES_GCM_DEC 0x02u

#define PISSA_STATUS_BUSY 0x01u
#define PISSA_STATUS_DONE 0x02u
#define PISSA_STATUS_ERR  0x04u

uint32_t pissa_read_status(uint32_t base);
int pissa_wait_done(uint32_t base, uint32_t timeout_ticks);
