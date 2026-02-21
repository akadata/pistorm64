// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <inttypes.h>
#include <endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stddef.h>
#include "config_file/config_file.h"
#include "log.h"
#ifndef FAKESTORM
#include "gpio/ps_protocol.h"
#endif
#include "platforms/amiga/rtg/irtg_structs.h"
#include "rtg.h"

#include "m68k.h"

/*
static zorro_device_t z3_rtg_device = {
    .name         = "pistorm-rtg",
    .bus          = ZORRO_BUS_Z3,
    .size         = PIGFX_RTG_SIZE,          // whatever the spec wants
    .manufacturer = PISTORM_MANUF_ID,
    .product      = PISTORM_PROD_PI_RTG,     // add to the enum
    .flags        = 0,
    .ac_rom       = z3_rtg_rom,              // autoconf ROM bytes
    .ac_rom_size  = sizeof(z3_rtg_rom),
    .reset        = z3_rtg_reset,
    .read8        = z3_rtg_read8,
    .write8       = z3_rtg_write8,
    .priv         = &rtg_state,
};
*/
static inline uint16_t read_be16(const uint8_t* ptr) {
  uint16_t tmp;
  memcpy(&tmp, ptr, sizeof(tmp));
  return be16toh(tmp);
}

static inline uint32_t read_be32(const uint8_t* ptr) {
  uint32_t tmp;
  memcpy(&tmp, ptr, sizeof(tmp));
  return be32toh(tmp);
}

static inline void write_be16(uint8_t* ptr, uint16_t value) {
  uint16_t tmp = htobe16(value);
  memcpy(ptr, &tmp, sizeof(tmp));
}

static inline void write_be32(uint8_t* ptr, uint32_t value) {
  uint32_t tmp = htobe32(value);
  memcpy(ptr, &tmp, sizeof(tmp));
}

void rtg_p2c_ex(int16_t src_x, int16_t src_y, int16_t dst_x, int16_t dst_y, int16_t width, int16_t height,
                uint8_t minterm, struct BitMap* bm, uint8_t mask, uint16_t dst_pitch,
                uint16_t src_pitch);

uint8_t rtg_u8[4];
uint16_t rtg_x[8];
uint16_t rtg_y[8];
uint16_t rtg_user[8];
uint16_t rtg_format;
uint32_t rtg_address[8];
uint32_t rtg_address_adj[8];
uint32_t rtg_rgb[8];

uint8_t display_enabled = 0xFF;

uint16_t rtg_display_width;
uint16_t rtg_display_height;
uint16_t rtg_display_format;
uint16_t rtg_pitch;
uint16_t rtg_total_rows;
uint16_t rtg_offset_x;
uint16_t rtg_offset_y;

uint8_t* rtg_mem; // FIXME
static unsigned char rtg_mem_alloc_kind = MAPALLOC_NONE;

uint32_t framebuffer_addr = 0;
uint32_t framebuffer_addr_adj = 0;

static void handle_rtg_command(uint32_t cmd);

/*static void handle_irtg_command(uint32_t cmd);*/

uint8_t realtime_graphics_debug = 0;
extern int cpu_emulation_running;
extern struct emulator_config* cfg;
extern uint8_t rtg_on;
extern uint8_t rtg_enabled;
extern uint8_t rtg_output_in_vblank;

#define DEBUG_RTG

#ifdef DEBUG_RTG
/*static const char *op_type_names[OP_TYPE_NUM] = {
    "BYTE",
    "WORD",
    "LONGWORD",
    "MEM",
};*/

#define DEBUG LOG_DEBUG
#else
#define DEBUG(...)
#endif

