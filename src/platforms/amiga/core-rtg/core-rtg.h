// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>
#include "platforms/amiga/amiga_zorro.h"

#define CORE_RTG_REG_SIZE   0x1000u

#ifndef CORE_RTG_Z3_MEM_MB
#define CORE_RTG_Z3_MEM_MB 128u
#endif
#ifndef CORE_RTG_Z2_MEM_MB
#define CORE_RTG_Z2_MEM_MB 8u
#endif

#define CORE_RTG_Z3_MEM_SIZE (CORE_RTG_Z3_MEM_MB * 0x00100000u)
#define CORE_RTG_Z2_MEM_SIZE (CORE_RTG_Z2_MEM_MB * 0x00100000u)
#define CORE_RTG_REG_MAGIC  0x00u
#define CORE_RTG_REG_VERSION 0x04u
#define CORE_RTG_REG_STATUS 0x08u
#define CORE_RTG_REG_CMD    0x0Cu

#define CORE_RTG_REG_FB_ADDR   0x10u
#define CORE_RTG_REG_FB_PITCH  0x14u
#define CORE_RTG_REG_FB_WIDTH  0x18u
#define CORE_RTG_REG_FB_HEIGHT 0x1Cu
#define CORE_RTG_REG_FB_FORMAT 0x20u
#define CORE_RTG_REG_PAN_X     0x24u
#define CORE_RTG_REG_PAN_Y     0x28u

#define CORE_RTG_REG_DISP_W    0x30u
#define CORE_RTG_REG_DISP_H    0x34u
#define CORE_RTG_REG_SCALE_X   0x38u
#define CORE_RTG_REG_SCALE_Y   0x3Cu

#define CORE_RTG_MAGIC         0x43525447u  // 'CRTG'
#define CORE_RTG_VERSION       0x00010000u

typedef struct core_rtg_state {
  uint8_t *vram;
  uint32_t vram_size;
  uint32_t fb_addr;
  uint32_t fb_pitch;
  uint16_t fb_width;
  uint16_t fb_height;
  uint16_t fb_format;
  uint16_t pan_x;
  uint16_t pan_y;

  uint16_t disp_width;
  uint16_t disp_height;
  uint32_t scale_x;
  uint32_t scale_y;

  uint8_t enabled;
} core_rtg_state_t;

void core_rtg_register(zorro_bus_t bus);
