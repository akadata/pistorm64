// SPDX-License-Identifier: MIT
#include <string.h>
#include <time.h>

#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/amiga_zorro.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"
#include "ppc_accel.h"
#include "ppc_accel_regs.h"
#include "log.h"

typedef struct ppc_accel_state {
  uint8_t window[PPC_ACCEL_Z2_SIZE];
  uint32_t control;
  uint32_t status;
  uint32_t irq_status;
} ppc_accel_state_t;

static ppc_accel_state_t g_ppc_accel_state;

static uint8_t ppc_accel_rom[] = {
    Z2_Z2,
    AC_MEM_SIZE_64KB,
    0x4, /* product high nibble */
    0x0, /* product low nibble */
    0x0,
    0x0,
    0x0,
    0x0,
    PISTORM_AC_MANUF_ID,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
};

static uint32_t read_be32(const uint8_t *base, uint32_t offset) {
  return ((uint32_t)base[offset + 0] << 24)
         | ((uint32_t)base[offset + 1] << 16)
         | ((uint32_t)base[offset + 2] << 8)
         | (uint32_t)base[offset + 3];
}

static void write_be32(uint8_t *base, uint32_t offset, uint32_t value) {
  base[offset + 0] = (uint8_t)((value >> 24) & 0xFFu);
  base[offset + 1] = (uint8_t)((value >> 16) & 0xFFu);
  base[offset + 2] = (uint8_t)((value >> 8) & 0xFFu);
  base[offset + 3] = (uint8_t)(value & 0xFFu);
}

static uint32_t monotonic_time32(void) {
  struct timespec ts;
  uint64_t ns;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0u;
  }
  ns = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
  return (uint32_t)(ns & 0xFFFFFFFFu);
}

static void ppc_accel_mailbox_reset(ppc_accel_state_t *state) {
  memset(state->window, 0, sizeof(state->window));
  write_be32(state->window, PPC_ACCEL_MAILBOX_OFFSET + PPC_ACCEL_MB_OFF_MAGIC, PPC_ACCEL_MB_MAGIC);
  write_be32(state->window, PPC_ACCEL_MAILBOX_OFFSET + PPC_ACCEL_MB_OFF_VERSION, PPC_ACCEL_MB_VERSION);
  write_be32(state->window, PPC_ACCEL_MAILBOX_OFFSET + PPC_ACCEL_MB_OFF_STATUS, PPC_ACCEL_MB_STATUS_IDLE);
}

static void ppc_accel_reset(zorro_device_t *dev) {
  ppc_accel_state_t *state;

  state = (ppc_accel_state_t *)dev->priv;
  state->control = 0u;
  state->status = 0u;
  state->irq_status = 0u;
  ppc_accel_mailbox_reset(state);
}

static void ppc_accel_process_mailbox(ppc_accel_state_t *state) {
  uint32_t seq;
  uint32_t ack_seq;
  uint32_t cmd;
  uint32_t arg0;
  uint32_t result0;
  uint32_t status;
  uint32_t mb;

  if ((state->status & PPC_ACCEL_STATUS_RUNNING) == 0u) {
    return;
  }

  mb = PPC_ACCEL_MAILBOX_OFFSET;
  seq = read_be32(state->window, mb + PPC_ACCEL_MB_OFF_SEQ);
  ack_seq = read_be32(state->window, mb + PPC_ACCEL_MB_OFF_ACK_SEQ);
  if (seq == ack_seq) {
    return;
  }

  cmd = read_be32(state->window, mb + PPC_ACCEL_MB_OFF_CMD);
  arg0 = read_be32(state->window, mb + PPC_ACCEL_MB_OFF_ARG0);
  result0 = 0u;
  status = PPC_ACCEL_MB_STATUS_ERR;

  write_be32(state->window, mb + PPC_ACCEL_MB_OFF_STATUS, PPC_ACCEL_MB_STATUS_BUSY);
  if (cmd == PPC_ACCEL_MB_CMD_PING) {
    result0 = arg0 ^ 0xFFFFFFFFu;
    status = PPC_ACCEL_MB_STATUS_DONE;
  } else if (cmd == PPC_ACCEL_MB_CMD_HOST_TIME32) {
    result0 = monotonic_time32();
    status = PPC_ACCEL_MB_STATUS_DONE;
  } else {
    status = PPC_ACCEL_MB_STATUS_ERR;
  }

  write_be32(state->window, mb + PPC_ACCEL_MB_OFF_RESULT0, result0);
  write_be32(state->window, mb + PPC_ACCEL_MB_OFF_RESULT1, 0u);
  write_be32(state->window, mb + PPC_ACCEL_MB_OFF_STATUS, status);
  write_be32(state->window, mb + PPC_ACCEL_MB_OFF_ACK_SEQ, seq);

  if ((state->control & PPC_ACCEL_CTRL_IRQ_ENABLE) != 0u) {
    state->irq_status |= PPC_ACCEL_IRQ_CMD_DONE;
  }
}

