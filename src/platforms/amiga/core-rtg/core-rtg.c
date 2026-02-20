// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <string.h>
#include "core-rtg.h"
#include "platforms/amiga/rtg/rtg.h"
#include "log.h"
#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"

#define CORE_RTG_Z2_SIZE (CORE_RTG_REG_SIZE + CORE_RTG_Z2_MEM_SIZE)
#define CORE_RTG_Z3_SIZE (CORE_RTG_REG_SIZE + CORE_RTG_Z3_MEM_SIZE)

extern uint8_t rtg_on;
extern uint8_t display_enabled;

extern zorro_device_t core_rtg_device;

static inline uint32_t read_be32(const uint8_t *ptr) {
  uint32_t tmp;
  memcpy(&tmp, ptr, sizeof(tmp));
  return ((tmp >> 24) & 0x000000FFu) |
         ((tmp >> 8)  & 0x0000FF00u) |
         ((tmp << 8)  & 0x00FF0000u) |
         ((tmp << 24) & 0xFF000000u);
}

static inline void write_be16(uint8_t *ptr, uint16_t value) {
  uint16_t tmp = (uint16_t)((value >> 8) | (value << 8));
  memcpy(ptr, &tmp, sizeof(tmp));
}

static inline void write_be32(uint8_t *ptr, uint32_t value) {
  uint32_t tmp = ((value >> 24) & 0x000000FFu) |
                 ((value >> 8)  & 0x0000FF00u) |
                 ((value << 8)  & 0x00FF0000u) |
                 ((value << 24) & 0xFF000000u);
  memcpy(ptr, &tmp, sizeof(tmp));
}

static core_rtg_state_t core_rtg_state;
static struct rtg_shared_data core_rtg_share_data;
static uint32_t core_rtg_clut_log = 0;
static uint32_t core_rtg_vram_log = 0;
static uint32_t core_rtg_vram_writes = 0;
static uint8_t core_rtg_pending_enable = 0;

static int core_rtg_ready(void) {
  if (core_rtg_state.fb_width == 0 || core_rtg_state.fb_height == 0) {
    return 0;
  }
  if (core_rtg_state.fb_pitch == 0) {
    return 0;
  }
  if (core_rtg_state.fb_format >= RTGFMT_NUM) {
    return 0;
  }
  if (core_rtg_state.vram == NULL || core_rtg_state.vram_size == 0) {
    return 0;
  }
  return 1;
}

static void core_rtg_update_addr_offset(void) {
  uint32_t base = core_rtg_device.base + CORE_RTG_REG_SIZE;
  uint32_t raw = core_rtg_state.fb_addr;
  if (core_rtg_device.base == 0 || raw < base) {
    core_rtg_state.fb_addr_offset = raw;
  } else {
    core_rtg_state.fb_addr_offset = raw - base;
  }
}

static uint32_t core_rtg_read_reg(uint32_t offset) {
  switch (offset) {
  case CORE_RTG_REG_MAGIC:
    return CORE_RTG_MAGIC;
  case CORE_RTG_REG_VERSION:
    return CORE_RTG_VERSION;
  case CORE_RTG_REG_STATUS:
    return core_rtg_state.enabled ? 1u : 0u;
  case CORE_RTG_REG_FB_ADDR:
    return core_rtg_state.fb_addr;
  case CORE_RTG_REG_FB_PITCH:
    return core_rtg_state.fb_pitch;
  case CORE_RTG_REG_FB_WIDTH:
    return core_rtg_state.fb_width;
  case CORE_RTG_REG_FB_HEIGHT:
    return core_rtg_state.fb_height;
  case CORE_RTG_REG_FB_FORMAT:
    return core_rtg_state.fb_format;
  case CORE_RTG_REG_PAN_X:
    return core_rtg_state.pan_x;
  case CORE_RTG_REG_PAN_Y:
    return core_rtg_state.pan_y;
  case CORE_RTG_REG_DISP_W:
    return core_rtg_state.disp_width;
  case CORE_RTG_REG_DISP_H:
    return core_rtg_state.disp_height;
  case CORE_RTG_REG_SCALE_X:
    return core_rtg_state.scale_x;
  case CORE_RTG_REG_SCALE_Y:
    return core_rtg_state.scale_y;
  case CORE_RTG_REG_CLUT_INDEX:
    return core_rtg_state.clut_index;
  default:
    return 0;
  }
}