static const char* rtg_format_names[RTGFMT_NUM] = {
    "4BPP PLANAR",        "8BPP CLUT",          "16BPP RGB (565 BE)", "16BPP RGB (565 LE)",
    "16BPP BGR (565 LE)", "24BPP RGB",          "24BPP BGR",          "32BPP RGB (ARGB)",
    "32BPP RGB (ABGR)",   "32BPP RGB (RGBA)",   "32BPP RGB (BGRA)",   "15BPP RGB (555 BE)",
    "15BPP RGB (555 LE)", "15BPP BGR (555 LE)", "YUV422 (CGX)",
    "YUV411 (AccuPak)",   "YUV411 (AccuPak PC)", "YUV422",
    "YUV422 (PC)",        "YUV422 (PA)",        "YUV422 (PAPC)",
    "NONE/UNKNOWN",
};

#ifndef RTG_GFX_MEM
#define RTG_GFX_MEM 128u
#endif
#ifndef RTG_MEM_MB
#define RTG_MEM_MB RTG_GFX_MEM
#endif

static const unsigned int rtg_mem_size = (unsigned int)RTG_GFX_MEM * SIZE_MEGA;

int init_rtg_data(struct emulator_config* cfg_) {
  rtg_mem = cfg_alloc_mapped_data(rtg_mem_size, 1, &rtg_mem_alloc_kind, "rtg_mem");
  if (!rtg_mem) {
    LOG_ERROR("Failed to allocate RTG video memory.\n");
    return 0;
  }

  /* Prefer reusing config-provided rtg_mem mapping to avoid duplicate ranges. */
  int map_index = get_named_mapped_item(cfg_, "rtg_mem");
  if (map_index >= 0) {
    unsigned int base = (unsigned int)cfg_->map_offset[map_index];
    unsigned int size = cfg_->map_size[map_index] ? cfg_->map_size[map_index] : rtg_mem_size;
    cfg_release_map_data(cfg_, map_index);
    cfg_->map_type[map_index] = MAPTYPE_RAM_NOALLOC;
    cfg_->map_size[map_index] = size;
    cfg_->map_high[map_index] = base + size;
    cfg_set_map_data_allocation(cfg_, map_index, rtg_mem, 0, MAPALLOC_EXTERNAL);
    m68k_add_ram_range(base, base + size, rtg_mem);
    LOG_INFO("[RTG] Bound existing rtg_mem map[%d] to allocated RTG buffer (%u MB).\n",
             map_index, size / SIZE_MEGA);
  } else {
    m68k_add_ram_range(PIGFX_RTG_BASE + PIGFX_REG_SIZE, PIGFX_UPPER, rtg_mem);
    add_mapping(cfg_, MAPTYPE_RAM_NOALLOC, PIGFX_RTG_BASE + PIGFX_REG_SIZE, rtg_mem_size,
                (unsigned int)-1, (char*)rtg_mem, "rtg_mem", 0);
  }
  return 1;
}

void shutdown_rtg(void) {
  LOG_INFO("[RTG] Shutting down RTG.\n");
  if (rtg_on) {
    display_enabled = 0xFF;
    rtg_on = 0;
  }
  if (rtg_mem) {
    cfg_free_mapped_data(rtg_mem, rtg_mem_size, rtg_mem_alloc_kind);
    rtg_mem = NULL;
    rtg_mem_alloc_kind = MAPALLOC_NONE;
  }
}

unsigned int rtg_get_fb(void) {
  return PIGFX_RTG_BASE + PIGFX_REG_SIZE + framebuffer_addr_adj;
}

uint8_t wait_vblank = 0;
uint32_t wait_rtg_frame = 0;
extern uint32_t cur_rtg_frame;