static uint32_t ppc_accel_reg_read32(ppc_accel_state_t *state, uint32_t offset) {
  switch (offset) {
  case PPC_ACCEL_REG_MAGIC:
    return PPC_ACCEL_MAGIC;
  case PPC_ACCEL_REG_ABI_VERSION:
    return PPC_ACCEL_ABI_VERSION;
  case PPC_ACCEL_REG_CONTROL:
    return state->control;
  case PPC_ACCEL_REG_STATUS:
    return state->status;
  case PPC_ACCEL_REG_DOORBELL:
    return 0u;
  case PPC_ACCEL_REG_IRQ_STATUS:
    return state->irq_status;
  case PPC_ACCEL_REG_IRQ_ACK:
    return 0u;
  case PPC_ACCEL_REG_MAILBOX_OFFSET:
    return PPC_ACCEL_MAILBOX_OFFSET;
  case PPC_ACCEL_REG_MAILBOX_SIZE:
    return PPC_ACCEL_MAILBOX_SIZE;
  case PPC_ACCEL_REG_SHARED_OFFSET:
    return PPC_ACCEL_SHARED_OFFSET;
  case PPC_ACCEL_REG_SHARED_SIZE:
    return PPC_ACCEL_SHARED_SIZE;
  default:
    return read_be32(state->window, offset);
  }
}

static void ppc_accel_reg_write32(ppc_accel_state_t *state, uint32_t offset, uint32_t value) {
  switch (offset) {
  case PPC_ACCEL_REG_CONTROL: {
    uint32_t new_control;

    new_control = value & (PPC_ACCEL_CTRL_START | PPC_ACCEL_CTRL_RESET | PPC_ACCEL_CTRL_IRQ_ENABLE);
    if ((new_control & PPC_ACCEL_CTRL_RESET) != 0u) {
      uint32_t keep_irq;

      keep_irq = new_control & PPC_ACCEL_CTRL_IRQ_ENABLE;
      state->control = keep_irq;
      state->status = 0u;
      state->irq_status = 0u;
      ppc_accel_mailbox_reset(state);
    } else {
      state->control = new_control;
      if ((state->control & PPC_ACCEL_CTRL_START) != 0u) {
        state->status |= PPC_ACCEL_STATUS_RUNNING;
      } else {
        state->status &= ~PPC_ACCEL_STATUS_RUNNING;
      }
    }
    break;
  }
  case PPC_ACCEL_REG_DOORBELL:
    state->irq_status |= PPC_ACCEL_IRQ_HOST_DOORBELL;
    break;
  case PPC_ACCEL_REG_IRQ_ACK:
    state->irq_status &= ~value;
    break;
  case PPC_ACCEL_REG_STATUS:
    /* status is host-owned */
    break;
  case PPC_ACCEL_REG_MAGIC:
  case PPC_ACCEL_REG_ABI_VERSION:
  case PPC_ACCEL_REG_MAILBOX_OFFSET:
  case PPC_ACCEL_REG_MAILBOX_SIZE:
  case PPC_ACCEL_REG_SHARED_OFFSET:
  case PPC_ACCEL_REG_SHARED_SIZE:
    /* read-only */
    break;
  default:
    write_be32(state->window, offset, value);
    break;
  }
}

static uint8_t ppc_accel_read8(zorro_device_t *dev, uint32_t offset) {
  ppc_accel_state_t *state;
  uint32_t reg_base;
  uint32_t reg_value;
  uint32_t shift;

  state = (ppc_accel_state_t *)dev->priv;
  if (offset >= PPC_ACCEL_Z2_SIZE) {
    return 0xFFu;
  }

  ppc_accel_process_mailbox(state);
  if (offset < PPC_ACCEL_REG_WINDOW_SIZE) {
    reg_base = offset & ~0x3u;
    reg_value = ppc_accel_reg_read32(state, reg_base);
    shift = (3u - (offset & 0x3u)) * 8u;
    return (uint8_t)((reg_value >> shift) & 0xFFu);
  }

  return state->window[offset];
}

static uint16_t ppc_accel_read16(zorro_device_t *dev, uint32_t offset) {
  uint16_t hi;
  uint16_t lo;

  hi = (uint16_t)ppc_accel_read8(dev, offset + 0u);
  lo = (uint16_t)ppc_accel_read8(dev, offset + 1u);
  return (uint16_t)((hi << 8) | lo);
}