static void core_rtg_write_reg(uint32_t offset, uint32_t value) {
  switch (offset) {
  case CORE_RTG_REG_STATUS:
    core_rtg_state.enabled = (uint8_t)(value & 0x1u);
    if (core_rtg_state.enabled) {
      if (!core_rtg_ready()) {
        LOG_WARN("[CORE-RTG] Enable requested but params not ready (w=%u h=%u fmt=%u pitch=%u addr=0x%08X)\n",
                 core_rtg_state.fb_width, core_rtg_state.fb_height, core_rtg_state.fb_format,
                 core_rtg_state.fb_pitch, core_rtg_state.fb_addr);
        core_rtg_state.enabled = 0;
        core_rtg_pending_enable = 1;
        break;
      }
      core_rtg_pending_enable = 0;
      core_rtg_update_addr_offset();
      core_rtg_state.fb_min_off = 0xFFFFFFFFu;
      core_rtg_state.fb_max_off = 0u;
      core_rtg_state.fb_last_off = 0u;
      core_rtg_state.fb_write_hits = 0u;
      core_rtg_state.vram_min_off = 0xFFFFFFFFu;
      core_rtg_state.vram_max_off = 0u;
      core_rtg_state.vram_last_off = 0u;
      LOG_INFO("[CORE-RTG] Enable w=%u h=%u fmt=%u pitch=%u addr=0x%08X off=0x%08X base=0x%08X\n",
               core_rtg_state.fb_width, core_rtg_state.fb_height, core_rtg_state.fb_format,
               core_rtg_state.fb_pitch, core_rtg_state.fb_addr, core_rtg_state.fb_addr_offset,
               core_rtg_device.base);
      core_rtg_share_data.width = &core_rtg_state.fb_width;
      core_rtg_share_data.height = &core_rtg_state.fb_height;
      core_rtg_share_data.format = &core_rtg_state.fb_format;
      core_rtg_share_data.pitch = &core_rtg_state.fb_pitch;
      core_rtg_share_data.offset_x = &core_rtg_state.pan_x;
      core_rtg_share_data.offset_y = &core_rtg_state.pan_y;
      core_rtg_share_data.memory = core_rtg_state.vram;
      core_rtg_share_data.addr = &core_rtg_state.fb_addr_offset;
      core_rtg_share_data.running = &rtg_on;
      core_rtg_share_data.vram_writes = &core_rtg_vram_writes;
      core_rtg_share_data.fb_writes = &core_rtg_state.fb_write_hits;
      core_rtg_share_data.fb_min_off = &core_rtg_state.fb_min_off;
      core_rtg_share_data.fb_max_off = &core_rtg_state.fb_max_off;
      core_rtg_share_data.fb_last_off = &core_rtg_state.fb_last_off;
      core_rtg_share_data.vram_min_off = &core_rtg_state.vram_min_off;
      core_rtg_share_data.vram_max_off = &core_rtg_state.vram_max_off;
      core_rtg_share_data.vram_last_off = &core_rtg_state.vram_last_off;
      rtg_output_set_source(&core_rtg_share_data);
      rtg_on = 1;
      display_enabled = 1;
      rtg_init_display();
    } else {
      rtg_shutdown_display();
      rtg_output_clear_source();
    }
    break;
  case CORE_RTG_REG_CMD:
    LOG_INFO("[CORE-RTG] CMD=0x%08X\n", value);
    break;
  case CORE_RTG_REG_FB_ADDR:
    core_rtg_state.fb_addr = value;
    core_rtg_update_addr_offset();
    LOG_DEBUG("[CORE-RTG] FB addr=0x%08X off=0x%08X\n", core_rtg_state.fb_addr,
              core_rtg_state.fb_addr_offset);
    if (core_rtg_pending_enable && core_rtg_ready()) {
      core_rtg_pending_enable = 0;
      core_rtg_state.enabled = 1;
      core_rtg_update_addr_offset();
      core_rtg_state.fb_min_off = 0xFFFFFFFFu;
      core_rtg_state.fb_max_off = 0u;
      core_rtg_state.fb_last_off = 0u;
      core_rtg_state.fb_write_hits = 0u;
      core_rtg_state.vram_min_off = 0xFFFFFFFFu;
      core_rtg_state.vram_max_off = 0u;
      core_rtg_state.vram_last_off = 0u;
      LOG_INFO("[CORE-RTG] Enable (deferred) w=%u h=%u fmt=%u pitch=%u addr=0x%08X off=0x%08X base=0x%08X\n",
               core_rtg_state.fb_width, core_rtg_state.fb_height, core_rtg_state.fb_format,
               core_rtg_state.fb_pitch, core_rtg_state.fb_addr, core_rtg_state.fb_addr_offset,
               core_rtg_device.base);
      core_rtg_share_data.width = &core_rtg_state.fb_width;
      core_rtg_share_data.height = &core_rtg_state.fb_height;
      core_rtg_share_data.format = &core_rtg_state.fb_format;
      core_rtg_share_data.pitch = &core_rtg_state.fb_pitch;
      core_rtg_share_data.offset_x = &core_rtg_state.pan_x;
      core_rtg_share_data.offset_y = &core_rtg_state.pan_y;
      core_rtg_share_data.memory = core_rtg_state.vram;
      core_rtg_share_data.addr = &core_rtg_state.fb_addr_offset;
      core_rtg_share_data.running = &rtg_on;
      core_rtg_share_data.vram_writes = &core_rtg_vram_writes;
      core_rtg_share_data.fb_writes = &core_rtg_state.fb_write_hits;
      core_rtg_share_data.fb_min_off = &core_rtg_state.fb_min_off;
      core_rtg_share_data.fb_max_off = &core_rtg_state.fb_max_off;
      core_rtg_share_data.fb_last_off = &core_rtg_state.fb_last_off;
      core_rtg_share_data.vram_min_off = &core_rtg_state.vram_min_off;
      core_rtg_share_data.vram_max_off = &core_rtg_state.vram_max_off;
      core_rtg_share_data.vram_last_off = &core_rtg_state.vram_last_off;
      rtg_output_set_source(&core_rtg_share_data);
      rtg_on = 1;
      display_enabled = 1;
      rtg_init_display();
    }
    break;
  case CORE_RTG_REG_FB_PITCH:
    core_rtg_state.fb_pitch = value;
    if (core_rtg_pending_enable && core_rtg_ready()) {
      core_rtg_pending_enable = 0;
      core_rtg_state.enabled = 1;
      core_rtg_update_addr_offset();
      core_rtg_state.fb_min_off = 0xFFFFFFFFu;
      core_rtg_state.fb_max_off = 0u;
      core_rtg_state.fb_last_off = 0u;
      core_rtg_state.fb_write_hits = 0u;
      core_rtg_state.vram_min_off = 0xFFFFFFFFu;
      core_rtg_state.vram_max_off = 0u;
      core_rtg_state.vram_last_off = 0u;
      LOG_INFO("[CORE-RTG] Enable (deferred) w=%u h=%u fmt=%u pitch=%u addr=0x%08X off=0x%08X base=0x%08X\n",
               core_rtg_state.fb_width, core_rtg_state.fb_height, core_rtg_state.fb_format,
               core_rtg_state.fb_pitch, core_rtg_state.fb_addr, core_rtg_state.fb_addr_offset,
               core_rtg_device.base);
      core_rtg_share_data.width = &core_rtg_state.fb_width;
      core_rtg_share_data.height = &core_rtg_state.fb_height;
      core_rtg_share_data.format = &core_rtg_state.fb_format;
      core_rtg_share_data.pitch = &core_rtg_state.fb_pitch;
      core_rtg_share_data.offset_x = &core_rtg_state.pan_x;
      core_rtg_share_data.offset_y = &core_rtg_state.pan_y;
      core_rtg_share_data.memory = core_rtg_state.vram;
      core_rtg_share_data.addr = &core_rtg_state.fb_addr_offset;
      core_rtg_share_data.running = &rtg_on;
      core_rtg_share_data.vram_writes = &core_rtg_vram_writes;
      core_rtg_share_data.fb_writes = &core_rtg_state.fb_write_hits;
      core_rtg_share_data.fb_min_off = &core_rtg_state.fb_min_off;
      core_rtg_share_data.fb_max_off = &core_rtg_state.fb_max_off;
      core_rtg_share_data.fb_last_off = &core_rtg_state.fb_last_off;
      core_rtg_share_data.vram_min_off = &core_rtg_state.vram_min_off;
      core_rtg_share_data.vram_max_off = &core_rtg_state.vram_max_off;
      core_rtg_share_data.vram_last_off = &core_rtg_state.vram_last_off;
      rtg_output_set_source(&core_rtg_share_data);
      rtg_on = 1;
      display_enabled = 1;
      rtg_init_display();
    }
    break;
  case CORE_RTG_REG_FB_WIDTH:
    core_rtg_state.fb_width = (uint16_t)(value & 0xFFFFu);
    if (core_rtg_pending_enable && core_rtg_ready()) {
      core_rtg_pending_enable = 0;
      core_rtg_state.enabled = 1;
      core_rtg_update_addr_offset();
      core_rtg_state.fb_min_off = 0xFFFFFFFFu;
      core_rtg_state.fb_max_off = 0u;
      core_rtg_state.fb_last_off = 0u;
      core_rtg_state.fb_write_hits = 0u;
      core_rtg_state.vram_min_off = 0xFFFFFFFFu;
      core_rtg_state.vram_max_off = 0u;
      core_rtg_state.vram_last_off = 0u;
      LOG_INFO("[CORE-RTG] Enable (deferred) w=%u h=%u fmt=%u pitch=%u addr=0x%08X off=0x%08X base=0x%08X\n",
               core_rtg_state.fb_width, core_rtg_state.fb_height, core_rtg_state.fb_format,
               core_rtg_state.fb_pitch, core_rtg_state.fb_addr, core_rtg_state.fb_addr_offset,
               core_rtg_device.base);
      core_rtg_share_data.width = &core_rtg_state.fb_width;
      core_rtg_share_data.height = &core_rtg_state.fb_height;
      core_rtg_share_data.format = &core_rtg_state.fb_format;
      core_rtg_share_data.pitch = &core_rtg_state.fb_pitch;
      core_rtg_share_data.offset_x = &core_rtg_state.pan_x;
      core_rtg_share_data.offset_y = &core_rtg_state.pan_y;
      core_rtg_share_data.memory = core_rtg_state.vram;
      core_rtg_share_data.addr = &core_rtg_state.fb_addr_offset;
      core_rtg_share_data.running = &rtg_on;
      core_rtg_share_data.vram_writes = &core_rtg_vram_writes;
      core_rtg_share_data.fb_writes = &core_rtg_state.fb_write_hits;
      core_rtg_share_data.fb_min_off = &core_rtg_state.fb_min_off;
      core_rtg_share_data.fb_max_off = &core_rtg_state.fb_max_off;
      core_rtg_share_data.fb_last_off = &core_rtg_state.fb_last_off;
      core_rtg_share_data.vram_min_off = &core_rtg_state.vram_min_off;
      core_rtg_share_data.vram_max_off = &core_rtg_state.vram_max_off;
      core_rtg_share_data.vram_last_off = &core_rtg_state.vram_last_off;
      rtg_output_set_source(&core_rtg_share_data);
      rtg_on = 1;
      display_enabled = 1;
      rtg_init_display();
    }
    break;
  case CORE_RTG_REG_FB_HEIGHT:
    core_rtg_state.fb_height = (uint16_t)(value & 0xFFFFu);
    if (core_rtg_pending_enable && core_rtg_ready()) {
      core_rtg_pending_enable = 0;
      core_rtg_state.enabled = 1;
      core_rtg_update_addr_offset();
      core_rtg_state.fb_min_off = 0xFFFFFFFFu;
      core_rtg_state.fb_max_off = 0u;
      core_rtg_state.fb_last_off = 0u;
      core_rtg_state.fb_write_hits = 0u;
      core_rtg_state.vram_min_off = 0xFFFFFFFFu;
      core_rtg_state.vram_max_off = 0u;
      core_rtg_state.vram_last_off = 0u;
      LOG_INFO("[CORE-RTG] Enable (deferred) w=%u h=%u fmt=%u pitch=%u addr=0x%08X off=0x%08X base=0x%08X\n",
               core_rtg_state.fb_width, core_rtg_state.fb_height, core_rtg_state.fb_format,
               core_rtg_state.fb_pitch, core_rtg_state.fb_addr, core_rtg_state.fb_addr_offset,
               core_rtg_device.base);
      core_rtg_share_data.width = &core_rtg_state.fb_width;
      core_rtg_share_data.height = &core_rtg_state.fb_height;
      core_rtg_share_data.format = &core_rtg_state.fb_format;
      core_rtg_share_data.pitch = &core_rtg_state.fb_pitch;
      core_rtg_share_data.offset_x = &core_rtg_state.pan_x;
      core_rtg_share_data.offset_y = &core_rtg_state.pan_y;
      core_rtg_share_data.memory = core_rtg_state.vram;
      core_rtg_share_data.addr = &core_rtg_state.fb_addr_offset;
      core_rtg_share_data.running = &rtg_on;
      core_rtg_share_data.vram_writes = &core_rtg_vram_writes;
      core_rtg_share_data.fb_writes = &core_rtg_state.fb_write_hits;
      core_rtg_share_data.fb_min_off = &core_rtg_state.fb_min_off;
      core_rtg_share_data.fb_max_off = &core_rtg_state.fb_max_off;
      core_rtg_share_data.fb_last_off = &core_rtg_state.fb_last_off;
      core_rtg_share_data.vram_min_off = &core_rtg_state.vram_min_off;
      core_rtg_share_data.vram_max_off = &core_rtg_state.vram_max_off;
      core_rtg_share_data.vram_last_off = &core_rtg_state.vram_last_off;
      rtg_output_set_source(&core_rtg_share_data);
      rtg_on = 1;
      display_enabled = 1;
      rtg_init_display();
    }
    break;
  case CORE_RTG_REG_FB_FORMAT:
    core_rtg_state.fb_format = (uint16_t)(value & 0xFFFFu);
    if (core_rtg_pending_enable && core_rtg_ready()) {
      core_rtg_pending_enable = 0;
      core_rtg_state.enabled = 1;
      core_rtg_update_addr_offset();
      core_rtg_state.fb_min_off = 0xFFFFFFFFu;
      core_rtg_state.fb_max_off = 0u;
      core_rtg_state.fb_last_off = 0u;
      core_rtg_state.fb_write_hits = 0u;
      core_rtg_state.vram_min_off = 0xFFFFFFFFu;
      core_rtg_state.vram_max_off = 0u;
      core_rtg_state.vram_last_off = 0u;
      LOG_INFO("[CORE-RTG] Enable (deferred) w=%u h=%u fmt=%u pitch=%u addr=0x%08X off=0x%08X base=0x%08X\n",
               core_rtg_state.fb_width, core_rtg_state.fb_height, core_rtg_state.fb_format,
               core_rtg_state.fb_pitch, core_rtg_state.fb_addr, core_rtg_state.fb_addr_offset,
               core_rtg_device.base);
      core_rtg_share_data.width = &core_rtg_state.fb_width;
      core_rtg_share_data.height = &core_rtg_state.fb_height;
      core_rtg_share_data.format = &core_rtg_state.fb_format;
      core_rtg_share_data.pitch = &core_rtg_state.fb_pitch;
      core_rtg_share_data.offset_x = &core_rtg_state.pan_x;
      core_rtg_share_data.offset_y = &core_rtg_state.pan_y;
      core_rtg_share_data.memory = core_rtg_state.vram;
      core_rtg_share_data.addr = &core_rtg_state.fb_addr_offset;
      core_rtg_share_data.running = &rtg_on;
      core_rtg_share_data.vram_writes = &core_rtg_vram_writes;
      core_rtg_share_data.fb_writes = &core_rtg_state.fb_write_hits;
      core_rtg_share_data.fb_min_off = &core_rtg_state.fb_min_off;
      core_rtg_share_data.fb_max_off = &core_rtg_state.fb_max_off;
      core_rtg_share_data.fb_last_off = &core_rtg_state.fb_last_off;
      core_rtg_share_data.vram_min_off = &core_rtg_state.vram_min_off;
      core_rtg_share_data.vram_max_off = &core_rtg_state.vram_max_off;
      core_rtg_share_data.vram_last_off = &core_rtg_state.vram_last_off;
      rtg_output_set_source(&core_rtg_share_data);
      rtg_on = 1;
      display_enabled = 1;
      rtg_init_display();
    }
    break;
  case CORE_RTG_REG_PAN_X:
    core_rtg_state.pan_x = (uint16_t)(value & 0xFFFFu);
    LOG_DEBUG("[CORE-RTG] PAN_X=%u\n", core_rtg_state.pan_x);
    break;
  case CORE_RTG_REG_PAN_Y:
    core_rtg_state.pan_y = (uint16_t)(value & 0xFFFFu);
    LOG_DEBUG("[CORE-RTG] PAN_Y=%u\n", core_rtg_state.pan_y);
    break;
  case CORE_RTG_REG_DISP_W:
    core_rtg_state.disp_width = (uint16_t)(value & 0xFFFFu);
    break;
  case CORE_RTG_REG_DISP_H:
    core_rtg_state.disp_height = (uint16_t)(value & 0xFFFFu);
    break;
  case CORE_RTG_REG_SCALE_X:
    core_rtg_state.scale_x = value;
    break;
  case CORE_RTG_REG_SCALE_Y:
    core_rtg_state.scale_y = value;
    break;
  case CORE_RTG_REG_CLUT_INDEX:
    core_rtg_state.clut_index = (uint8_t)(value & 0xFFu);
    break;
  case CORE_RTG_REG_CLUT_RGB:
    rtg_set_clut_entry(core_rtg_state.clut_index, value);
    if (core_rtg_clut_log < 8) {
      LOG_INFO("[CORE-RTG] CLUT[%u] = %06X\n", core_rtg_state.clut_index, value & 0xFFFFFFu);
      core_rtg_clut_log++;
    }
    break;
  default:
    break;
  }
}