unsigned int rtg_read(uint32_t address, uint8_t mode) {
   //printf("%s read from RTG: %.8X\n", op_type_names[mode], address);

  if (address >= PIGFX_REG_SIZE) {
    const unsigned int offset = address - PIGFX_REG_SIZE;
    if (rtg_mem && offset < rtg_mem_size) {
      switch (mode) {
      case OP_TYPE_BYTE:
        return rtg_mem[offset];
        break;
      case OP_TYPE_WORD:
        return read_be16(&rtg_mem[offset]);
        break;
      case OP_TYPE_LONGWORD:
        return read_be32(&rtg_mem[offset]);
        break;
      default:
        return 0;
      }
    }
  }

  switch (address) {
  case RTG_COMMAND:
    return rtg_enabled ? 0xFFCF : 0x0000;
  case RTG_WAITVSYNC:
    if (rtg_on) {
      return 1;   // DEBUG: force vblank always ready
      if (!wait_vblank && cur_rtg_frame != wait_rtg_frame) {
        wait_rtg_frame = cur_rtg_frame;
        if (wait_rtg_frame == 0) {
          wait_rtg_frame = cur_rtg_frame;
        }
        if (wait_rtg_frame == 0)
          LOG_DEBUG("Wait RTG frame was zero!\n");
        wait_vblank = 1;
      }
      if (cur_rtg_frame != wait_rtg_frame && wait_vblank) {
        wait_vblank = 0;
        return 1;
      } else
        return 0;
    }
    // fallthrough
  case RTG_INVBLANK:
    return !rtg_on || rtg_output_in_vblank;
  default:
    break;
  }

  return 0;
}

// Forward declaration
static struct timespec diff(struct timespec start, struct timespec end) __attribute__((unused));

static struct timespec diff(struct timespec start, struct timespec end) {
  struct timespec temp;
  if ((end.tv_nsec - start.tv_nsec) < 0) {
    temp.tv_sec = end.tv_sec - start.tv_sec - 1;
    temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
  } else {
    temp.tv_sec = end.tv_sec - start.tv_sec;
    temp.tv_nsec = end.tv_nsec - start.tv_nsec;
  }
  return temp;
}

static inline void rtg_write_reg_u8(uint32_t address, uint8_t value) {
  // RTG_U81..RTG_U84 are byte argument registers for command payload.
  uint8_t* const reg_u1 = &rtg_u8[0];
  uint8_t* const reg_u2 = &rtg_u8[1];
  uint8_t* const reg_u3 = &rtg_u8[2];
  uint8_t* const reg_u4 = &rtg_u8[3];

  switch (address) {
  case RTG_U81:
    *reg_u1 = value;
    break;
  case RTG_U82:
    *reg_u2 = value;
    break;
  case RTG_U83:
    *reg_u3 = value;
    break;
  case RTG_U84:
    *reg_u4 = value;
    break;
  default:
    break;
  }
}

static inline void rtg_write_reg_u16(uint32_t address, uint16_t value) {
  // Word registers (RTG_Xn/Yn/Un/FORMAT/COMMAND) define command payload.
  uint16_t* const reg_x1 = &rtg_x[0];
  uint16_t* const reg_x2 = &rtg_x[1];
  uint16_t* const reg_x3 = &rtg_x[2];
  uint16_t* const reg_x4 = &rtg_x[3];
  uint16_t* const reg_x5 = &rtg_x[4];

  uint16_t* const reg_y1 = &rtg_y[0];
  uint16_t* const reg_y2 = &rtg_y[1];
  uint16_t* const reg_y3 = &rtg_y[2];
  uint16_t* const reg_y4 = &rtg_y[3];
  uint16_t* const reg_y5 = &rtg_y[4];

  uint16_t* const reg_user1 = &rtg_user[0];
  uint16_t* const reg_user2 = &rtg_user[1];

  switch (address) {
  case RTG_X1:
    *reg_x1 = value;
    break;
  case RTG_X2:
    *reg_x2 = value;
    break;
  case RTG_X3:
    *reg_x3 = value;
    break;
  case RTG_X4:
    *reg_x4 = value;
    break;
  case RTG_X5:
    *reg_x5 = value;
    break;
  case RTG_Y1:
    *reg_y1 = value;
    break;
  case RTG_Y2:
    *reg_y2 = value;
    break;
  case RTG_Y3:
    *reg_y3 = value;
    break;
  case RTG_Y4:
    *reg_y4 = value;
    break;
  case RTG_Y5:
    *reg_y5 = value;
    break;
  case RTG_U1:
    *reg_user1 = value;
    break;
  case RTG_U2:
    *reg_user2 = value;
    break;
  case RTG_FORMAT:
    rtg_format = value;
    break;
  case RTG_COMMAND:
    handle_rtg_command(value);
    break;
  default:
    break;
  }
}

