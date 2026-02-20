// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

#ifndef CORE_RTG_BASE
#define CORE_RTG_BASE 0x70000000u
#endif

#ifndef CORE_RTG_REG_SIZE
#define CORE_RTG_REG_SIZE 0x00001000u
#endif

#define CORE_RTG_REG_MAGIC   0x00u
#define CORE_RTG_REG_VERSION 0x04u
#define CORE_RTG_REG_STATUS  0x08u
#define CORE_RTG_REG_CMD     0x0Cu

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
#define CORE_RTG_REG_CLUT_INDEX 0x40u
#define CORE_RTG_REG_CLUT_RGB   0x44u

#define CORE_RTG_MAGIC 0x43525447u
#define CORE_RTG_VERSION 0x00010000u