static uint32_t core_rtg_read32(zorro_device_t *dev, uint32_t offset) {
  (void)dev;
  if (offset < CORE_RTG_REG_SIZE) {
    return core_rtg_read_reg(offset & ~0x3u);
  }

  uint32_t mem_off = offset - CORE_RTG_REG_SIZE;
  if (mem_off + sizeof(uint32_t) > core_rtg_state.vram_size || !core_rtg_state.vram) {
    return 0;
  }
  return read_be32(&core_rtg_state.vram[mem_off]);
}

static uint16_t core_rtg_read16(zorro_device_t *dev, uint32_t offset) {
  uint32_t base = offset & ~0x3u;
  uint32_t val = core_rtg_read32(dev, base);
  if ((offset & 0x2u) != 0) {
    return (uint16_t)(val & 0xFFFFu);
  }
  return (uint16_t)((val >> 16) & 0xFFFFu);
}

static uint8_t core_rtg_read8(zorro_device_t *dev, uint32_t offset) {
  uint32_t base = offset & ~0x3u;
  uint32_t val = core_rtg_read32(dev, base);
  uint32_t shift = (3u - (offset & 0x3u)) * 8u;
  return (uint8_t)((val >> shift) & 0xFFu);
}

static void core_rtg_write32(zorro_device_t *dev, uint32_t offset, uint32_t value) {
  (void)dev;
  if (offset < CORE_RTG_REG_SIZE) {
    core_rtg_write_reg(offset & ~0x3u, value);
    return;
  }

  uint32_t mem_off = offset - CORE_RTG_REG_SIZE;
  if (mem_off + sizeof(uint32_t) > core_rtg_state.vram_size || !core_rtg_state.vram) {
    return;
  }
  write_be32(&core_rtg_state.vram[mem_off], value);
  core_rtg_vram_writes++;
  core_rtg_state.vram_last_off = mem_off;
  if (core_rtg_state.vram_min_off == 0xFFFFFFFFu || mem_off < core_rtg_state.vram_min_off) {
    core_rtg_state.vram_min_off = mem_off;
  }
  if (mem_off > core_rtg_state.vram_max_off) {
    core_rtg_state.vram_max_off = mem_off;
  }
  if (core_rtg_state.fb_pitch && core_rtg_state.fb_height) {
    size_t fb_start = (size_t)core_rtg_state.fb_addr_offset;
    size_t fb_end = fb_start + (size_t)core_rtg_state.fb_pitch * core_rtg_state.fb_height;
    if ((size_t)mem_off >= fb_start && (size_t)mem_off < fb_end) {
      core_rtg_state.fb_write_hits++;
      core_rtg_state.fb_last_off = mem_off;
      if (core_rtg_state.fb_min_off == 0xFFFFFFFFu || mem_off < core_rtg_state.fb_min_off) {
        core_rtg_state.fb_min_off = mem_off;
      }
      if (mem_off > core_rtg_state.fb_max_off) {
        core_rtg_state.fb_max_off = mem_off;
      }
    }
  }
  if (core_rtg_vram_log < 8) {
    LOG_INFO("[CORE-RTG] VRAM32 @0x%08X = 0x%08X\n", mem_off, value);
    core_rtg_vram_log++;
  }
}