static inline void rtg_write_reg_u32(uint32_t address, uint32_t value) {
  // Long registers carry addresses/colors and command trigger.
  uint32_t* const reg_addr1 = &rtg_address[0];
  uint32_t* const reg_addr2 = &rtg_address[1];
  uint32_t* const reg_addr3 = &rtg_address[2];
  uint32_t* const reg_addr4 = &rtg_address[3];
  uint32_t* const reg_rgb1 = &rtg_rgb[0];
  uint32_t* const reg_rgb2 = &rtg_rgb[1];

  switch (address) {
  case RTG_ADDR1:
    *reg_addr1 = value;
    rtg_address_adj[0] = value - (PIGFX_RTG_BASE + PIGFX_REG_SIZE);
    break;
  case RTG_ADDR2:
    *reg_addr2 = value;
    rtg_address_adj[1] = value - (PIGFX_RTG_BASE + PIGFX_REG_SIZE);
    break;
  case RTG_ADDR3:
    *reg_addr3 = value;
    break;
  case RTG_ADDR4:
    *reg_addr4 = value;
    break;
  case RTG_RGB1:
    *reg_rgb1 = value;
    break;
  case RTG_RGB2:
    *reg_rgb2 = value;
    break;
  case RTG_COMMAND:
    handle_rtg_command(value);
    break;
  default:
    break;
  }
}

void rtg_write(uint32_t address, uint32_t value, uint8_t mode) {
  // printf("%s write to RTG: %.8X (%.8X)\n", op_type_names[mode], address, value);
  if (address >= PIGFX_REG_SIZE) {
    const unsigned int offset = address - PIGFX_REG_SIZE;
    if (rtg_mem && offset < rtg_mem_size) {
      switch (mode) {
      case OP_TYPE_BYTE:
        rtg_mem[offset] = (uint8_t)value;
        break;
      case OP_TYPE_WORD:
        write_be16(&rtg_mem[offset], (uint16_t)value);
        break;
      case OP_TYPE_LONGWORD:
        write_be32(&rtg_mem[offset], (uint32_t)value);
        break;
      default:
        return;
      }
    }
    return;
  }

  switch (mode) {
  case OP_TYPE_BYTE:
    rtg_write_reg_u8(address, (uint8_t)value);
    break;
  case OP_TYPE_WORD:
    rtg_write_reg_u16(address, (uint16_t)value);
    break;
  case OP_TYPE_LONGWORD:
    rtg_write_reg_u32(address, value);
    break;
  default:
    break;
  }

  return;
}

#define gdebug(a)  

