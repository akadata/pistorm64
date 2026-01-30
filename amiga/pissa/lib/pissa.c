// SPDX-License-Identifier: MIT
#include "pissa.h"

static inline volatile uint32_t *pissa_reg(uint32_t base, uint32_t off) {
  return (volatile uint32_t *)(base + off);
}

uint32_t pissa_read_status(uint32_t base) { return *pissa_reg(base, PISSA_REG_STATUS); }

int pissa_wait_done(uint32_t base, uint32_t timeout_ticks) {
  while (timeout_ticks--) {
    uint32_t status = pissa_read_status(base);
    if ((status & PISSA_STATUS_BUSY) == 0) {
      if (status & PISSA_STATUS_ERR) {
        return -1;
      }
      return 0;
    }
  }
  return -2;
}