static void core_rtg_write16(zorro_device_t *dev, uint32_t offset, uint16_t value) {
  if (offset < CORE_RTG_REG_SIZE) {
    uint32_t base = offset & ~0x3u;
    uint32_t cur = core_rtg_read_reg(base);
    uint32_t val32;
    if ((offset & 0x2u) == 0) {
      val32 = (cur & 0x0000FFFFu) | ((uint32_t)value << 16);
    } else {
      val32 = (cur & 0xFFFF0000u) | (uint32_t)value;
    }
    core_rtg_write_reg(base, val32);
    return;
  }

  uint32_t mem_off = offset - CORE_RTG_REG_SIZE;
  if (mem_off + sizeof(uint16_t) > core_rtg_state.vram_size || !core_rtg_state.vram) {
    return;
  }
  write_be16(&core_rtg_state.vram[mem_off], value);
  core_rtg_vram_writes++;
  core_rtg_state.vram_last_off = mem_off;
  if (core_rtg_state.vram_min_off == 0xFFFFFFFFu || mem_off < core_rtg_state.vram_min_off) {
    core_rtg_state.vram_min_off = mem_off;
  }
  if (mem_off > core_rtg_state.vram_max_off) {
    core_rtg_state.vram_max_off = mem_off;
  }
  if (core_rtg_state.fb_pitch && core_rtg_state.fb_height) {
    size_t fb_start = (size_t)core_rtg_state.fb_addr_offset;
    size_t fb_end = fb_start + (size_t)core_rtg_state.fb_pitch * core_rtg_state.fb_height;
    if ((size_t)mem_off >= fb_start && (size_t)mem_off < fb_end) {
      core_rtg_state.fb_write_hits++;
      core_rtg_state.fb_last_off = mem_off;
      if (core_rtg_state.fb_min_off == 0xFFFFFFFFu || mem_off < core_rtg_state.fb_min_off) {
        core_rtg_state.fb_min_off = mem_off;
      }
      if (mem_off > core_rtg_state.fb_max_off) {
        core_rtg_state.fb_max_off = mem_off;
      }
    }
  }
  if (core_rtg_vram_log < 8) {
    LOG_INFO("[CORE-RTG] VRAM16 @0x%08X = 0x%04X\n", mem_off, value);
    core_rtg_vram_log++;
  }
}