static void handle_rtg_command(uint32_t cmd) {
  // Snapshot register payload once; each command interprets these slots differently.
  const uint16_t reg_x1 = rtg_x[0];
  const uint16_t reg_x2 = rtg_x[1];
  const uint16_t reg_x3 = rtg_x[2];
  const uint16_t reg_x4 = rtg_x[3];
  const uint16_t reg_x5 = rtg_x[4];

  const uint16_t reg_y1 = rtg_y[0];
  const uint16_t reg_y2 = rtg_y[1];
  const uint16_t reg_y3 = rtg_y[2];

  const uint8_t reg_u1 = rtg_u8[0];
  const uint8_t reg_u2 = rtg_u8[1];
  const uint8_t reg_u3 = rtg_u8[2];
  const uint8_t reg_u4 = rtg_u8[3];

  const uint16_t reg_user1 = rtg_user[0];

  const uint32_t reg_addr1 = rtg_address[0];
  const uint32_t reg_addr2 = rtg_address[1];
  const uint32_t reg_rgb1 = rtg_rgb[0];
  const uint32_t reg_rgb2 = rtg_rgb[1];

  switch (cmd) {
  case RTGCMD_SETGC:
    gdebug("SetGC\n");
    if (rtg_display_format != rtg_format) {
      LOG_INFO("Pixel format switch from: %s (%d) to %s (%d)\n",
               rtg_format_names[rtg_display_format], rtg_display_format,
               rtg_format_names[rtg_format], rtg_format);
    }
    rtg_display_format = rtg_format;
    rtg_display_width = reg_x1;
    rtg_display_height = reg_y1;
    if (reg_u1) {
      // rtg_pitch = rtg_display_width << rtg_format;
      framebuffer_addr_adj =
          framebuffer_addr + (rtg_offset_x * rtg_pixel_size[rtg_display_format]) + (rtg_offset_y * rtg_pitch);
      rtg_total_rows = reg_y2;
    } else {
      // rtg_pitch = rtg_display_width << rtg_format;
      framebuffer_addr_adj =
          framebuffer_addr + (rtg_offset_x * rtg_pixel_size[rtg_display_format]) + (rtg_offset_y * rtg_pitch);
      rtg_total_rows = reg_y2;
    }
    if (realtime_graphics_debug) {
      LOG_DEBUG("Set RTG mode:\n");
      LOG_DEBUG("%dx%d pixels\n", rtg_display_width, rtg_display_height);
    }
    break;
  case RTGCMD_SETPAN:
    // printf("Command: SetPan.\n");
    rtg_offset_x = reg_x2;
    rtg_offset_y = reg_y2;
    rtg_pitch = (uint16_t)(reg_x1 * rtg_pixel_size[rtg_display_format]);
    framebuffer_addr = reg_addr1 - (PIGFX_RTG_BASE + PIGFX_REG_SIZE);
    framebuffer_addr_adj =
        framebuffer_addr + (rtg_offset_x * rtg_pixel_size[rtg_display_format]) + (rtg_offset_y * rtg_pitch);

    // printf("PAN:\nPitch: %d\n", rtg_pitch);
    // printf("Pixel format: %s (%d)\n", rtg_format_names[rtg_format], rtg_format);
    // printf("Display pixel format: %s (%d)\n", rtg_format_names[rtg_display_format], rtg_display_format);
    break;
  case RTGCMD_SETCLUT:
   //  printf("Command: SetCLUT.\n");
   //  printf("Set palette entry %d to %d, %d, %d\n", rtg_u8[0], rtg_u8[1], rtg_u8[2], rtg_u8[3]);
  //   printf("Set palette entry %d to 32-bit palette color: %.8X\n", rtg_u8[0], rtg_rgb[0]);
    rtg_set_clut_entry(reg_u1, reg_rgb1);
    break;
  case RTGCMD_SETDISPLAY:
    gdebug("SetDisplay\n");
    if (realtime_graphics_debug) {
      LOG_DEBUG("RTG SetDisplay %s\n", (reg_u2) ? "enabled" : "disabled");
    }
    break;
  case RTGCMD_ENABLE:
  case RTGCMD_SETSWITCH:
    gdebug("SetSwitch\n");
    if (realtime_graphics_debug) {
      LOG_DEBUG("RTG SetSwitch %s\n", ((reg_x1) & 0x01) ? "enabled" : "disabled");
      LOG_DEBUG("LAL: %.4X\n", reg_x1);
    }
    display_enabled = ((reg_x1) & 0x01);
    if (display_enabled != rtg_on) {
      rtg_on = display_enabled;
      if (rtg_on) {
        rtg_init_display();
      } else {
        rtg_shutdown_display();
      }
    }
    break;
  case RTGCMD_FILLRECT:
    if (reg_u1 == 0xFF || rtg_format != RTGFMT_8BIT_CLUT) {
      rtg_fillrect_solid( // done messes with workbench width, height 
        reg_x1,
        reg_y1,
        reg_x2,
        reg_y2,
        reg_rgb1,
        reg_x3,
        rtg_format
        );
      //LOG_DEBUG("[RTG/RTGCMD_FILLRECT/FillRect Solid] rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[2]=%u ,rtg_u8[0]=%u\n", rtg_x[0], rtg_y[0], rtg_x[1], rtg_y[1], rtg_x[2], rtg_u8[0]);
      gdebug("FillRect Solid\n");
    } else {
      rtg_fillrect(
        reg_x1,
        reg_y1,
        reg_x2,
        reg_y2,
        reg_rgb1,
        reg_x3,
        rtg_format,
        reg_u1
        );

      //LOG_DEBUG("[RTG/RTGCMD_FILLRECT/FillRect Masked] rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[2]=%u ,rtg_u8[0]=%u\n", rtg_x[0], rtg_y[0], rtg_x[1], rtg_y[1], rtg_x[2], rtg_u8[0]);
      gdebug("FillRect Masked\n");
    }
    break;
  case RTGCMD_INVERTRECT:
    rtg_invertrect(
      reg_x1,
      reg_y1,
      reg_x2,
      reg_y2,
      reg_x3,
      rtg_format,
      reg_u1
      );
    //LOG_DEBUG("[RTG/RTGCMD_INVERTRECT] rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[2]=%u ,rtg_u8[0]=%u\n", rtg_x[0], rtg_y[0], rtg_x[1], rtg_y[1], rtg_x[2], rtg_u8[0]);
    gdebug("InvertRect\n");
    break;
  case RTGCMD_BLITRECT:
    if (reg_u1 == 0xFF || rtg_format != RTGFMT_8BIT_CLUT) {
      rtg_blitrect_solid(
        reg_x1,
        reg_y1,
        reg_x2,
        reg_y2,
        reg_x3,
        reg_y3,
        reg_x4,
        rtg_format
        );
      //LOG_DEBUG("[RTG/RTGCMD_BLITRECT/Solid] rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[2]=%u ,rtg_y[2]=%u ,rtg_x[3]=%u ,rtg_u8[0]=%u\n", rtg_x[0], rtg_y[0], rtg_x[1], rtg_y[1], rtg_x[2], rtg_y[2], rtg_x[3], rtg_u8[0]);
      gdebug("BlitRect Solid\n");
    } else {
      rtg_blitrect(
        reg_x1,
        reg_y1,
        reg_x2,
        reg_y2,
        reg_x3,
        reg_y3,
        reg_x4,
        rtg_format,
        reg_u1
        );
      //LOG_DEBUG("[RTG/RTGCMD_BLITRECT/Masked] rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[2]=%u ,rtg_y[2]=%u ,rtg_x[3]=%u ,rtg_u8[0]=%u\n", rtg_x[0], rtg_y[0], rtg_x[1], rtg_y[1], rtg_x[2], rtg_y[2], rtg_x[3], rtg_u8[0]);
      gdebug("BlitRect Masked\n");
    }
    break;
  case RTGCMD_BLITRECT_NOMASK_COMPLETE:
    rtg_blitrect_nomask_complete(
      reg_x1,
      reg_y1,
      reg_x2,
      reg_y2,
      reg_x3,
      reg_y3,
      reg_x4,
      reg_x5,
      reg_addr1,
      reg_addr2,
      rtg_format,
      (int8_t)reg_u1
      );
    //LOG_DEBUG("[RTG/RTGCMD_BLITRECT_NOMASK_COMPLETE] rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[2]=%u ,rtg_y[2]=%u ,rtg_x[3]=%u ,rtg_x[4]=%u ,rtg_u8[0]=%u\n", rtg_x[0], rtg_y[0], rtg_x[1], rtg_y[1], rtg_x[2], rtg_y[2], rtg_x[3], rtg_x[4], rtg_u8[0]);
    gdebug("BlitRectNoMaskComplete\n");
    break;
  case RTGCMD_BLITPATTERN:
    rtg_blitpattern(reg_x1,
      reg_y1,
      reg_x2,
      reg_y2,
      reg_addr1,
      reg_rgb1,
      reg_rgb2,
      reg_x4,
      rtg_format,
      reg_x3,
      reg_y3,
      reg_u1,
      reg_u2,
      reg_u3
      );
    /*LOG_DEBUG(
    "[RTG/RTGCMD_BLITPATTERN] "
    "x0=%u y0=%u x1=%u y1=%u addr=0x%08X fg=0x%08X bg=0x%08X "
    "pitch=%u fmt=%u x2=%u y2=%u u0=%u u1=%u u2=%u\n",
    (uint16_t)rtg_x[0],
    (uint16_t)rtg_y[0],
    (uint16_t)rtg_x[1],
    (uint16_t)rtg_y[1],
    (uint32_t)rtg_address[0],
    (uint32_t)rtg_rgb[0],
    (uint32_t)rtg_rgb[1],
    (uint16_t)rtg_x[3],
    (unsigned)rtg_format,
    (uint16_t)rtg_x[2],
    (uint16_t)rtg_y[2],
    (unsigned)rtg_u8[0],
    (unsigned)rtg_u8[1],
    (unsigned)rtg_u8[2]
    ); */
    

    gdebug("BlitPattern\n");
    return;
  case RTGCMD_BLITTEMPLATE:
    rtg_blittemplate(  /// text etc
      reg_x1,
      reg_y1,
      reg_x2,
      reg_y2,
      reg_addr1,
      reg_rgb1,
      reg_rgb2,
      reg_x4,
      reg_x5,
      rtg_format, 
      reg_x3,
      reg_u1,
      reg_u2
      );
   // LOG_DEBUG("[RTG/RTGCMD_BLITTEMPLATE]  rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[3]=%u ,rtg_x[4]=%u \n", rtg_x[0],rtg_y[0],rtg_x[1],rtg_y[1],rtg_x[3],rtg_x[4]);
    gdebug("BlitTemplate\n");
    break;
  case RTGCMD_DRAWLINE:
    if (reg_u1 == 0xFF && reg_y3 == 0xFFFF) {
      rtg_drawline_solid(  // tried! fail here
        (int16_t)reg_x1,
        (int16_t)reg_y1,
        (int16_t)reg_x2,
        (int16_t)reg_y2,
        reg_x3,
        reg_rgb1,
        reg_x4,
        rtg_format
        );
     // LOG_DEBUG("[RTG/RTGCMD_DRAWLINE/Solid] rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[2]=%u ,rtg_x[3]=%u ,rtg_u8[0]=%u ,rtg_y[2]=%u\n", rtg_x[0], rtg_y[0], rtg_x[1], rtg_y[1], rtg_x[2], rtg_x[3], rtg_u8[0], rtg_y[2]);
    }  else {
      rtg_drawline(
        (int16_t)reg_x1,
        (int16_t)reg_y1,
        (int16_t)reg_x2,
        (int16_t)reg_y2,
        reg_x3,
        reg_y3,
        reg_x5,
        reg_rgb1,
        reg_rgb2,
        reg_x4,
        rtg_format,
        reg_u1,
        reg_u2);
      //LOG_DEBUG("[RTG/RTGCMD_DRAWLINE/Masked] rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[2]=%u ,rtg_y[2]=%u ,rtg_x[3]=%u ,rtg_x[4]=%u ,rtg_u8[0]=%u ,rtg_u8[1]=%u\n", rtg_x[0], rtg_y[0], rtg_x[1], rtg_y[1], rtg_x[2], rtg_y[2], rtg_x[3], rtg_x[4], rtg_u8[0], rtg_u8[1]);
    }

    gdebug("DrawLine\n");
    break;
  case RTGCMD_P2C:
    rtg_p2c((int16_t)reg_x1,
      (int16_t)reg_y1,
      (int16_t)(reg_x2 / 2), // fake native mode horizontal correction.
      (int16_t)reg_y2,
      (int16_t)reg_x3,
      (int16_t)reg_y3,
      reg_u2,
      reg_u3,
      reg_u1,
      (uint8_t)(reg_user1 >> 0x8),
      reg_x5,
      (uint8_t*)&rtg_mem[rtg_address_adj[1]]
      );
    // LOG_DEBUG("[RTG/RTGCMD_P2C] rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[2]=%u ,rtg_y[2]=%u ,rtg_x[4]=%u ,rtg_u8[0]=%u ,rtg_u8[1]=%u ,rtg_u8[2]=%u\n", rtg_x[0], rtg_y[0], rtg_x[1], rtg_y[1], rtg_x[2], rtg_y[2], rtg_x[4], rtg_u8[0], rtg_u8[1], rtg_u8[2]);
    gdebug("Planar2Chunky\n");
    break;
  case RTGCMD_P2D:
    rtg_p2d(
      (int16_t)reg_x1,
      (int16_t)reg_y1,
      (int16_t)(reg_x2 / 2), // PiGFX mode horizontal correction.
      (int16_t)reg_y2,
      (int16_t)reg_x3,
      (int16_t)reg_y3,
      reg_u2,
      reg_u3,
      reg_u1,
      (uint8_t)(reg_user1 >> 0x8),
      reg_x5,
      (uint8_t*)&rtg_mem[rtg_address_adj[1]]
      );
    //LOG_DEBUG("[RTG/RTGCMD_P2D] rtg_x[0]=%u ,rtg_y[0]=%u ,rtg_x[1]=%u ,rtg_y[1]=%u ,rtg_x[2]=%u ,rtg_y[2]=%u ,rtg_x[4]=%u ,rtg_u8[0]=%u ,rtg_u8[1]=%u ,rtg_u8[2]=%u\n", rtg_x[0], rtg_y[0], rtg_x[1], rtg_y[1], rtg_x[2], rtg_y[2], rtg_x[4], rtg_u8[0], rtg_u8[1], rtg_u8[2]);
    gdebug("Planar2Direct\n");
    break;
  case RTGCMD_SETSPRITE:
    rtg_enable_mouse_cursor(
      (uint8_t)reg_user1
      );
   // LOG_DEBUG("[RTG/RTGCMD_SETSPRITE] rtg_user[0]=%u\n", rtg_user[0]);
    gdebug("SetSprite\n");
    break;
  case RTGCMD_SETSPRITECOLOR:
    rtg_set_cursor_clut_entry(
      reg_u1,
      reg_u2,
      reg_u3,
      reg_u4
      );
   // LOG_DEBUG("[RTG/RTGCMD_SETSPRITECOLOR] rtg_u8[0]=%u ,rtg_u8[1]=%u ,rtg_u8[2]=%u ,rtg_u8[3]=%u\n", rtg_u8[0], rtg_u8[1], rtg_u8[2], rtg_u8[3]);
    gdebug("SetSpriteColor\n");
    break;
  case RTGCMD_SETSPRITEPOS:
    rtg_set_mouse_cursor_pos(
      (int16_t)reg_x1,
      (int16_t)reg_y1
      );
    gdebug("SetSpritePos\n");
    break;
  case RTGCMD_SETSPRITEIMAGE:
    rtg_set_mouse_cursor_image(
      &rtg_mem[rtg_address_adj[1]],
      reg_u1,
      reg_u2
      );
    //LOG_DEBUG("[RTG/RTGCMD_SETSPRITEIMAGE] rtg_address_adj[1]=%u ,rtg_u8[0]=%u ,rtg_u8[1]=%u\n", rtg_address_adj[1], rtg_u8[0], rtg_u8[1]);
    gdebug("SetSpriteImage\n");
    break;
  case RTGCMD_DEBUGME:
    LOG_DEBUG("[RTG] DebugMe!\n");
    break;
  default:
    LOG_DEBUG("[!!!RTG] Unknown/unhandled RTG command %d ($%.4X)\n", cmd, cmd);
    break;
  }
}