static uint32_t ppc_accel_read32(zorro_device_t *dev, uint32_t offset) {
  ppc_accel_state_t *state;

  state = (ppc_accel_state_t *)dev->priv;
  if ((offset + 3u) >= PPC_ACCEL_Z2_SIZE) {
    return 0xFFFFFFFFu;
  }

  ppc_accel_process_mailbox(state);
  if ((offset < PPC_ACCEL_REG_WINDOW_SIZE) && ((offset & 0x3u) == 0u)) {
    return ppc_accel_reg_read32(state, offset);
  }

  return ((uint32_t)ppc_accel_read8(dev, offset + 0u) << 24)
         | ((uint32_t)ppc_accel_read8(dev, offset + 1u) << 16)
         | ((uint32_t)ppc_accel_read8(dev, offset + 2u) << 8)
         | (uint32_t)ppc_accel_read8(dev, offset + 3u);
}

static void ppc_accel_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  ppc_accel_state_t *state;
  uint32_t reg_base;
  uint32_t reg_value;
  uint32_t shift;
  uint32_t mask;

  state = (ppc_accel_state_t *)dev->priv;
  if (offset >= PPC_ACCEL_Z2_SIZE) {
    return;
  }

  if (offset < PPC_ACCEL_REG_WINDOW_SIZE) {
    reg_base = offset & ~0x3u;
    reg_value = ppc_accel_reg_read32(state, reg_base);
    shift = (3u - (offset & 0x3u)) * 8u;
    mask = 0xFFu << shift;
    reg_value = (reg_value & ~mask) | ((uint32_t)value << shift);
    ppc_accel_reg_write32(state, reg_base, reg_value);
  } else {
    state->window[offset] = value;
  }

  ppc_accel_process_mailbox(state);
}

static void ppc_accel_write16(zorro_device_t *dev, uint32_t offset, uint16_t value) {
  ppc_accel_state_t *state;
  uint32_t reg_base;
  uint32_t reg_value;
  uint32_t shift;
  uint32_t mask;

  state = (ppc_accel_state_t *)dev->priv;
  if ((offset + 1u) >= PPC_ACCEL_Z2_SIZE) {
    return;
  }

  if ((offset & 0x1u) != 0u) {
    ppc_accel_write8(dev, offset + 0u, (uint8_t)((value >> 8) & 0xFFu));
    ppc_accel_write8(dev, offset + 1u, (uint8_t)(value & 0xFFu));
    return;
  }

  if (offset < PPC_ACCEL_REG_WINDOW_SIZE) {
    reg_base = offset & ~0x3u;
    reg_value = ppc_accel_reg_read32(state, reg_base);
    shift = (2u - (offset & 0x2u)) * 8u;
    mask = 0xFFFFu << shift;
    reg_value = (reg_value & ~mask) | ((uint32_t)value << shift);
    ppc_accel_reg_write32(state, reg_base, reg_value);
  } else {
    state->window[offset + 0u] = (uint8_t)((value >> 8) & 0xFFu);
    state->window[offset + 1u] = (uint8_t)(value & 0xFFu);
  }

  ppc_accel_process_mailbox(state);
}

static void ppc_accel_write32(zorro_device_t *dev, uint32_t offset, uint32_t value) {
  ppc_accel_state_t *state;

  state = (ppc_accel_state_t *)dev->priv;
  if ((offset + 3u) >= PPC_ACCEL_Z2_SIZE) {
    return;
  }

  if ((offset < PPC_ACCEL_REG_WINDOW_SIZE) && ((offset & 0x3u) == 0u)) {
    ppc_accel_reg_write32(state, offset, value);
  } else {
    state->window[offset + 0u] = (uint8_t)((value >> 24) & 0xFFu);
    state->window[offset + 1u] = (uint8_t)((value >> 16) & 0xFFu);
    state->window[offset + 2u] = (uint8_t)((value >> 8) & 0xFFu);
    state->window[offset + 3u] = (uint8_t)(value & 0xFFu);
  }

  ppc_accel_process_mailbox(state);
}

static zorro_device_t z2_ppc_accel_device = {
    .name = "z2-ppc-accel",
    .bus = ZORRO_BUS_Z2,
    .size = PPC_ACCEL_Z2_SIZE,
    .manufacturer = PISTORM_MANUF_ID,
    .product = PISTORM_PROD_PPC_ACCEL_Z2,
    .flags = 0,
    .ac_rom = ppc_accel_rom,
    .ac_rom_size = sizeof(ppc_accel_rom),
    .reset = ppc_accel_reset,
    .read8 = ppc_accel_read8,
    .read16 = ppc_accel_read16,
    .read32 = ppc_accel_read32,
    .write8 = ppc_accel_write8,
    .write16 = ppc_accel_write16,
    .write32 = ppc_accel_write32,
    .priv = &g_ppc_accel_state,
};

void z2_ppc_accel_register(void) {
  int slot;

  LOG_INFO("[ZORRO] Registering Z2 PPC accelerator device.\n");
  ppc_accel_reset(&z2_ppc_accel_device);
  slot = zorro_register_device(&z2_ppc_accel_device);
  if (slot < 0) {
    LOG_INFO("[ZORRO] Failed to register Z2 PPC accelerator device.\n");
  }
}