static void core_rtg_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  if (offset < CORE_RTG_REG_SIZE) {
    uint32_t base = offset & ~0x3u;
    uint32_t cur = core_rtg_read_reg(base);
    uint32_t shift = (3u - (offset & 0x3u)) * 8u;
    uint32_t mask = 0xFFu << shift;
    uint32_t val32 = (cur & ~mask) | ((uint32_t)value << shift);
    core_rtg_write_reg(base, val32);
    return;
  }

  uint32_t mem_off = offset - CORE_RTG_REG_SIZE;
  if (mem_off >= core_rtg_state.vram_size || !core_rtg_state.vram) {
    return;
  }
  core_rtg_state.vram[mem_off] = value;
  core_rtg_vram_writes++;
  core_rtg_state.vram_last_off = mem_off;
  if (core_rtg_state.vram_min_off == 0xFFFFFFFFu || mem_off < core_rtg_state.vram_min_off) {
    core_rtg_state.vram_min_off = mem_off;
  }
  if (mem_off > core_rtg_state.vram_max_off) {
    core_rtg_state.vram_max_off = mem_off;
  }
  if (core_rtg_state.fb_pitch && core_rtg_state.fb_height) {
    size_t fb_start = (size_t)core_rtg_state.fb_addr_offset;
    size_t fb_end = fb_start + (size_t)core_rtg_state.fb_pitch * core_rtg_state.fb_height;
    if ((size_t)mem_off >= fb_start && (size_t)mem_off < fb_end) {
      core_rtg_state.fb_write_hits++;
      core_rtg_state.fb_last_off = mem_off;
      if (core_rtg_state.fb_min_off == 0xFFFFFFFFu || mem_off < core_rtg_state.fb_min_off) {
        core_rtg_state.fb_min_off = mem_off;
      }
      if (mem_off > core_rtg_state.fb_max_off) {
        core_rtg_state.fb_max_off = mem_off;
      }
    }
  }
  if (core_rtg_vram_log < 8) {
    LOG_INFO("[CORE-RTG] VRAM8 @0x%08X = 0x%02X\n", mem_off, value);
    core_rtg_vram_log++;
  }
}

