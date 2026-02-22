// SPDX-License-Identifier: MIT
// PiStorm PiRTG64 driver, VBCC edition.
// Based in part on the ZZ9000 RTG driver.
// PiRTG64 Picasso96 RTG card – build script
//
// Copyright (c) 2026 AKADATA Limited
// Licensed under the MIT License – see LICENSE for details.
// Developed by AKADATA, with help and support from Codex.

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
#include "platforms/amiga/pirtg64/irtg_structs.h"
#include "pirtg64.h"

#include "m68k.h"

/*
static zorro_device_t z3_rtg_device = {
    .name         = "pirtg64-rtg",
    .bus          = ZORRO_BUS_Z3,
    .size         = PIRTG64_RTG_SIZE,          // whatever the spec wants
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
                uint8_t minterm, struct BitMap* bm, uint8_t mask, uint16_t dst_pitch,uint16_t src_pitch);

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

static int rtg_memwrite_debug = -1;
static uint32_t rtg_memwrite32_samples = 0;
static uint32_t rtg_memwrite32_gray = 0;
static uint32_t rtg_memwrite32_alpha0 = 0;
static uint32_t rtg_memwrite16_samples = 0;
static uint32_t rtg_memwrite16_gray = 0;
static uint64_t rtg_memwrite_total_bytes = 0;
static uint64_t rtg_memwrite_last_report_ns = 0;
static int rtg_cmd_debug = -1;
static uint32_t rtg_cmd_counts[RTGCMD_DEBUGME + 1];
static uint64_t rtg_cmd_last_report_ns = 0;
static uint64_t rtg_cmd_p2c_pixels = 0;
static uint64_t rtg_cmd_p2d_pixels = 0;
static uint32_t rtg_cmd_p2c_max_w = 0;
static uint32_t rtg_cmd_p2c_max_h = 0;
static uint32_t rtg_cmd_p2d_max_w = 0;
static uint32_t rtg_cmd_p2d_max_h = 0;

static inline uint64_t rtg_now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static inline int rtg_memwrite_debug_enabled(void) {
  if (rtg_memwrite_debug < 0) {
    const char* env = getenv("PISTORM_RTG_MEMWRITE_DEBUG");
    rtg_memwrite_debug = (env && *env && atoi(env) != 0) ? 1 : 0;
    if (rtg_memwrite_debug) {
      LOG_INFO("[RTG/DBG] RTG memory write debug enabled via PISTORM_RTG_MEMWRITE_DEBUG=1.\n");
    }
  }
  return rtg_memwrite_debug;
}

static inline int rtg_cmd_debug_enabled(void) {
  if (rtg_cmd_debug < 0) {
    const char* env = getenv("PISTORM_RTG_CMD_DEBUG");
    rtg_cmd_debug = (env && *env && atoi(env) != 0) ? 1 : 0;
    if (rtg_cmd_debug) {
      rtg_cmd_last_report_ns = rtg_now_ns();
      LOG_INFO("[RTG/DBG] RTG command debug enabled via PISTORM_RTG_CMD_DEBUG=1.\n");
    }
  }
  return rtg_cmd_debug;
}

static void rtg_cmd_debug_note(uint32_t cmd) {
  if (!rtg_cmd_debug_enabled()) {
    return;
  }
  if (cmd <= RTGCMD_DEBUGME) {
    rtg_cmd_counts[cmd]++;
  }
  uint64_t now = rtg_now_ns();
  if (rtg_cmd_last_report_ns == 0) {
    rtg_cmd_last_report_ns = now;
    return;
  }
  if (now - rtg_cmd_last_report_ns < 1000000000ull) {
    return;
  }
  LOG_INFO("[RTG/DBG][CMD/sec] setgc=%u setpan=%u setclut=%u fill=%u blit=%u blitnm=%u tmpl=%u patt=%u p2c=%u p2d=%u draw=%u p2c_px=%" PRIu64 " p2d_px=%" PRIu64 " p2c_max=%ux%u p2d_max=%ux%u\n",
           rtg_cmd_counts[RTGCMD_SETGC], rtg_cmd_counts[RTGCMD_SETPAN],
           rtg_cmd_counts[RTGCMD_SETCLUT], rtg_cmd_counts[RTGCMD_FILLRECT],
           rtg_cmd_counts[RTGCMD_BLITRECT], rtg_cmd_counts[RTGCMD_BLITRECT_NOMASK_COMPLETE],
           rtg_cmd_counts[RTGCMD_BLITTEMPLATE], rtg_cmd_counts[RTGCMD_BLITPATTERN],
           rtg_cmd_counts[RTGCMD_P2C], rtg_cmd_counts[RTGCMD_P2D],
           rtg_cmd_counts[RTGCMD_DRAWLINE], rtg_cmd_p2c_pixels, rtg_cmd_p2d_pixels,
           rtg_cmd_p2c_max_w, rtg_cmd_p2c_max_h, rtg_cmd_p2d_max_w, rtg_cmd_p2d_max_h);
  memset(rtg_cmd_counts, 0, sizeof(rtg_cmd_counts));
  rtg_cmd_p2c_pixels = 0;
  rtg_cmd_p2d_pixels = 0;
  rtg_cmd_p2c_max_w = 0;
  rtg_cmd_p2c_max_h = 0;
  rtg_cmd_p2d_max_w = 0;
  rtg_cmd_p2d_max_h = 0;
  rtg_cmd_last_report_ns = now;
}

static inline void rtg_cmd_debug_note_p2_dims(int is_p2d, int16_t width, int16_t height) {
  if (!rtg_cmd_debug_enabled()) {
    return;
  }
  if (width <= 0 || height <= 0) {
    return;
  }
  uint32_t w = (uint32_t)width;
  uint32_t h = (uint32_t)height;
  uint64_t pixels = (uint64_t)w * (uint64_t)h;
  if (is_p2d) {
    rtg_cmd_p2d_pixels += pixels;
    if (w > rtg_cmd_p2d_max_w) {
      rtg_cmd_p2d_max_w = w;
    }
    if (h > rtg_cmd_p2d_max_h) {
      rtg_cmd_p2d_max_h = h;
    }
  } else {
    rtg_cmd_p2c_pixels += pixels;
    if (w > rtg_cmd_p2c_max_w) {
      rtg_cmd_p2c_max_w = w;
    }
    if (h > rtg_cmd_p2c_max_h) {
      rtg_cmd_p2c_max_h = h;
    }
  }
}

static void rtg_memwrite_debug_note(size_t write_bytes) {
  if (!rtg_memwrite_debug_enabled()) {
    return;
  }
  rtg_memwrite_total_bytes += write_bytes;
  uint64_t now = rtg_now_ns();
  if (rtg_memwrite_last_report_ns == 0) {
    rtg_memwrite_last_report_ns = now;
    return;
  }
  if (now - rtg_memwrite_last_report_ns < 1000000000ull) {
    return;
  }
  LOG_INFO("[RTG/DBG][MEM/sec] bytes=%" PRIu64 " fmt=%u mem16(samples=%u gray=%u) mem32(samples=%u gray=%u alpha0=%u)\n",
           rtg_memwrite_total_bytes, rtg_display_format, rtg_memwrite16_samples, rtg_memwrite16_gray,
           rtg_memwrite32_samples, rtg_memwrite32_gray, rtg_memwrite32_alpha0);
  rtg_memwrite_total_bytes = 0;
  rtg_memwrite16_samples = 0;
  rtg_memwrite16_gray = 0;
  rtg_memwrite32_samples = 0;
  rtg_memwrite32_gray = 0;
  rtg_memwrite32_alpha0 = 0;
  rtg_memwrite_last_report_ns = now;
}

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
    m68k_add_ram_range(PIRTG64_BASE + PIRTG64_REG_SIZE, PIRTG64_UPPER, rtg_mem);
    add_mapping(cfg_, MAPTYPE_RAM_NOALLOC, PIRTG64_BASE + PIRTG64_REG_SIZE, rtg_mem_size,
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
  return PIRTG64_BASE + PIRTG64_REG_SIZE + framebuffer_addr_adj;
}

uint8_t wait_vblank = 0;
uint32_t wait_rtg_frame = 0;
extern uint32_t cur_rtg_frame;

unsigned int rtg_read(uint32_t address, uint8_t mode) {
   //printf("%s read from RTG: %.8X\n", op_type_names[mode], address);

  if (address >= PIRTG64_REG_SIZE) {
    const unsigned int offset = address - PIRTG64_REG_SIZE;
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
    rtg_address_adj[0] = value - (PIRTG64_BASE + PIRTG64_REG_SIZE);
    break;
  case RTG_ADDR2:
    *reg_addr2 = value;
    rtg_address_adj[1] = value - (PIRTG64_BASE + PIRTG64_REG_SIZE);
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
  if (address >= PIRTG64_REG_SIZE) {
    const unsigned int offset = address - PIRTG64_REG_SIZE;
    if (rtg_mem && offset < rtg_mem_size) {
      switch (mode) {
      case OP_TYPE_BYTE:
        rtg_mem[offset] = (uint8_t)value;
        rtg_memwrite_debug_note(1);
        break;
      case OP_TYPE_WORD:
        write_be16(&rtg_mem[offset], (uint16_t)value);
        rtg_memwrite_debug_note(2);
        if (rtg_memwrite_debug_enabled() && rtg_display_format == RTGFMT_RGB565_LE) {
          uint16_t px = (uint16_t)value;
          uint8_t r5 = (uint8_t)((px >> 11) & 0x1F);
          uint8_t g6 = (uint8_t)((px >> 5) & 0x3F);
          uint8_t b5 = (uint8_t)(px & 0x1F);
          uint8_t g5 = (uint8_t)((g6 + 1u) >> 1);
          rtg_memwrite16_samples++;
          if (r5 == g5 && g5 == b5) {
            rtg_memwrite16_gray++;
          }
        }
        break;
      case OP_TYPE_LONGWORD:
        write_be32(&rtg_mem[offset], (uint32_t)value);
        rtg_memwrite_debug_note(4);
        if (rtg_memwrite_debug_enabled() && rtg_display_format == RTGFMT_RGB32_BGRA) {
          uint8_t b1 = (uint8_t)((value >> 16) & 0xFF);
          uint8_t b2 = (uint8_t)((value >> 8) & 0xFF);
          uint8_t b3 = (uint8_t)(value & 0xFF);
          rtg_memwrite32_samples++;
          if (b1 == b2 && b2 == b3) {
            rtg_memwrite32_gray++;
          }
          if (b3 == 0) {
            rtg_memwrite32_alpha0++;
          }
        }
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
    rtg_write_reg_u32(address, (uint32_t)value);
    break;
  default:
    break;
  }

  return;
}

#define gdebug(a)  

static void handle_rtg_command(uint32_t cmd) {
  rtg_cmd_debug_note(cmd);
  // Raw RTG argument registers (transport-level payload).
  const uint16_t x1_word = rtg_x[0];
  const uint16_t x2_word = rtg_x[1];
  const uint16_t x3_word = rtg_x[2];
  const uint16_t x4_word = rtg_x[3];
  const uint16_t x5_word = rtg_x[4];
  const uint16_t y1_word = rtg_y[0];
  const uint16_t y2_word = rtg_y[1];
  const uint16_t y3_word = rtg_y[2];
  const uint8_t u1_byte = rtg_u8[0];
  const uint8_t u2_byte = rtg_u8[1];
  const uint8_t u3_byte = rtg_u8[2];
  const uint8_t u4_byte = rtg_u8[3];
  const uint16_t user1_word = rtg_user[0];
  const uint32_t addr1_long = rtg_address[0];
  const uint32_t addr2_long = rtg_address[1];
  const uint32_t rgb1_long = rtg_rgb[0];
  const uint32_t rgb2_long = rtg_rgb[1];

  // Common command aliases.
  const uint16_t display_width = x1_word;      // SETGC
  const uint16_t display_height = y1_word;     // SETGC
  const uint16_t total_rows = y2_word;         // SETGC
  const uint8_t gc_flags = u1_byte;            // SETGC

  const uint16_t pan_width = x1_word;          // SETPAN
  const uint16_t pan_offset_x = x2_word;       // SETPAN
  const uint16_t pan_offset_y = y2_word;       // SETPAN
  const uint32_t pan_framebuffer = addr1_long; // SETPAN

  const uint8_t clut_index = u1_byte;          // SETCLUT
  const uint32_t clut_color = rgb1_long;       // SETCLUT

  const uint16_t switch_flags = x1_word;       // ENABLE/SETSWITCH

  const uint16_t fill_dst_x = x1_word;         // FILLRECT/INVERTRECT
  const uint16_t fill_dst_y = y1_word;         // FILLRECT/INVERTRECT
  const uint16_t fill_width = x2_word;         // FILLRECT/INVERTRECT
  const uint16_t fill_height = y2_word;        // FILLRECT/INVERTRECT
  const uint16_t fill_pitch = x3_word;         // FILLRECT/INVERTRECT
  const uint32_t fill_color = rgb1_long;       // FILLRECT
  const uint8_t fill_mask = u1_byte;           // FILLRECT/INVERTRECT

  const uint16_t blit_src_x = x1_word;         // BLITRECT/NOMASK
  const uint16_t blit_src_y = y1_word;         // BLITRECT/NOMASK
  const uint16_t blit_dst_x = x2_word;         // BLITRECT/NOMASK
  const uint16_t blit_dst_y = y2_word;         // BLITRECT/NOMASK
  const uint16_t blit_width = x3_word;         // BLITRECT/NOMASK
  const uint16_t blit_height = y3_word;        // BLITRECT/NOMASK
  const uint16_t blit_pitch = x4_word;         // BLITRECT/NOMASK
  const uint16_t blit_src_pitch = x4_word;     // NOMASK
  const uint16_t blit_dst_pitch = x5_word;     // NOMASK
  const uint32_t blit_src_addr = addr1_long;   // NOMASK
  const uint32_t blit_dst_addr = addr2_long;   // NOMASK
  const int8_t blit_minterm = (int8_t)u1_byte; // NOMASK
  const uint8_t blit_mask = u1_byte;           // BLITRECT

  const uint16_t pattern_dst_x = x1_word;      // BLITPATTERN
  const uint16_t pattern_dst_y = y1_word;      // BLITPATTERN
  const uint16_t pattern_width = x2_word;      // BLITPATTERN
  const uint16_t pattern_height = y2_word;     // BLITPATTERN
  const uint32_t pattern_src_addr = addr1_long;// BLITPATTERN
  const uint32_t pattern_fg_color = rgb1_long; // BLITPATTERN
  const uint32_t pattern_bg_color = rgb2_long; // BLITPATTERN
  const uint16_t pattern_dst_pitch = x4_word;  // BLITPATTERN
  const uint16_t pattern_offset_x = x3_word;   // BLITPATTERN
  const uint16_t pattern_offset_y = y3_word;   // BLITPATTERN
  const uint8_t pattern_mask = u1_byte;        // BLITPATTERN
  const uint8_t pattern_draw_mode = u2_byte;   // BLITPATTERN
  const uint8_t pattern_loop_rows = u3_byte;   // BLITPATTERN

  const uint16_t template_dst_x = x1_word;     // BLITTEMPLATE
  const uint16_t template_dst_y = y1_word;     // BLITTEMPLATE
  const uint16_t template_width = x2_word;     // BLITTEMPLATE
  const uint16_t template_height = y2_word;    // BLITTEMPLATE
  const uint32_t template_src_addr = addr1_long;// BLITTEMPLATE
  const uint32_t template_fg_color = rgb1_long; // BLITTEMPLATE
  const uint32_t template_bg_color = rgb2_long; // BLITTEMPLATE
  const uint16_t template_dst_pitch = x4_word;  // BLITTEMPLATE
  const uint16_t template_src_pitch = x5_word;  // BLITTEMPLATE
  const uint16_t template_offset_x = x3_word;   // BLITTEMPLATE
  const uint8_t template_mask = u1_byte;        // BLITTEMPLATE
  const uint8_t template_draw_mode = u2_byte;   // BLITTEMPLATE

  const int16_t line_start_x = (int16_t)x1_word;// DRAWLINE
  const int16_t line_start_y = (int16_t)y1_word;// DRAWLINE
  const int16_t line_end_x = (int16_t)x2_word;  // DRAWLINE
  const int16_t line_end_y = (int16_t)y2_word;  // DRAWLINE
  const uint16_t line_length = x3_word;         // DRAWLINE
  const uint16_t line_pattern = y3_word;        // DRAWLINE
  const uint16_t line_pattern_offset = x5_word; // DRAWLINE
  const uint32_t line_fg_color = rgb1_long;     // DRAWLINE
  const uint32_t line_bg_color = rgb2_long;     // DRAWLINE
  const uint16_t line_pitch = x4_word;          // DRAWLINE
  const uint8_t line_mask = u1_byte;            // DRAWLINE
  const uint8_t line_draw_mode = u2_byte;       // DRAWLINE

  const int16_t p2_src_x = (int16_t)x1_word;    // P2C/P2D
  const int16_t p2_src_y = (int16_t)y1_word;    // P2C/P2D
  const int16_t p2_dst_x = (int16_t)(x2_word / 2); // P2C/P2D fake-native correction
  const int16_t p2_dst_y = (int16_t)y2_word;    // P2C/P2D
  const int16_t p2_width = (int16_t)x3_word;    // P2C/P2D
  const int16_t p2_height = (int16_t)y3_word;   // P2C/P2D
  const uint8_t p2_draw_mode = u2_byte;         // P2C/P2D (minterm)
  const uint8_t p2_planes = u3_byte;            // P2C/P2D (depth/planes)
  const uint8_t p2_mask = u1_byte;              // P2C/P2D
  const uint8_t p2_layer_mask = (uint8_t)(user1_word >> 8); // P2C/P2D
  const uint16_t p2_src_pitch = x5_word;        // P2C/P2D
  const uint8_t* p2_bitmap = (uint8_t*)&rtg_mem[rtg_address_adj[1]]; // P2C/P2D

  const uint8_t sprite_enable = (uint8_t)user1_word; // SETSPRITE
  const uint8_t sprite_r = u1_byte;                  // SETSPRITECOLOR
  const uint8_t sprite_g = u2_byte;                  // SETSPRITECOLOR
  const uint8_t sprite_b = u3_byte;                  // SETSPRITECOLOR
  const uint8_t sprite_idx = u4_byte;                // SETSPRITECOLOR
  const int16_t sprite_x = (int16_t)x1_word;         // SETSPRITEPOS
  const int16_t sprite_y = (int16_t)y1_word;         // SETSPRITEPOS
  const uint8_t* sprite_image = &rtg_mem[rtg_address_adj[1]]; // SETSPRITEIMAGE
  const uint8_t sprite_w = u1_byte;                  // SETSPRITEIMAGE
  const uint8_t sprite_h = u2_byte;                  // SETSPRITEIMAGE

  switch (cmd) {
  case RTGCMD_SETGC:
    gdebug("SetGC\n");
    if (rtg_display_format != rtg_format) {
      LOG_INFO("Pixel format switch from: %s (%d) to %s (%d)\n",
               rtg_format_names[rtg_display_format], rtg_display_format,
               rtg_format_names[rtg_format], rtg_format);
    }
    rtg_display_format = rtg_format;
    rtg_display_width = display_width;
    rtg_display_height = display_height;
    if (gc_flags) {
      // rtg_pitch = rtg_display_width << rtg_format;
      framebuffer_addr_adj = framebuffer_addr + (rtg_offset_x * rtg_pixel_size[rtg_display_format]) + (rtg_offset_y * rtg_pitch);
      rtg_total_rows = total_rows;
    } else {
      // rtg_pitch = rtg_display_width << rtg_format;
      framebuffer_addr_adj = framebuffer_addr + (rtg_offset_x * rtg_pixel_size[rtg_display_format]) + (rtg_offset_y * rtg_pitch);
      rtg_total_rows = total_rows;
    }
    if (realtime_graphics_debug) {
      LOG_DEBUG("Set RTG mode:\n");
      LOG_DEBUG("%dx%d pixels\n", rtg_display_width, rtg_display_height);
    }
    break;
  case RTGCMD_SETPAN:
    // printf("Command: SetPan.\n");
    rtg_offset_x = pan_offset_x;
    rtg_offset_y = pan_offset_y;
    rtg_pitch = (uint16_t)(pan_width * rtg_pixel_size[rtg_display_format]);
    framebuffer_addr = pan_framebuffer - (PIRTG64_BASE + PIRTG64_REG_SIZE);
    framebuffer_addr_adj = framebuffer_addr + (rtg_offset_x * rtg_pixel_size[rtg_display_format]) + (rtg_offset_y * rtg_pitch);

    // printf("PAN:\nPitch: %d\n", rtg_pitch);
    // printf("Pixel format: %s (%d)\n", rtg_format_names[rtg_format], rtg_format);
    // printf("Display pixel format: %s (%d)\n", rtg_format_names[rtg_display_format], rtg_display_format);
    break;
  case RTGCMD_SETCLUT:
    // IMPORTANT: reg_rgb1 is the correct palette payload for this driver path.
    // A previous attempt to source RGB from reg_u2/reg_u3/reg_u4 caused severe CLUT corruption
    // (black/green screen, 2-color icons). Treat any change here as high-risk and re-test
    // Workbench + 8-bit video playback before merging.
    rtg_set_clut_entry(clut_index, clut_color);
    break;
  case RTGCMD_SETDISPLAY:
    gdebug("SetDisplay\n");
    if (realtime_graphics_debug) {
      LOG_DEBUG("RTG SetDisplay %s\n", (u2_byte) ? "enabled" : "disabled");
    }
    break;
  case RTGCMD_ENABLE:
  case RTGCMD_SETSWITCH:
    gdebug("SetSwitch\n");
    if (realtime_graphics_debug) {
      LOG_DEBUG("RTG SetSwitch %s\n", ((switch_flags) & 0x01) ? "enabled" : "disabled");
      LOG_DEBUG("LAL: %.4X\n", switch_flags);
    }
    display_enabled = ((switch_flags) & 0x01);
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
    if (fill_mask == 0xFF || rtg_format != RTGFMT_8BIT_CLUT) { // done messes with workbench width, height 
      rtg_fillrect_solid(fill_dst_x, fill_dst_y, fill_width, fill_height, fill_color, fill_pitch, rtg_format);
      gdebug("FillRect Solid\n");
    } else {
      rtg_fillrect(fill_dst_x, fill_dst_y, fill_width, fill_height, fill_color, fill_pitch,
                   rtg_format, fill_mask);
      gdebug("FillRect Masked\n");
    }
    break;
  case RTGCMD_INVERTRECT:
    rtg_invertrect(fill_dst_x, fill_dst_y, fill_width, fill_height, fill_pitch, rtg_format,
                   fill_mask);
    gdebug("InvertRect\n");
    break;
  case RTGCMD_BLITRECT:
    if (blit_mask == 0xFF || rtg_format != RTGFMT_8BIT_CLUT) {
      rtg_blitrect_solid(blit_src_x, blit_src_y, blit_dst_x, blit_dst_y, blit_width, blit_height,
                         blit_pitch, rtg_format);
      gdebug("BlitRect Solid\n");
    } else {
      rtg_blitrect(blit_src_x, blit_src_y, blit_dst_x, blit_dst_y, blit_width, blit_height,
                   blit_pitch, rtg_format, blit_mask);
      gdebug("BlitRect Masked\n");
    }
    break;
  case RTGCMD_BLITRECT_NOMASK_COMPLETE:
    rtg_blitrect_nomask_complete(blit_src_x, blit_src_y, blit_dst_x, blit_dst_y, blit_width,
                                 blit_height, blit_src_pitch, blit_dst_pitch, blit_src_addr,
                                 blit_dst_addr, rtg_format, blit_minterm);
    gdebug("BlitRectNoMaskComplete\n");
    break;
  case RTGCMD_BLITPATTERN:
    rtg_blitpattern(pattern_dst_x, pattern_dst_y, pattern_width, pattern_height, pattern_src_addr,
                    pattern_fg_color, pattern_bg_color, pattern_dst_pitch, rtg_format,
                    pattern_offset_x, pattern_offset_y, pattern_mask, pattern_draw_mode,
                    pattern_loop_rows);
    gdebug("BlitPattern\n");
    return;
  case RTGCMD_BLITTEMPLATE: /// text etc
    rtg_blittemplate(template_dst_x, template_dst_y, template_width, template_height,
                     template_src_addr, template_fg_color, template_bg_color, template_dst_pitch,
                     template_src_pitch, rtg_format, template_offset_x, template_mask,
                     template_draw_mode);
    gdebug("BlitTemplate\n");
    break;
  case RTGCMD_DRAWLINE:
    if (line_mask == 0xFF && line_pattern == 0xFFFF) {// tried! fail here
      rtg_drawline_solid(line_start_x, line_start_y, line_end_x, line_end_y, line_length,
                         line_fg_color, line_pitch, rtg_format);
    }  else {
      rtg_drawline(line_start_x, line_start_y, line_end_x, line_end_y, line_length, line_pattern,
                   line_pattern_offset, line_fg_color, line_bg_color, line_pitch, rtg_format,
                   line_mask, line_draw_mode);
    }
    gdebug("DrawLine\n");
    break;
  case RTGCMD_P2C: // fake native mode horizontal correction.
      rtg_cmd_debug_note_p2_dims(0, p2_width, p2_height);
      rtg_p2c(p2_src_x, p2_src_y, p2_dst_x, p2_dst_y, p2_width, p2_height, p2_draw_mode,
              p2_planes, p2_mask, p2_layer_mask, p2_src_pitch, (uint8_t*)p2_bitmap);
    gdebug("Planar2Chunky\n");
    break;
  case RTGCMD_P2D:// PiGFX mode horizontal correction.
    rtg_cmd_debug_note_p2_dims(1, p2_width, p2_height);
    rtg_p2d(p2_src_x, p2_src_y, p2_dst_x, p2_dst_y, p2_width, p2_height, p2_draw_mode,
            p2_planes, p2_mask, p2_layer_mask, p2_src_pitch, (uint8_t*)p2_bitmap);
    gdebug("Planar2Direct\n");
    break;
  case RTGCMD_SETSPRITE:
    rtg_enable_mouse_cursor(sprite_enable);
    gdebug("SetSprite\n");
    break;
  case RTGCMD_SETSPRITECOLOR:
    rtg_set_cursor_clut_entry(sprite_r, sprite_g, sprite_b, sprite_idx);
    gdebug("SetSpriteColor\n");
    break;
  case RTGCMD_SETSPRITEPOS:
    rtg_set_mouse_cursor_pos(sprite_x, sprite_y);
    gdebug("SetSpritePos\n");
    break;
  case RTGCMD_SETSPRITEIMAGE:
    rtg_set_mouse_cursor_image((uint8_t*)sprite_image, sprite_w, sprite_h);
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
