// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <string.h>

#include "z2_pissa.h"
#include "log.h"
#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/amiga_zorro.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"
#include "host/crypto/pi_crypto.h"

#define Z2_PISSA_SIZE 0x10000u

#define REG_CMD 0x00u
#define REG_STATUS 0x04u
#define REG_KEY_OFF 0x08u
#define REG_IV_OFF 0x0Cu
#define REG_AAD_OFF 0x10u
#define REG_AAD_LEN 0x14u
#define REG_SRC_OFF 0x18u
#define REG_DST_OFF 0x1Cu
#define REG_LEN_BYTES 0x20u
#define REG_TAG_OFF 0x24u

#define CMD_AES_GCM_ENC 0x01u
#define CMD_AES_GCM_DEC 0x02u

#define STATUS_BUSY 0x01u
#define STATUS_DONE 0x02u
#define STATUS_ERR 0x04u

#define PISSA_IV_LEN 12u
#define PISSA_TAG_LEN 16u

typedef struct {
  uint8_t mem[Z2_PISSA_SIZE];
  uint32_t cmd;
  uint32_t status;
  uint32_t key_off;
  uint32_t iv_off;
  uint32_t aad_off;
  uint32_t aad_len;
  uint32_t src_off;
  uint32_t dst_off;
  uint32_t len_bytes;
  uint32_t tag_off;
} z2_pissa_state_t;

static z2_pissa_state_t pissa_state;

static int pissa_range_ok(uint32_t off, uint32_t len) {
  if (len == 0) {
    return 1;
  }
  if (off >= Z2_PISSA_SIZE) {
    return 0;
  }
  if (off + len > Z2_PISSA_SIZE) {
    return 0;
  }
  return 1;
}

static void pissa_set_status(uint32_t status) { pissa_state.status = status; }

static uint32_t pissa_read_reg(uint32_t offset) {
  switch (offset) {
  case REG_CMD:
    return pissa_state.cmd;
  case REG_STATUS:
    return pissa_state.status;
  case REG_KEY_OFF:
    return pissa_state.key_off;
  case REG_IV_OFF:
    return pissa_state.iv_off;
  case REG_AAD_OFF:
    return pissa_state.aad_off;
  case REG_AAD_LEN:
    return pissa_state.aad_len;
  case REG_SRC_OFF:
    return pissa_state.src_off;
  case REG_DST_OFF:
    return pissa_state.dst_off;
  case REG_LEN_BYTES:
    return pissa_state.len_bytes;
  case REG_TAG_OFF:
    return pissa_state.tag_off;
  default:
    return 0;
  }
}

static void pissa_write_reg(uint32_t offset, uint32_t value) {
  switch (offset) {
  case REG_CMD:
    pissa_state.cmd = value & 0xFFu;
    break;
  case REG_STATUS:
    pissa_state.status = value;
    break;
  case REG_KEY_OFF:
    pissa_state.key_off = value;
    break;
  case REG_IV_OFF:
    pissa_state.iv_off = value;
    break;
  case REG_AAD_OFF:
    pissa_state.aad_off = value;
    break;
  case REG_AAD_LEN:
    pissa_state.aad_len = value;
    break;
  case REG_SRC_OFF:
    pissa_state.src_off = value;
    break;
  case REG_DST_OFF:
    pissa_state.dst_off = value;
    break;
  case REG_LEN_BYTES:
    pissa_state.len_bytes = value;
    break;
  case REG_TAG_OFF:
    pissa_state.tag_off = value;
    break;
  default:
    break;
  }
}

static void pissa_exec_cmd(void) {
  uint32_t cmd = pissa_state.cmd;
  if (cmd == 0) {
    return;
  }

  pissa_set_status(STATUS_BUSY);

  if (!pissa_range_ok(pissa_state.key_off, 32u) ||
      !pissa_range_ok(pissa_state.iv_off, PISSA_IV_LEN) ||
      !pissa_range_ok(pissa_state.src_off, pissa_state.len_bytes) ||
      !pissa_range_ok(pissa_state.dst_off, pissa_state.len_bytes) ||
      !pissa_range_ok(pissa_state.tag_off, PISSA_TAG_LEN)) {
    pissa_set_status(STATUS_DONE | STATUS_ERR);
    pissa_state.cmd = 0;
    return;
  }

  if (pissa_state.aad_len > 0 &&
      !pissa_range_ok(pissa_state.aad_off, pissa_state.aad_len)) {
    pissa_set_status(STATUS_DONE | STATUS_ERR);
    pissa_state.cmd = 0;
    return;
  }

  const uint8_t *key = &pissa_state.mem[pissa_state.key_off];
  const uint8_t *iv = &pissa_state.mem[pissa_state.iv_off];
  const uint8_t *aad = NULL;
  if (pissa_state.aad_len > 0) {
    aad = &pissa_state.mem[pissa_state.aad_off];
  }
  const uint8_t *src = &pissa_state.mem[pissa_state.src_off];
  uint8_t *dst = &pissa_state.mem[pissa_state.dst_off];
  uint8_t *tag = &pissa_state.mem[pissa_state.tag_off];

  int ok = 0;
  if (cmd == CMD_AES_GCM_ENC) {
    ok = pi_aes256_gcm_encrypt(key, iv, PISSA_IV_LEN, aad, pissa_state.aad_len,
                               src, pissa_state.len_bytes, dst, tag);
  } else if (cmd == CMD_AES_GCM_DEC) {
    ok = pi_aes256_gcm_decrypt(key, iv, PISSA_IV_LEN, aad, pissa_state.aad_len,
                               src, pissa_state.len_bytes, tag, dst);
  }

  if (!ok) {
    pissa_set_status(STATUS_DONE | STATUS_ERR);
  } else {
    pissa_set_status(STATUS_DONE);
  }

  pissa_state.cmd = 0;
}