static void core_rtg_reset(zorro_device_t *dev) {
  (void)dev;
  memset(&core_rtg_state, 0, sizeof(core_rtg_state));
  core_rtg_state.scale_x = 0x00010000u;
  core_rtg_state.scale_y = 0x00010000u;
  core_rtg_state.fb_min_off = 0xFFFFFFFFu;
  core_rtg_state.vram_min_off = 0xFFFFFFFFu;
}

static uint8_t core_rtg_rom[] = {
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

zorro_device_t core_rtg_device = {
    .name = "core-rtg",
    .bus = ZORRO_BUS_Z3,
    .size = CORE_RTG_Z3_SIZE,
    .manufacturer = PISTORM_MANUF_ID,
    .product = PISTORM_PROD_CORE_RTG_Z3,
    .flags = 0,
    .ac_rom = core_rtg_rom,
    .ac_rom_size = sizeof(core_rtg_rom),
    .reset = core_rtg_reset,
    .read8 = core_rtg_read8,
    .read16 = core_rtg_read16,
    .read32 = core_rtg_read32,
    .write8 = core_rtg_write8,
    .write16 = core_rtg_write16,
    .write32 = core_rtg_write32,
    .priv = &core_rtg_state,
};

void core_rtg_register(zorro_bus_t bus) {
  if (!core_rtg_state.vram) {
    core_rtg_state.vram_size =
        (bus == ZORRO_BUS_Z2) ? CORE_RTG_Z2_MEM_SIZE : CORE_RTG_Z3_MEM_SIZE;
    core_rtg_state.vram = (uint8_t *)calloc(1, core_rtg_state.vram_size);
    if (!core_rtg_state.vram) {
      LOG_WARN("[ZORRO] core-rtg VRAM allocation failed (%u bytes).\n",
               core_rtg_state.vram_size);
      core_rtg_state.vram_size = 0;
    }
  }
  if (bus == ZORRO_BUS_Z2) {
    core_rtg_device.bus = ZORRO_BUS_Z2;
    core_rtg_device.size = CORE_RTG_Z2_SIZE;
    core_rtg_device.product = PISTORM_PROD_CORE_RTG_Z2;
  } else {
    core_rtg_device.bus = ZORRO_BUS_Z3;
    core_rtg_device.size = CORE_RTG_Z3_SIZE;
    core_rtg_device.product = PISTORM_PROD_CORE_RTG_Z3;
  }

  LOG_INFO("[ZORRO] Registering core RTG device (%s).\n",
           core_rtg_device.bus == ZORRO_BUS_Z2 ? "Z2" : "Z3");
  int slot = zorro_register_device(&core_rtg_device);
  if (slot < 0) {
    LOG_WARN("[ZORRO] Failed to register core RTG device.\n");
  }
}