static uint32_t pissa_read32_mem(uint32_t offset) {
  if (offset + 3u >= Z2_PISSA_SIZE) {
    return 0;
  }
  uint32_t v = ((uint32_t)pissa_state.mem[offset] << 24) |
               ((uint32_t)pissa_state.mem[offset + 1] << 16) |
               ((uint32_t)pissa_state.mem[offset + 2] << 8) |
               (uint32_t)pissa_state.mem[offset + 3];
  return v;
}

static void pissa_write32_mem(uint32_t offset, uint32_t value) {
  if (offset + 3u >= Z2_PISSA_SIZE) {
    return;
  }
  pissa_state.mem[offset] = (uint8_t)((value >> 24) & 0xFFu);
  pissa_state.mem[offset + 1] = (uint8_t)((value >> 16) & 0xFFu);
  pissa_state.mem[offset + 2] = (uint8_t)((value >> 8) & 0xFFu);
  pissa_state.mem[offset + 3] = (uint8_t)(value & 0xFFu);
}

static uint32_t z2_pissa_read32(zorro_device_t *dev, uint32_t offset) {
  (void)dev;
  if (offset <= REG_TAG_OFF) {
    return pissa_read_reg(offset);
  }
  return pissa_read32_mem(offset);
}

static uint16_t z2_pissa_read16(zorro_device_t *dev, uint32_t offset) {
  uint32_t base = offset & ~0x3u;
  uint32_t val = z2_pissa_read32(dev, base);
  if ((offset & 0x2u) != 0) {
    return (uint16_t)(val & 0xFFFFu);
  }
  return (uint16_t)((val >> 16) & 0xFFFFu);
}

static uint8_t z2_pissa_read8(zorro_device_t *dev, uint32_t offset) {
  uint32_t base = offset & ~0x3u;
  uint32_t val = z2_pissa_read32(dev, base);
  uint32_t shift = (3u - (offset & 0x3u)) * 8u;
  return (uint8_t)((val >> shift) & 0xFFu);
}

static void z2_pissa_write32(zorro_device_t *dev, uint32_t offset, uint32_t value) {
  (void)dev;
  if (offset <= REG_TAG_OFF) {
    pissa_write_reg(offset, value);
    if (offset == REG_CMD && (value & 0xFFu) != 0) {
      pissa_exec_cmd();
    }
    return;
  }
  pissa_write32_mem(offset, value);
}

static void z2_pissa_write16(zorro_device_t *dev, uint32_t offset, uint16_t value) {
  uint32_t base = offset & ~0x3u;
  uint32_t val32 = (uint32_t)value;
  if ((offset & 0x2u) == 0) {
    val32 <<= 16;
  }
  z2_pissa_write32(dev, base, val32);
}

static void z2_pissa_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  uint32_t base = offset & ~0x3u;
  uint32_t val32 = (uint32_t)value;
  uint32_t shift = (3u - (offset & 0x3u)) * 8u;
  val32 <<= shift;
  z2_pissa_write32(dev, base, val32);
}

static void z2_pissa_reset(zorro_device_t *dev) {
  (void)dev;
  memset(&pissa_state, 0, sizeof(pissa_state));
}

static uint8_t z2_pissa_rom[] = {
    Z2_Z2,
    AC_MEM_SIZE_64KB,
    0x1,
    0x0,
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

static zorro_device_t z2_pissa_device = {
    .name = "z2-pissa",
    .bus = ZORRO_BUS_Z2,
    .size = Z2_PISSA_SIZE,
    .manufacturer = 0x07DB,
    .product = 0x0003,
    .flags = 0,
    .ac_rom = z2_pissa_rom,
    .ac_rom_size = sizeof(z2_pissa_rom),
    .reset = z2_pissa_reset,
    .read8 = z2_pissa_read8,
    .read16 = z2_pissa_read16,
    .read32 = z2_pissa_read32,
    .write8 = z2_pissa_write8,
    .write16 = z2_pissa_write16,
    .write32 = z2_pissa_write32,
    .priv = &pissa_state,
};

void z2_pissa_register(void) {
  LOG_INFO("[ZORRO] Registering Z2 PISSA crypto device.\n");
  int slot = zorro_register_device(&z2_pissa_device);
  if (slot < 0) {
    LOG_INFO("[ZORRO] Failed to register Z2 PISSA crypto device.\n");
  }
}
