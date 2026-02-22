// SPDX-License-Identifier: MIT
// PiStorm PiRTG64 driver,
// Based in part on the ZZ9000 RTG driver.
// PiRTG64 Picasso96 RTG card – build script
//
// Copyright (c) 2026 AKADATA Limited
// Licensed under the MIT License – see LICENSE for details.
// Developed by AKADATA, with help and support from Codex.

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "m68k.h"
#include "config_file/config_file.h"
#ifndef FAKESTORM
#include "gpio/ps_protocol.h"
#endif
#include <endian.h>
#include "platforms/amiga/pirtg64/irtg_structs.h"
#include "pirtg64.h"
#include "log.h"


// Helper functions for safe unaligned memory access
static inline uint16_t load_u16_be(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, sizeof v);
    return be16toh(v);
}


static inline uint32_t load_u32_be(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return be32toh(v);
}


static inline uint8_t* rtg_line_pixel_ptr(uint8_t* base, int32_t x, uint16_t format) {
  ptrdiff_t offset = (ptrdiff_t)x * (ptrdiff_t)rtg_pixel_size[format];
  return base + offset;
}

static inline void store_u16_be(uint8_t *p, uint16_t v) {
    v = htobe16(v);
    memcpy(p, &v, sizeof v);
}

static inline void store_u32_be(uint8_t *p, uint32_t v) {
    v = htobe32(v);
    memcpy(p, &v, sizeof v);
}


static inline size_t rtg_index_offset(int index, size_t element_size) {
    return (size_t)index * element_size;
}

extern uint32_t rtg_address[8];
extern uint32_t rtg_address_adj[8];

extern uint8_t *rtg_mem;

extern uint16_t rtg_user[8];

extern uint16_t rtg_x[8];
extern uint16_t rtg_y[8];

extern uint16_t rtg_format;
extern uint16_t rtg_display_format;

extern uint32_t framebuffer_addr;
extern uint32_t framebuffer_addr_adj;

extern uint8_t realtime_graphics_debug;


#ifndef RTG_GFX_MEM
#define RTG_GFX_MEM 128u
#endif
#ifndef RTG_MEM_MB
#define RTG_MEM_MB RTG_GFX_MEM
#endif

static const size_t rtg_mem_size = (size_t)RTG_GFX_MEM * SIZE_MEGA;


static uint32_t rtg_oob_log_count = 0;

static int rtg_calc_span(size_t x_bytes, uint16_t width, uint16_t height, 
  uint16_t line_pitch, size_t bpp, size_t* out_span) {
  if (width == 0 || height == 0 || bpp == 0 || line_pitch == 0) {
    return 0;
  }
  if (x_bytes > SIZE_MAX - ((size_t)width * bpp)) {
    return 0;
  }
  size_t row_span = x_bytes + ((size_t)width * bpp);
  if (row_span > line_pitch) {
    return 0;
  }
  *out_span = ((size_t)line_pitch * (height - 1)) + row_span;
  return 1;
}

static int rtg_check_bounds(size_t base, size_t span, const char* tag, 
  uint16_t line_pitch, uint16_t width, uint16_t height, uint16_t pixel_format) {

  if (base >= rtg_mem_size || span > rtg_mem_size - base) {
    if (rtg_oob_log_count < 20) {
      LOG_DEBUG("[RTG/OOB] %s base=0x%zx span=%zu pitch=%u w=%u h=%u fmt=%u\n", tag, base, span,
               line_pitch, width, height, pixel_format);
      rtg_oob_log_count++;
    }
    return 0;
  }
  return 1;
}

static int rtg_get_ptr_checked(uint32_t base_adj, uint16_t dst_x, uint16_t dst_y, uint16_t width, uint16_t height,
                               uint16_t dst_pitch, uint16_t pixel_format, const char* tag,
                               uint8_t** out_ptr) {
  if (pixel_format >= RTG_FMT_NUM) {
    if (rtg_oob_log_count < 20) {
      LOG_DEBUG("[RTG/OOB] %s invalid format: %u\n", tag, pixel_format);
      rtg_oob_log_count++;
    }
    return 0;
  }
  size_t bpp = rtg_pixel_size[pixel_format];
  size_t x_bytes = (size_t)dst_x * bpp;
  size_t span = 0;
  if (!rtg_calc_span(x_bytes, width, height, dst_pitch, bpp, &span)) {
    if (rtg_oob_log_count < 20) {
      LOG_DEBUG("[RTG/OOB] %s invalid span: x=%u y=%u w=%u h=%u pitch=%u fmt=%u\n", tag, dst_x, dst_y, width, height,
               dst_pitch, pixel_format);
      rtg_oob_log_count++;
    }
    return 0;
  }
  size_t base = (size_t)base_adj + x_bytes + ((size_t)dst_y * dst_pitch);
  if (!rtg_check_bounds(base, span, tag, dst_pitch, width, height, pixel_format)) {
    return 0;
  }
  *out_ptr = &rtg_mem[base];
  return 1;
}

void rtg_fillrect_solid(uint16_t dst_x, uint16_t dst_y, uint16_t width, uint16_t height, 
  uint32_t color, uint16_t dst_pitch, uint16_t pixel_format) {
  uint8_t* dptr = NULL;

  if (!rtg_get_ptr_checked(rtg_address_adj[0], dst_x, dst_y, width, height, 
    dst_pitch, pixel_format, "fillrect_solid", &dptr)) {
    return;
  }

  switch (pixel_format) {
  case RTG_FMT_8BIT_CLUT: {
    for (int xs = 0; xs < width; xs++) {
      dptr[xs] = color & 0xFF;
    }
    break;
  }
  case RTG_FMT_RGB565_LE:
  case RTG_FMT_RGB565_BE:
  case RTG_FMT_BGR565_LE:
  case RTG_FMT_RGB555_LE:
  case RTG_FMT_RGB555_BE:
  case RTG_FMT_BGR555_LE: {
    uint16_t color16 = (color & 0xFFFF);
    for (int xs = 0; xs < width; xs++) {
      size_t offset = (size_t)xs * sizeof(uint16_t);
      store_u16_be(&dptr[offset], color16);
    }
    break;
  }
  case RTG_FMT_RGB32_ABGR:
  case RTG_FMT_RGB32_ARGB:
  case RTG_FMT_RGB32_BGRA:
  case RTG_FMT_RGB32_RGBA: {
    for (int xs = 0; xs < width; xs++) {
      size_t offset = (size_t)xs * sizeof(uint32_t);
      store_u32_be(&dptr[offset], color);
    }
    break;
  }
  case RTG_FMT_RGB24:
  case RTG_FMT_BGR24: {
    for (int xs = 0; xs < width; xs++) {
      size_t offset = (size_t)xs * rtg_pixel_size[pixel_format];
      rtg_store_pixel(&dptr[offset], pixel_format, color);
    }
    break;
  }
  }
  for (int ys = 1; ys < height; ys++) {
    dptr += dst_pitch;
    size_t copy_bytes = (size_t)width * rtg_pixel_size[pixel_format];
    memcpy(dptr, dptr - dst_pitch, copy_bytes);
  }
}

void rtg_fillrect(uint16_t dst_x, uint16_t dst_y, uint16_t width, uint16_t height, uint32_t color, uint16_t dst_pitch,
                  uint16_t pixel_format, uint8_t mask) {
  uint8_t* dptr = NULL;

  if (!rtg_get_ptr_checked(rtg_address_adj[0], dst_x, dst_y, width, height, dst_pitch, pixel_format, "fillrect", &dptr)) {
    return;
  }

  for (int ys = 0; ys < height; ys++) {
    for (int xs = 0; xs < width; xs++) {
      size_t offset = (size_t)xs * rtg_pixel_size[pixel_format];
      SET_RTG_PIXEL_MASK(&dptr[offset], color, pixel_format);
    }
    dptr += dst_pitch;
  }
}

void rtg_invertrect(uint16_t dst_x, uint16_t dst_y, uint16_t width, uint16_t height, uint16_t dst_pitch, uint16_t pixel_format,
                    uint8_t color_mask) {
  if (color_mask) {
  }
  uint8_t* dptr = NULL;
  if (!rtg_get_ptr_checked(rtg_address_adj[0], dst_x, dst_y, width, height, dst_pitch, pixel_format, "invertrect", &dptr)) {
    return;
  }
  for (int ys = 0; ys < height; ys++) {
    switch (pixel_format) {
    case RTG_FMT_8BIT_CLUT: {
      for (int xs = 0; xs < width; xs++) {
        dptr[xs] ^= color_mask;
      }
      break;
    }
      case RTG_FMT_RGB565_LE:
      case RTG_FMT_RGB565_BE:
      case RTG_FMT_BGR565_LE:
      case RTG_FMT_RGB555_LE:
      case RTG_FMT_RGB555_BE:
      case RTG_FMT_BGR555_LE: {
        for (int xs = 0; xs < width; xs++) {
          size_t offset = (size_t)xs * sizeof(uint16_t);
          uint16_t val = load_u16_be(&dptr[offset]);
          val = ~val;
          store_u16_be(&dptr[offset], val);
        }
        break;
      }
      case RTG_FMT_RGB32_ABGR:
      case RTG_FMT_RGB32_ARGB:
    case RTG_FMT_RGB32_BGRA:
    case RTG_FMT_RGB32_RGBA: {
        for (int xs = 0; xs < width; xs++) {
          size_t offset = (size_t)xs * sizeof(uint32_t);
          uint32_t val = load_u32_be(&dptr[offset]);
          val = ~val;
          store_u32_be(&dptr[offset], val);
        }
      break;
    }
    case RTG_FMT_RGB24:
    case RTG_FMT_BGR24: {
      for (int xs = 0; xs < width; xs++) {
        size_t offset = (size_t)xs * rtg_pixel_size[pixel_format];
        uint32_t val = rtg_load_pixel(&dptr[offset], pixel_format);
        val = ~val;
        rtg_store_pixel(&dptr[offset], pixel_format, val);
      }
      break;
    }
    }
    dptr += dst_pitch;
  }
}

void rtg_blitrect(uint16_t src_x, uint16_t src_y, uint16_t dst_x, uint16_t dst_y, uint16_t width, uint16_t height,
                  uint16_t line_pitch, uint16_t pixel_format, uint8_t color_mask) {
  uint8_t mask = color_mask;
  if (mask) {
  }
  uint8_t* sptr = NULL;
  uint8_t* dptr = NULL;
  if (!rtg_get_ptr_checked(rtg_address_adj[0], src_x, src_y, width, height, line_pitch, pixel_format, "blitrect_src", &sptr)) {
    return;
  }
  if (!rtg_get_ptr_checked(rtg_address_adj[0], dst_x, dst_y, width, height, line_pitch, pixel_format, "blitrect_dst", &dptr)) {
    return;
  }

  int xdir = 1;
  int32_t pitch_step = line_pitch;

  if (src_y < dst_y) {
    pitch_step = -line_pitch;
    sptr += ((height - 1) * line_pitch);
    dptr += ((height - 1) * line_pitch);
  }
  if (src_x < dst_x) {
    xdir = 0;
  }

  for (int ys = 0; ys < height; ys++) {
    if (pixel_format == RTG_FMT_8BIT_CLUT) {
      if (xdir) {
        for (int xs = 0; xs < width; xs++) {
          SET_RTG_PIXEL_MASK(&dptr[xs], sptr[xs], pixel_format);
        }
      } else {
        for (int xs = (int)width - 1; xs >= 0; xs--) {
          SET_RTG_PIXEL_MASK(&dptr[xs], sptr[xs], pixel_format);
        }
      }
    } else {
      size_t bpp = rtg_pixel_size[pixel_format];
        if (xdir) {
          for (int xs = 0; xs < width; xs++) {
            switch (pixel_format) {
            case RTG_FMT_RGB565_LE:
            case RTG_FMT_RGB565_BE:
            case RTG_FMT_BGR565_LE:
            case RTG_FMT_RGB555_LE:
            case RTG_FMT_RGB555_BE:
            case RTG_FMT_BGR555_LE:
              {
                size_t src_offset = rtg_index_offset(xs, sizeof(uint16_t));
                size_t dst_offset = rtg_index_offset(xs, bpp);
                uint16_t pixel_val = load_u16_be(&sptr[src_offset]);
                SET_RTG_PIXEL_MASK(&dptr[dst_offset], pixel_val, pixel_format);
              }
              break;
            case RTG_FMT_RGB32_ABGR:
            case RTG_FMT_RGB32_ARGB:
            case RTG_FMT_RGB32_BGRA:
            case RTG_FMT_RGB32_RGBA:
              {
                size_t src_offset = rtg_index_offset(xs, sizeof(uint32_t));
                size_t dst_offset = rtg_index_offset(xs, bpp);
                uint32_t pixel_val = load_u32_be(&sptr[src_offset]);
                SET_RTG_PIXEL_MASK(&dptr[dst_offset], pixel_val, pixel_format);
              }
              break;
            case RTG_FMT_RGB24:
            case RTG_FMT_BGR24:
              {
                size_t src_offset = rtg_index_offset(xs, bpp);
                size_t dst_offset = rtg_index_offset(xs, bpp);
                uint32_t pixel_val = rtg_load_pixel(&sptr[src_offset], pixel_format);
                SET_RTG_PIXEL_MASK(&dptr[dst_offset], pixel_val, pixel_format);
              }
              break;
            }
          }
        } else {
          for (int xs = (int)width - 1; xs >= 0; xs--) {
            switch (pixel_format) {
            case RTG_FMT_RGB565_LE:
            case RTG_FMT_RGB565_BE:
            case RTG_FMT_BGR565_LE:
            case RTG_FMT_RGB555_LE:
            case RTG_FMT_RGB555_BE:
            case RTG_FMT_BGR555_LE:
              {
                size_t src_offset = rtg_index_offset(xs, sizeof(uint16_t));
                size_t dst_offset = rtg_index_offset(xs, bpp);
                uint16_t pixel_val = load_u16_be(&sptr[src_offset]);
                SET_RTG_PIXEL_MASK(&dptr[dst_offset], pixel_val, pixel_format);
              }
              break;
            case RTG_FMT_RGB32_ABGR:
            case RTG_FMT_RGB32_ARGB:
            case RTG_FMT_RGB32_BGRA:
            case RTG_FMT_RGB32_RGBA:
              {
                size_t src_offset = rtg_index_offset(xs, sizeof(uint32_t));
                size_t dst_offset = rtg_index_offset(xs, bpp);
                uint32_t pixel_val = load_u32_be(&sptr[src_offset]);
                SET_RTG_PIXEL_MASK(&dptr[dst_offset], pixel_val, pixel_format);
              }
              break;
            case RTG_FMT_RGB24:
            case RTG_FMT_BGR24:
              {
                size_t src_offset = rtg_index_offset(xs, bpp);
                size_t dst_offset = rtg_index_offset(xs, bpp);
                uint32_t pixel_val = rtg_load_pixel(&sptr[src_offset], pixel_format);
                SET_RTG_PIXEL_MASK(&dptr[dst_offset], pixel_val, pixel_format);
              }
              break;
            }
          }
        }
    }
    sptr += pitch_step;
    dptr += pitch_step;
  }
}

void rtg_blitrect_solid(uint16_t src_x, uint16_t src_y, uint16_t dst_x, uint16_t dst_y, uint16_t width, uint16_t height,
                        uint16_t line_pitch, int16_t pixel_format) {
  uint8_t* sptr = NULL;
  uint8_t* dptr = NULL;
  if (!rtg_get_ptr_checked(rtg_address_adj[0], src_x, src_y, width, height, line_pitch, pixel_format, "blitrect_solid_src",
                           &sptr)) {
    return;
  }
  if (!rtg_get_ptr_checked(rtg_address_adj[0], dst_x, dst_y, width, height, line_pitch, pixel_format, "blitrect_solid_dst",
                           &dptr)) {
    return;
  }

  int xdir = 1;
  int32_t pitch_step = line_pitch;

  if (src_y < dst_y) {
    pitch_step = -line_pitch;
    sptr += ((height - 1) * line_pitch);
    dptr += ((height - 1) * line_pitch);
  }
  if (src_x < dst_x) {
    xdir = 0;
  }

  for (int ys = 0; ys < height; ys++) {
    if (xdir)
      memcpy(dptr, sptr, width * rtg_pixel_size[pixel_format]);
    else
      memmove(dptr, sptr, width * rtg_pixel_size[pixel_format]);
    sptr += pitch_step;
    dptr += pitch_step;
  }
}

void rtg_blitrect_nomask_complete(uint16_t src_x, uint16_t src_y, uint16_t dst_x, uint16_t dst_y, uint16_t width,
                                  uint16_t height, uint16_t src_pitch, uint16_t dst_pitch,
                                  uint32_t src_addr, uint32_t dst_addr, uint16_t format,
                                  int8_t minterm) {
  if (minterm) {
  }
  uint8_t* sptr = NULL;
  uint8_t* dptr = NULL;
  uint32_t src_base = src_addr - (RTG_BASE + RTG_REG_SIZE);
  uint32_t dst_base = dst_addr - (RTG_BASE + RTG_REG_SIZE);
  if (!rtg_get_ptr_checked(src_base, src_x, src_y, width, height, src_pitch, format, "blitrect_nomask_src",
                           &sptr)) {
    return;
  }
  if (!rtg_get_ptr_checked(dst_base, dst_x, dst_y, width, height, dst_pitch, format, "blitrect_nomask_dst",
                           &dptr)) {
    return;
  }

  int xdir = 1;
  int32_t src_pitch_step = src_pitch;
  int32_t dst_pitch_step = dst_pitch;
  uint8_t draw_mode = minterm;
  uint32_t mask = 0xFF;

  if (src_addr == dst_addr) {
    if (src_y < dst_y) {
      src_pitch_step = -src_pitch;
      sptr += ((height - 1) * src_pitch);
      dst_pitch_step = -dst_pitch;
      dptr += ((height - 1) * dst_pitch);
    }
    if (src_x < dst_x) {
      xdir = 0;
    }
  }

  switch (format) {
  case RTG_FMT_RGB565_LE:
  case RTG_FMT_RGB565_BE:
  case RTG_FMT_BGR565_LE:
  case RTG_FMT_RGB555_LE:
  case RTG_FMT_RGB555_BE:
  case RTG_FMT_BGR555_LE:
    mask = 0xFFFF;
    break;
  case RTG_FMT_RGB32_ABGR:
  case RTG_FMT_RGB32_ARGB:
  case RTG_FMT_RGB32_BGRA:
  case RTG_FMT_RGB32_RGBA:
    mask = 0xFFFFFFFF;
    break;
  case RTG_FMT_RGB24:
  case RTG_FMT_BGR24:
    mask = 0x00FFFFFF;
    break;
  default:
    break;
  }

  if (minterm == MINTERM_SRC) {
    {
      size_t row_bytes = (size_t)width * rtg_pixel_size[format];
      for (int ys = 0; ys < height; ys++) {
        if (xdir)
          memcpy(dptr, sptr, row_bytes);
        else
          memmove(dptr, sptr, row_bytes);
        sptr += src_pitch_step;
        dptr += dst_pitch_step;
      }
    }
  } else {
    for (int ys = 0; ys < height; ys++) {
      if (xdir) {
        for (int xs = 0; xs < width; xs++) {
          switch (format) {
          case RTG_FMT_8BIT_CLUT:
            HANDLE_MINTERM_PIXEL(sptr[xs], dptr[xs], format);
            break;
          case RTG_FMT_RGB565_LE:
          case RTG_FMT_RGB565_BE:
          case RTG_FMT_BGR565_LE:
          case RTG_FMT_RGB555_LE:
          case RTG_FMT_RGB555_BE:
          case RTG_FMT_BGR555_LE:
            {
              size_t src_offset = rtg_index_offset(xs, sizeof(uint16_t));
              size_t dst_offset = rtg_index_offset(xs, sizeof(uint16_t));
              uint16_t src_val = load_u16_be(&sptr[src_offset]);
              uint16_t dst_val = load_u16_be(&dptr[dst_offset]);
              HANDLE_MINTERM_PIXEL(src_val, dst_val, format);
            }
            break;
          case RTG_FMT_RGB32_ABGR:
          case RTG_FMT_RGB32_ARGB:
          case RTG_FMT_RGB32_BGRA:
          case RTG_FMT_RGB32_RGBA:
            {
              size_t src_offset = rtg_index_offset(xs, sizeof(uint32_t));
              size_t dst_offset = rtg_index_offset(xs, sizeof(uint32_t));
              uint32_t src_val = load_u32_be(&sptr[src_offset]);
              uint32_t dst_val = load_u32_be(&dptr[dst_offset]);
              HANDLE_MINTERM_PIXEL(src_val, dst_val, format);
            }
            break;
          case RTG_FMT_RGB24:
          case RTG_FMT_BGR24:
            {
              size_t src_offset = rtg_index_offset(xs, rtg_pixel_size[format]);
              size_t dst_offset = rtg_index_offset(xs, rtg_pixel_size[format]);
              uint32_t src_val = rtg_load_pixel(&sptr[src_offset], format);
              uint32_t dst_val = rtg_load_pixel(&dptr[dst_offset], format);
              HANDLE_MINTERM_PIXEL(src_val, dst_val, format);
            }
            break;
          }
        }
      } else {
        for (int xs = (int)width - 1; xs >= 0; xs--) {
          switch (format) {
          case RTG_FMT_8BIT_CLUT:
            HANDLE_MINTERM_PIXEL(sptr[xs], dptr[xs], format);
            break;
          case RTG_FMT_RGB565_LE:
          case RTG_FMT_RGB565_BE:
          case RTG_FMT_BGR565_LE:
          case RTG_FMT_RGB555_LE:
          case RTG_FMT_RGB555_BE:
          case RTG_FMT_BGR555_LE:
            {
              size_t src_offset = rtg_index_offset(xs, sizeof(uint16_t));
              size_t dst_offset = rtg_index_offset(xs, sizeof(uint16_t));
              uint16_t src_val = load_u16_be(&sptr[src_offset]);
              uint16_t dst_val = load_u16_be(&dptr[dst_offset]);
              HANDLE_MINTERM_PIXEL(src_val, dst_val, format);
            }
            break;
          case RTG_FMT_RGB32_ABGR:
          case RTG_FMT_RGB32_ARGB:
          case RTG_FMT_RGB32_BGRA:
          case RTG_FMT_RGB32_RGBA:
            {
              size_t src_offset = rtg_index_offset(xs, sizeof(uint32_t));
              size_t dst_offset = rtg_index_offset(xs, sizeof(uint32_t));
              uint32_t src_val = load_u32_be(&sptr[src_offset]);
              uint32_t dst_val = load_u32_be(&dptr[dst_offset]);
              HANDLE_MINTERM_PIXEL(src_val, dst_val, format);
            }
            break;
          case RTG_FMT_RGB24:
          case RTG_FMT_BGR24:
            {
              size_t src_offset = rtg_index_offset(xs, rtg_pixel_size[format]);
              size_t dst_offset = rtg_index_offset(xs, rtg_pixel_size[format]);
              uint32_t src_val = rtg_load_pixel(&sptr[src_offset], format);
              uint32_t dst_val = rtg_load_pixel(&dptr[dst_offset], format);
              HANDLE_MINTERM_PIXEL(src_val, dst_val, format);
            }
            break;
          }
        }
      }
      sptr += src_pitch_step;
      dptr += dst_pitch_step;
    }
  }
}

extern struct emulator_config* cfg;

void rtg_blittemplate(uint16_t dst_x, uint16_t dst_y, uint16_t width, uint16_t height, uint32_t src_addr,
                      uint32_t fg_color_raw, uint32_t bg_color_raw, uint16_t dst_pitch, uint16_t template_pitch,
                      uint16_t pixel_format, uint16_t offset_x, uint8_t color_mask, uint8_t draw_mode) {
  // P96 uses template blits for window decorations (gadgets/scrollbars/titlebar text/masks).
  // Legacy RTG macros below rely on these local variable names.
  uint16_t pitch = dst_pitch;
  uint16_t t_pitch = template_pitch;
  uint8_t mask = color_mask;
  uint8_t* dptr = NULL;
  if (!rtg_get_ptr_checked(rtg_address_adj[1], dst_x, dst_y, width, height, dst_pitch, pixel_format, "blittemplate",
                           &dptr)) {
    return;
  }
  uint8_t* sptr = NULL;
  uint8_t cur_bit = 0, base_bit = 0, cur_byte = 0;
  uint8_t invert = (draw_mode & DRAWMODE_INVERSVID);
  uint16_t tmpl_x = 0;

  draw_mode &= 0x03;

  tmpl_x = offset_x / 4;
  cur_bit = base_bit = (0x80 >> (offset_x % 8));

  if (realtime_graphics_debug) {
    size_t bpp = (pixel_format < RTG_FMT_NUM) ? rtg_pixel_size[pixel_format] : 0;
    LOG_DEBUG("DEBUG: BlitTemplate - %d, %d (%dx%d)\n", dst_x, dst_y, width, height);
    LOG_DEBUG("Src: %.8X\n", src_addr);
    LOG_DEBUG("Dest: %.8X (%.8X)\n", rtg_address[1], rtg_address_adj[1]);
    LOG_DEBUG("pitch: %d t_pitch: %d format: %d\n", dst_pitch, template_pitch, pixel_format);
    LOG_DEBUG("offset_x: %d mask: %.2X draw_mode: %d\n", offset_x, color_mask, draw_mode);
    LOG_DEBUG("bpp: %zu display_format: %u fb_adj: %.8X\n", bpp, rtg_display_format,
              framebuffer_addr_adj);
  }

  uint32_t fg_color = htobe32(fg_color_raw);
  uint32_t bg_color = htobe32(bg_color_raw);

  switch (pixel_format) {
  case RTG_FMT_RGB565_LE:
  case RTG_FMT_RGB565_BE:
  case RTG_FMT_BGR565_LE:
  case RTG_FMT_RGB555_LE:
  case RTG_FMT_RGB555_BE:
  case RTG_FMT_BGR555_LE:
    fg_color = htobe16((fg_color_raw & 0xFFFF));
    bg_color = htobe16((bg_color_raw & 0xFFFF));
    break;
  case RTG_FMT_8BIT_CLUT:
  case RTG_FMT_4BIT_PLANAR:
    fg_color = (fg_color_raw & 0xFF);
    bg_color = (bg_color_raw & 0xFF);
    break;
  default:
    break;
  }

  if (realtime_graphics_debug) {
    size_t bpp = (pixel_format < RTG_FMT_NUM) ? rtg_pixel_size[pixel_format] : 0;
    LOG_DEBUG("DEBUG: BlitTemplate - %d, %d (%dx%d)\n", dst_x, dst_y, width, height);
    LOG_DEBUG("Src: %.8X\n", src_addr);
    LOG_DEBUG("Dest: %.8X (%.8X)\n", rtg_address[1], rtg_address_adj[1]);
    LOG_DEBUG("pitch: %d t_pitch: %d format: %d\n", dst_pitch, template_pitch, pixel_format);
    LOG_DEBUG("offset_x: %d mask: %.2X draw_mode: %d\n", offset_x, color_mask, draw_mode);
    LOG_DEBUG("bpp: %zu display_format: %u fb_adj: %.8X\n", bpp, rtg_display_format,
              framebuffer_addr_adj);
  }

  sptr = get_mapped_data_pointer_by_address(cfg, src_addr);
  if (!sptr) {
    if (realtime_graphics_debug) {
      LOG_DEBUG("BlitTemplate data NOT available in mapped range, source address: $%.8X\n",
                src_addr);
    }
  } else {
    if (realtime_graphics_debug) {
      LOG_DEBUG("BlitTemplate data available in mapped range at $%.8X\n", src_addr);
    }
  }

  switch (draw_mode) {
  case DRAWMODE_JAM1:
    for (uint16_t ys = 0; ys < height; ys++) {
      for (int xs = 0; xs < width; xs++) {
        TEMPLATE_LOOPX;
        if (width >= 8 && cur_bit == 0x80 && xs < width - 8) {
          if (color_mask == 0xFF || pixel_format != RTG_FMT_8BIT_CLUT) {
            SET_RTG_PIXELS(rtg_pixel_at(dptr, (size_t)xs, pixel_format), fg_color, pixel_format);
          } else {
            SET_RTG_PIXELS_MASK(&dptr[xs], fg_color, pixel_format);
          }
          xs += 7;
        } else {
          while (cur_bit > 0 && xs < width) {
            if (cur_byte & cur_bit) {
              if (color_mask == 0xFF || pixel_format != RTG_FMT_8BIT_CLUT) {
                SET_RTG_PIXEL(rtg_pixel_at(dptr, (size_t)xs, pixel_format), fg_color, pixel_format);
              } else {
                SET_RTG_PIXEL_MASK(&dptr[xs], fg_color, pixel_format);
              }
            }
            xs++;
            cur_bit >>= 1;
          }
          xs--;
          cur_bit = 0x80;
        }
      }
      TEMPLATE_LOOPY;
    }
    return;
  case DRAWMODE_JAM2:
    for (uint16_t ys = 0; ys < height; ys++) {
      for (int xs = 0; xs < width; xs++) {
        TEMPLATE_LOOPX;
        if (width >= 8 && cur_bit == 0x80 && xs < width - 8) {
            if (color_mask == 0xFF || pixel_format != RTG_FMT_8BIT_CLUT) {
              SET_RTG_PIXELS2_COND(rtg_pixel_at(dptr, (size_t)xs, pixel_format), fg_color, bg_color, pixel_format);
            } else {
              SET_RTG_PIXELS2_COND_MASK(rtg_pixel_at(dptr, (size_t)xs, pixel_format), fg_color, bg_color,
                                        pixel_format);
            }

          xs += 7;
        } else {
          while (cur_bit > 0 && xs < width) {
            if (color_mask == 0xFF || pixel_format != RTG_FMT_8BIT_CLUT) {
                SET_RTG_PIXEL(rtg_pixel_at(dptr, (size_t)xs, pixel_format),
                              (cur_byte & cur_bit) ? fg_color : bg_color, pixel_format);
            } else {
              SET_RTG_PIXEL_MASK(rtg_pixel_at(dptr, (size_t)xs, pixel_format),
                                 (cur_byte & cur_bit) ? fg_color : bg_color, pixel_format);
            }
            xs++;
            cur_bit >>= 1;
          }
          xs--;
          cur_bit = 0x80;
        }
      }
      TEMPLATE_LOOPY;
    }
    return;
  case DRAWMODE_COMPLEMENT:
    for (uint16_t ys = 0; ys < height; ys++) {
      for (int xs = 0; xs < width; xs++) {
        TEMPLATE_LOOPX;
        if (width >= 8 && cur_bit == 0x80 && xs < width - 8) {
          INVERT_RTG_PIXELS(rtg_pixel_at(dptr, (size_t)xs, pixel_format), pixel_format)
          xs += 7;
        } else {
          while (cur_bit > 0 && xs < width) {
            if (cur_byte & cur_bit) {
              INVERT_RTG_PIXEL(rtg_pixel_at(dptr, (size_t)xs, pixel_format), pixel_format)
            }
            xs++;
            cur_bit >>= 1;
          }
          xs--;
          cur_bit = 0x80;
        }
      }
      TEMPLATE_LOOPY;
    }
    return;
  }
}

void rtg_blitpattern(uint16_t dst_x, uint16_t dst_y, uint16_t width, uint16_t height, uint32_t src_addr_,
                     uint32_t fg_color_raw, uint32_t bg_color_raw, uint16_t dst_pitch, uint16_t pixel_format,
                     uint16_t offset_x, uint16_t offset_y, uint8_t mask, uint8_t draw_mode,
                     uint8_t loop_rows) {
  // Legacy RTG macros below rely on this local variable name.
  uint16_t pitch = dst_pitch;
  if (mask) {
  }

#ifdef RTG_STUB_PATTERN
  uint32_t fill = (draw_mode == DRAWMODE_JAM2) ? bg_color_raw : fg_color_raw;
  rtg_fillrect_solid(dst_x, dst_y, width, height, fill, dst_pitch, pixel_format);
  return;
#endif

  // P96 uses pattern blits for window decoration fills and requesters.
  uint8_t* dptr = NULL;
  if (!rtg_get_ptr_checked(rtg_address_adj[1], dst_x, dst_y, width, height, dst_pitch, pixel_format, "blitpattern",
                           &dptr)) {
    return;
  }
  if (loop_rows == 0) {
    if (rtg_oob_log_count < 20) {
      LOG_DEBUG("[RTG/OOB] blitpattern invalid loop_rows=0\n");
      rtg_oob_log_count++;
    }
    loop_rows = 1;
  }
  uint8_t *sptr = NULL, *sptr_base = NULL;
  uint8_t cur_bit = 0, base_bit = 0, cur_byte = 0;
  uint8_t invert = (draw_mode & DRAWMODE_INVERSVID);
  uint16_t tmpl_x = 0;
  uint32_t src_addr = src_addr_;
  uint32_t src_addr_base = src_addr;

  draw_mode &= 0x03;

  tmpl_x = (offset_x / 8) % 2;
  cur_bit = base_bit = (0x80 >> (offset_x % 8));

  uint32_t fg_color = htobe32(fg_color_raw);
  uint32_t bg_color = htobe32(bg_color_raw);

  switch (pixel_format) {
  case RTG_FMT_RGB565_LE:
  case RTG_FMT_RGB565_BE:
  case RTG_FMT_BGR565_LE:
  case RTG_FMT_RGB555_LE:
  case RTG_FMT_RGB555_BE:
  case RTG_FMT_BGR555_LE:
    htobe16((fg_color_raw & 0xFFFF));
    htobe16((bg_color_raw & 0xFFFF));
    break;
  case RTG_FMT_8BIT_CLUT:
  case RTG_FMT_4BIT_PLANAR:
    fg_color = (fg_color_raw & 0xFF);
    bg_color = (bg_color_raw & 0xFF);
    break;
  default:
    break;
  }

  if (realtime_graphics_debug) {
    size_t bpp = (pixel_format < RTG_FMT_NUM) ? rtg_pixel_size[pixel_format] : 0;
    LOG_DEBUG("DEBUG: BlitPattern - %d, %d (%dx%d)\n", dst_x, dst_y, width, height);
    LOG_DEBUG("Src: %.8X\n", src_addr);
    LOG_DEBUG("Dest: %.8X (%.8X)\n", rtg_address[1], rtg_address_adj[1]);
    LOG_DEBUG("pitch: %d format: %d\n", dst_pitch, pixel_format);
    LOG_DEBUG("offset_x: %d offset_y: %d mask: %.2X draw_mode: %d loop_rows: %u\n", offset_x,
              offset_y, mask, draw_mode, loop_rows);
    LOG_DEBUG("bpp: %zu display_format: %u fb_adj: %.8X\n", bpp, rtg_display_format,
              framebuffer_addr_adj);
  }

  sptr = get_mapped_data_pointer_by_address(cfg, src_addr);
  if (!sptr) {
    if (realtime_graphics_debug) {
      LOG_DEBUG("BlitPattern data NOT available in mapped range, source address: $%.8X\n",
                src_addr);
      src_addr += (offset_y % loop_rows) * 2;
    }
  } else {
    if (realtime_graphics_debug) {
      LOG_DEBUG("BlitPattern data available in mapped range at $%.8X\n", src_addr);
    }
    sptr_base = sptr;
    sptr += (offset_y % loop_rows) * 2;
  }

  switch (draw_mode) {
  case DRAWMODE_JAM1:
    for (uint16_t ys = 0; ys < height; ys++) {
      for (int xs = 0; xs < width; xs++) {
        PATTERN_LOOPX;
        if (width >= 8 && cur_bit == 0x80 && xs < width - 8) {
          if (mask == 0xFF || pixel_format != RTG_FMT_8BIT_CLUT) {
            SET_RTG_PIXELS(rtg_pixel_at(dptr, (size_t)xs, pixel_format), fg_color, pixel_format);
          } else {
            SET_RTG_PIXELS_MASK(&dptr[xs], fg_color, pixel_format);
          }
          xs += 7;
        } else {
          while (cur_bit > 0 && xs < width) {
            if (cur_byte & cur_bit) {
              if (mask == 0xFF || pixel_format != RTG_FMT_8BIT_CLUT) {
                SET_RTG_PIXEL(rtg_pixel_at(dptr, (size_t)xs, pixel_format), fg_color, pixel_format);
              } else {
                SET_RTG_PIXEL_MASK(rtg_pixel_at(dptr, (size_t)xs, pixel_format), fg_color, pixel_format);
              }
            }
            xs++;
            cur_bit >>= 1;
          }
          xs--;
          cur_bit = 0x80;
        }
      }
      PATTERN_LOOPY;
    }
    return;
  case DRAWMODE_JAM2:
    for (uint16_t ys = 0; ys < height; ys++) {
      for (int xs = 0; xs < width; xs++) {
        PATTERN_LOOPX;
        if (width >= 8 && cur_bit == 0x80 && xs < width - 8) {
            if (mask == 0xFF || pixel_format != RTG_FMT_8BIT_CLUT) {
              SET_RTG_PIXELS2_COND(rtg_pixel_at(dptr, (size_t)xs, pixel_format), fg_color, bg_color, pixel_format);
            } else {
              SET_RTG_PIXELS2_COND_MASK(rtg_pixel_at(dptr, (size_t)xs, pixel_format), fg_color, bg_color,
                                        pixel_format);
            }

          xs += 7;
        } else {
          while (cur_bit > 0 && xs < width) {
            if (mask == 0xFF || pixel_format != RTG_FMT_8BIT_CLUT) {
                SET_RTG_PIXEL(rtg_pixel_at(dptr, (size_t)xs, pixel_format),
                              (cur_byte & cur_bit) ? fg_color : bg_color, pixel_format);
            } else {
              SET_RTG_PIXEL_MASK(rtg_pixel_at(dptr, (size_t)xs, pixel_format),
                                (cur_byte & cur_bit) ? fg_color : bg_color, pixel_format);
            }
            xs++;
            cur_bit >>= 1;
          }
          xs--;
          cur_bit = 0x80;
        }
      }
      PATTERN_LOOPY;
    }
    return;
  case DRAWMODE_COMPLEMENT:
    for (uint16_t ys = 0; ys < height; ys++) {
      for (int xs = 0; xs < width; xs++) {
        PATTERN_LOOPX;
        if (width >= 8 && cur_bit == 0x80 && xs < width - 8) {
          INVERT_RTG_PIXELS(rtg_pixel_at(dptr, (size_t)xs, pixel_format), pixel_format)
          xs += 7;
        } else {
          while (cur_bit > 0 && xs < width) {
            if (cur_byte & cur_bit) {
              INVERT_RTG_PIXEL(rtg_pixel_at(dptr, (size_t)xs, pixel_format), pixel_format)
            }
            xs++;
            cur_bit >>= 1;
          }
          xs--;
          cur_bit = 0x80;
        }
      }
      PATTERN_LOOPY;
    }
    return;
  }
}

void rtg_drawline_solid(int16_t start_x, int16_t start_y, int16_t delta_x, int16_t delta_y, uint16_t length,
                        uint32_t fg_color_raw, uint16_t dst_pitch, uint16_t pixel_format) {
  int16_t line_start_x = start_x;
  int16_t line_start_y = start_y;
  int16_t line_end_x = start_x + delta_x;
  int16_t line_end_y = start_y + delta_y;
  int32_t min_x = (line_start_x < line_end_x) ? line_start_x : line_end_x;
  int32_t max_x = (line_start_x > line_end_x) ? line_start_x : line_end_x;
  int32_t min_y = (line_start_y < line_end_y) ? line_start_y : line_end_y;
  int32_t max_y = (line_start_y > line_end_y) ? line_start_y : line_end_y;
  if (min_x < 0 || min_y < 0) {
    if (rtg_oob_log_count < 20) {
      LOG_DEBUG("[RTG/OOB] drawline_solid negative coords: (%d,%d)-(%d,%d)\n", line_start_x, line_start_y, line_end_x, line_end_y);
      rtg_oob_log_count++;
    }
    return;
  }
  uint8_t* base_ptr = NULL;
  int32_t span_w32 = max_x - min_x + 1;
  int32_t span_h32 = max_y - min_y + 1;
  if (span_w32 < 0)
    span_w32 = 0;
  if (span_h32 < 0)
    span_h32 = 0;
  uint16_t span_w = (uint16_t)span_w32;
  uint16_t span_h = (uint16_t)span_h32;
  if (!rtg_get_ptr_checked(rtg_address_adj[0], (uint16_t)min_x, (uint16_t)min_y, span_w, span_h,
                           dst_pitch, pixel_format, "drawline_solid", &base_ptr)) {
    return;
  }
  (void)base_ptr;

  uint32_t fg_color = htobe32(fg_color_raw);

  switch (pixel_format) {
  case RTG_FMT_RGB565_LE:
  case RTG_FMT_RGB565_BE:
  case RTG_FMT_BGR565_LE:
  case RTG_FMT_RGB555_LE:
  case RTG_FMT_RGB555_BE:
  case RTG_FMT_BGR555_LE:
    fg_color = htobe16((fg_color_raw & 0xFFFF));
    break;
  case RTG_FMT_8BIT_CLUT:
  case RTG_FMT_4BIT_PLANAR:
    fg_color = (fg_color_raw & 0xFF);
    break;
  default:
    break;
  }

  uint8_t* dptr = &rtg_mem[rtg_address_adj[0] + ((size_t)line_start_y * dst_pitch)];

  int32_t line_step = dst_pitch;
  int8_t x_step = 1;

  int32_t line_dx, line_dy, dx_abs, dy_abs, ix, iy;
  int16_t current_x = line_start_x;

  if (line_end_x < line_start_x)
    x_step = -1;
  if (line_end_y < line_start_y)
    line_step = -dst_pitch;

  line_dx = line_end_x - line_start_x;
  line_dy = line_end_y - line_start_y;
  dx_abs = abs(line_dx);
  dy_abs = abs(line_dy);
  ix = dy_abs >> 1;
  iy = dx_abs >> 1;

  SET_RTG_PIXEL(rtg_line_pixel_ptr(dptr, current_x, pixel_format), fg_color, pixel_format);

  if (dx_abs >= dy_abs) {
    if (!length)
      length = (uint16_t)dx_abs;
    for (uint16_t i = 0; i < length; i++) {
      iy += dy_abs;
      if (iy >= dx_abs) {
        iy -= dx_abs;
        dptr += line_step;
      }
      current_x += x_step;

      SET_RTG_PIXEL(rtg_line_pixel_ptr(dptr, current_x, pixel_format), fg_color, pixel_format);
    }
  } else {
    if (!length)
      length = (uint16_t)dy_abs;
    for (uint16_t i = 0; i < length; i++) {
      ix += dx_abs;
      if (ix >= dy_abs) {
        ix -= dy_abs;
        current_x += x_step;
      }
      dptr += line_step;

      SET_RTG_PIXEL(rtg_line_pixel_ptr(dptr, current_x, pixel_format), fg_color, pixel_format);
    }
  }
}

#define DRAW_LINE_PIXEL                                                                            \
  do {                                                                                             \
    uint8_t* __rtg_line_pixel = rtg_line_pixel_ptr(dptr, x, format);                              \
    if (pattern & cur_bit) {                                                                       \
      if (invert) {                                                                                \
        INVERT_RTG_PIXEL(__rtg_line_pixel, format)                                                 \
      } else {                                                                                     \
        if (mask == 0xFF || format != RTG_FMT_8BIT_CLUT) {                                          \
          SET_RTG_PIXEL(__rtg_line_pixel, fg_color, format);                                      \
        } else {                                                                                  \
          SET_RTG_PIXEL_MASK(__rtg_line_pixel, fg_color, format);                                 \
        }                                                                                         \
      }                                                                                           \
    } else if (draw_mode == DRAWMODE_JAM2) {                                                       \
      if (invert) {                                                                                \
        INVERT_RTG_PIXEL(__rtg_line_pixel, format)                                                 \
      } else {                                                                                     \
        if (mask == 0xFF || format != RTG_FMT_8BIT_CLUT) {                                          \
          SET_RTG_PIXEL(__rtg_line_pixel, bg_color, format);                                      \
        } else {                                                                                  \
          SET_RTG_PIXEL_MASK(__rtg_line_pixel, bg_color, format);                                 \
        }                                                                                         \
      }                                                                                           \
    }                                                                                             \
  } while (0);                                                                                     \
  if ((cur_bit >>= 1) == 0)                                                                        \
    cur_bit = 0x8000;

void rtg_drawline(int16_t x1_, int16_t y1_, int16_t x2_, int16_t y2_, uint16_t len,
                  uint16_t pattern, uint16_t pattern_offset, uint32_t fgcol, uint32_t bgcol,
                  uint16_t pitch, uint16_t format, uint8_t mask, uint8_t draw_mode) {
  if (pattern_offset) {
  }

  int16_t x1 = x1_, y1 = y1_;
  int16_t x2 = x1_ + x2_, y2 = y1 + y2_;
  int32_t min_x = (x1 < x2) ? x1 : x2;
  int32_t max_x = (x1 > x2) ? x1 : x2;
  int32_t min_y = (y1 < y2) ? y1 : y2;
  int32_t max_y = (y1 > y2) ? y1 : y2;
  if (min_x < 0 || min_y < 0) {
    if (rtg_oob_log_count < 20) {
      LOG_DEBUG("[RTG/OOB] drawline negative coords: (%d,%d)-(%d,%d)\n", x1, y1, x2, y2);
      rtg_oob_log_count++;
    }
    return;
  }
  uint8_t* base_ptr = NULL;
  uint16_t span_w = (uint16_t)(max_x - min_x + 1);
  uint16_t span_h = (uint16_t)(max_y - min_y + 1);
  if (!rtg_get_ptr_checked(rtg_address_adj[0], (uint16_t)min_x, (uint16_t)min_y, span_w, span_h,
                           pitch, format, "drawline", &base_ptr)) {
    return;
  }
  (void)base_ptr;
  uint16_t cur_bit = 0x8000;
  // uint32_t color_mask = 0xFFFF0000;
  uint8_t invert = 0;

  uint32_t fg_color = htobe32(fgcol);
  uint32_t bg_color = htobe32(bgcol);

  switch (format) {
  case RTG_FMT_RGB565_LE:
  case RTG_FMT_RGB565_BE:
  case RTG_FMT_BGR565_LE:
  case RTG_FMT_RGB555_LE:
  case RTG_FMT_RGB555_BE:
  case RTG_FMT_BGR555_LE:
    fg_color = htobe16((fgcol & 0xFFFF));
    bg_color = htobe16((bgcol & 0xFFFF));
    break;
  case RTG_FMT_8BIT_CLUT:
  case RTG_FMT_4BIT_PLANAR:
    fg_color = (fgcol & 0xFF);
    bg_color = (bgcol & 0xFF);
    break;
  default:
    break;
  }

  uint8_t* dptr = &rtg_mem[rtg_address_adj[0] + ((size_t)y1 * pitch)];

  int32_t line_step = pitch;
  int8_t x_step = 1;

  int32_t dx, dy, dx_abs, dy_abs, ix, iy;
  int16_t x = x1;

  if (x2 < x1)
    x_step = -1;
  if (y2 < y1)
    line_step = -pitch;

  dx = x2 - x1;
  dy = y2 - y1;
  dx_abs = abs(dx);
  dy_abs = abs(dy);
  ix = dy_abs >> 1;
  iy = dx_abs >> 1;

  if (draw_mode & DRAWMODE_INVERSVID)
    pattern = ~pattern;
  if (draw_mode & DRAWMODE_COMPLEMENT) {
    invert = 1;
  }
  draw_mode &= 0x01;

  DRAW_LINE_PIXEL;

  if (dx_abs >= dy_abs) {
    if (!len)
      len = (uint16_t)dx_abs;
    for (uint16_t i = 0; i < len; i++) {
      iy += dy_abs;
      if (iy >= dx_abs) {
        iy -= dx_abs;
        dptr += line_step;
      }
      x += x_step;

      DRAW_LINE_PIXEL;
    }
  } else {
    if (!len)
      len = (uint16_t)dy_abs;
    for (uint16_t i = 0; i < len; i++) {
      ix += dx_abs;
      if (ix >= dy_abs) {
        ix -= dy_abs;
        x += x_step;
      }
      dptr += line_step;

      DRAW_LINE_PIXEL;
    }
  }
}

// This is slow and somewhat useless, needs a rewrite to ps_read_16 copy the bit plane data
// similarly to what the code in the RTG driver does. Disabled for now.
void rtg_p2c_ex(int16_t src_x, int16_t src_y, int16_t dst_x, int16_t dst_y, int16_t width, int16_t height,
                uint8_t minterm, struct BitMap* bm, uint8_t mask, uint16_t dst_pitch,
                uint16_t src_pitch) {
  uint16_t pitch = dst_pitch;
  uint8_t* dptr = NULL;
  if (dst_x < 0 || dst_y < 0) {
    if (rtg_oob_log_count < 20) {
      LOG_DEBUG("[RTG/OOB] p2c_ex invalid coords: dx=%d dy=%d\n", dst_x, dst_y);
      rtg_oob_log_count++;
    }
    return;
  }
  if (!rtg_get_ptr_checked(rtg_address_adj[0], (uint16_t)dst_x, (uint16_t)dst_y, (uint16_t)width, (uint16_t)height,
                           pitch, rtg_format, "p2c_ex_dst", &dptr)) {
    return;
  }
  uint8_t draw_mode = minterm;

  uint8_t cur_bit, base_bit, base_byte;
  uint16_t cur_byte = 0, u8_fg = 0, u8_tmp = 0;

  cur_bit = base_bit = (0x80 >> (src_x % 8));
  cur_byte = base_byte = (uint8_t)((src_x / 8) % src_pitch);

  uint8_t* plane_ptr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint32_t plane_addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  for (int i = 0; i < bm->Depth; i++) {
    uint32_t plane_address = be32toh(bm->_p_Planes[i]);
    if (plane_address != 0 && plane_address != 0xFFFFFFFF) {
      plane_ptr[i] = get_mapped_data_pointer_by_address(cfg, be32toh(bm->_p_Planes[i]));
      if (!plane_ptr[i]) {
        plane_addr[i] = be32toh(bm->_p_Planes[i]);
        if (plane_addr[i] != 0)
          plane_addr[i] += (uint32_t)(src_y * src_pitch);
      } else {
        plane_ptr[i] += (uint32_t)(src_y * src_pitch);
      }
    } else {
      plane_addr[i] = plane_address;
    }
  }

  for (int16_t line_y = 0; line_y < height; line_y++) {
    for (int16_t x = dst_x; x < dst_x + width; x++) {
      u8_fg = 0;
      if (minterm & 0x01) {
        for (int i = 0; i < bm->Depth; i++) {
          if (plane_ptr[i]) {
            if (~plane_ptr[i][cur_byte] & cur_bit)
              u8_fg |= (1 << i);
          } else {
            if (plane_addr[i] == 0xFFFFFFFF)
              u8_fg |= (1 << i);
            else if (plane_addr[i] != 0) {
              u8_tmp = (uint8_t)ps_read_8(plane_addr[i] + cur_byte);
              if (~u8_tmp & cur_bit)
                u8_fg |= (1 << i);
            }
          }
        }
      } else {
        for (int i = 0; i < bm->Depth; i++) {
          if (plane_ptr[i]) {
            if (plane_ptr[i][cur_byte] & cur_bit)
              u8_fg |= (1 << i);
          } else {
            if (plane_addr[i] == 0xFFFFFFFF)
              u8_fg |= (1 << i);
            else if (plane_addr[i] != 0) {
              u8_tmp = (uint8_t)ps_read_8(plane_addr[i] + cur_byte);
              if (u8_tmp & cur_bit)
                u8_fg |= (1 << i);
            }
          }
        }
      }

      if (mask == 0xFF && (draw_mode == MINTERM_SRC || draw_mode == MINTERM_NOTSRC)) {
        dptr[x] = (uint8_t)u8_fg;
        goto skip;
      }

      HANDLE_MINTERM_PIXEL(u8_fg, dptr[(size_t)x], RTG_FMT_8BIT_CLUT);

    skip:;
      if ((cur_bit >>= 1) == 0) {
        cur_bit = 0x80;
        cur_byte++;
        cur_byte %= src_pitch;
      }
    }
    dptr += pitch;
    for (int i = 0; i < bm->Depth; i++) {
      if (plane_ptr[i])
        plane_ptr[i] += src_pitch;
      if (plane_addr[i] && plane_addr[i] != 0xFFFFFFFF)
        plane_addr[i] += src_pitch;
    }
    cur_bit = base_bit;
    cur_byte = base_byte;
  }
}

void rtg_p2c(int16_t src_x, int16_t src_y, int16_t dst_x, int16_t dst_y, int16_t width, int16_t height,
             uint8_t draw_mode, uint8_t planes, uint8_t mask, uint8_t layer_mask,
             uint16_t src_line_pitch, uint8_t* bmp_data_src) {
  uint16_t pitch = rtg_x[3];
  uint8_t* dptr = NULL;
  if (dst_x < 0 || dst_y < 0) {
    if (rtg_oob_log_count < 20) {
      LOG_DEBUG("[RTG/OOB] p2c invalid coords: dx=%d dy=%d\n", dst_x, dst_y);
      rtg_oob_log_count++;
    }
    return;
  }
  if (!rtg_get_ptr_checked(rtg_address_adj[0], (uint16_t)dst_x, (uint16_t)dst_y, (uint16_t)width, (uint16_t)height,
                           pitch, rtg_format, "p2c_dst", &dptr)) {
    return;
  }

  uint8_t cur_bit, base_bit, base_byte, cur_byte = 0;
  uint8_t u8_fg = 0;
  // uint32_t color_mask = 0xFFFFFFFF;

  uint32_t plane_size = (uint32_t)src_line_pitch * (uint32_t)height;
  uint8_t* bmp_data = bmp_data_src;

  cur_bit = base_bit = (0x80 >> (src_x % 8));
  cur_byte = base_byte = (uint8_t)((src_x / 8) % src_line_pitch);

  if (realtime_graphics_debug) {
    LOG_DEBUG("P2C: %d,%d - %d,%d (%dx%d) %d, %.2X\n", src_x, src_y, dst_x, dst_y, width, height, planes, layer_mask);
    LOG_DEBUG("Mask: %.2X Minterm: %.2X\n", mask, draw_mode);
    LOG_DEBUG("Pitch: %d Src Pitch: %d (!!!: %.4X)\n", pitch, src_line_pitch, rtg_user[0]);
    LOG_DEBUG("Curbyte: %d Curbit: %d\n", cur_byte, cur_bit);
    LOG_DEBUG("Plane size: %d Total size: %d (%X)\n", plane_size, plane_size * planes,
              plane_size * planes);
    LOG_DEBUG("Source: %.8X - %.8X\n", rtg_address[1], rtg_address_adj[1]);
    LOG_DEBUG("Target: %.8X - %.8X\n", rtg_address[0], rtg_address_adj[0]);

    LOG_DEBUG("Grabbing data from RTG memory.\nData:\n");
    for (int i = 0; i < height; i++) {
      for (int k = 0; k < planes; k++) {
        for (int j = 0; j < src_line_pitch; j++) {
          LOG_DEBUG("%.2X", (uint8_t)bmp_data_src[(uint32_t)j + ((uint32_t)i * (uint32_t)src_line_pitch) + (plane_size * (uint32_t)k)]);
        }
        LOG_DEBUG("  ");
      }
      LOG_DEBUG("\n");
    }
  }

  for (int16_t line_y = 0; line_y < height; line_y++) {
    for (int16_t x = dst_x; x < dst_x + width; x++) {
      u8_fg = 0;
      if (draw_mode & 0x01) {
        DECODE_INVERTED_PLANAR_PIXEL(u8_fg)
      } else {
        DECODE_PLANAR_PIXEL(u8_fg)
      }

      if (mask == 0xFF && (draw_mode == MINTERM_SRC || draw_mode == MINTERM_NOTSRC)) {
        dptr[x] = (uint8_t)u8_fg;
        goto skip;
      }

      HANDLE_MINTERM_PIXEL(u8_fg, dptr[(size_t)x], rtg_format);

    skip:;
      if ((cur_bit >>= 1) == 0) {
        cur_bit = 0x80;
        cur_byte++;
        cur_byte = (uint8_t)(cur_byte % src_line_pitch);
      }
    }
    dptr += pitch;
    if ((((int16_t)(line_y + src_y + 1)) % (int16_t)height) != 0)
      bmp_data += src_line_pitch;
    else
      bmp_data = bmp_data_src;
    cur_bit = base_bit;
    cur_byte = base_byte;
  }
}

void rtg_p2d(int16_t src_x, int16_t src_y, int16_t dst_x, int16_t dst_y, int16_t width, int16_t height,
             uint8_t draw_mode, uint8_t planes, uint8_t mask, uint8_t layer_mask,
             uint16_t src_line_pitch, uint8_t* bmp_data_src) {
  uint16_t pitch = rtg_x[3];
  uint8_t* dptr = NULL;
  if (dst_x < 0 || dst_y < 0) {
    if (rtg_oob_log_count < 20) {
      LOG_DEBUG("[RTG/OOB] p2d invalid coords: dx=%d dy=%d\n", dst_x, dst_y);
      rtg_oob_log_count++;
    }
    return;
  }
  if (!rtg_get_ptr_checked(rtg_address_adj[0], (uint16_t)dst_x, (uint16_t)dst_y, (uint16_t)width, (uint16_t)height,
                           pitch, rtg_format, "p2d_dst", &dptr)) {
    return;
  }

  uint8_t cur_bit      = 0;
  uint8_t base_bit     = 0;
  uint8_t base_byte    = 0;
  uint8_t cur_byte     = 0;
  uint8_t u8_fg        = 0;

  // uint32_t color_mask = 0xFFFFFFFF;

  uint32_t plane_size = (uint32_t)src_line_pitch * (uint32_t)height;
  uint8_t* bmp_data = bmp_data_src;

  cur_bit = base_bit = (0x80 >> (src_x % 8));
  cur_byte = base_byte = (uint8_t)((src_x / 8) % src_line_pitch);

  if (realtime_graphics_debug) {
    LOG_DEBUG("P2D: %d,%d - %d,%d (%dx%d) %d, %.2X\n", src_x, src_y, dst_x, dst_y, width, height, planes, layer_mask);
    LOG_DEBUG("Mask: %.2X Minterm: %.2X\n", mask, draw_mode);
    LOG_DEBUG("Pitch: %d Src Pitch: %d (!!!: %.4X)\n", pitch, src_line_pitch, rtg_user[0]);
    LOG_DEBUG("Curbyte: %d Curbit: %d\n", cur_byte, cur_bit);
    LOG_DEBUG("Plane size: %d Total size: %d (%X)\n", plane_size, plane_size * planes,
              plane_size * planes);
    LOG_DEBUG("Source: %.8X - %.8X\n", rtg_address[1], rtg_address_adj[1]);
    LOG_DEBUG("Target: %.8X - %.8X\n", rtg_address[0], rtg_address_adj[0]);

    LOG_DEBUG("Grabbing data from RTG memory.\nData:\n");
    for (int i = 0; i < height; i++) {
      for (int k = 0; k < planes; k++) {
        for (int j = 0; j < src_line_pitch; j++) {
          LOG_DEBUG("%.2X", (uint8_t)bmp_data_src[(uint32_t)j + ((uint32_t)i * (uint32_t)src_line_pitch) + (plane_size * (uint32_t)k)]);
        }
        LOG_DEBUG("  ");
      }
      LOG_DEBUG("\n");
    }
  }

  uint32_t clut_array[256];
  for (int i = 0; i < 256; i++) {
    uint32_t temp_val;
    memcpy(&temp_val, &bmp_data_src[(size_t)i * sizeof(uint32_t)], sizeof(uint32_t));
    clut_array[i] = be32toh(temp_val);
  }
  uint32_t* clut = clut_array;
  bmp_data += (256 * 4);
  bmp_data_src += (256 * 4);

  for (int16_t line_y = 0; line_y < height; line_y++) {
    for (int16_t x = dst_x; x < dst_x + width; x++) {
      u8_fg = 0;
      if (draw_mode & 0x01) {
        DECODE_INVERTED_PLANAR_PIXEL(u8_fg)
      } else {
        DECODE_PLANAR_PIXEL(u8_fg)
      }

      uint32_t fg_color = clut[u8_fg];

      if (mask == 0xFF && (draw_mode == MINTERM_SRC || draw_mode == MINTERM_NOTSRC)) {
        switch (rtg_format) {
        case RTG_FMT_8BIT_CLUT:
          dptr[(size_t)x] = u8_fg;
          break;
        case RTG_FMT_RGB565_LE:
        case RTG_FMT_RGB565_BE:
        case RTG_FMT_BGR565_LE:
        case RTG_FMT_RGB555_LE:
        case RTG_FMT_RGB555_BE:
        case RTG_FMT_BGR555_LE:
          {
            uint16_t color16 = (fg_color >> 16);
            store_u16_be(&dptr[(size_t)x * sizeof(uint16_t)], color16);
          }
          break;
        case RTG_FMT_RGB32_ABGR:
        case RTG_FMT_RGB32_ARGB:
        case RTG_FMT_RGB32_BGRA:
        case RTG_FMT_RGB32_RGBA:
          store_u32_be(&dptr[(size_t)x * sizeof(uint32_t)], fg_color);
          break;
        case RTG_FMT_RGB24:
        case RTG_FMT_BGR24:
          rtg_store_pixel(&dptr[(size_t)x * rtg_pixel_size[rtg_format]], rtg_format, fg_color);
          break;
        }
        goto skip;
      }

    skip:;
      if ((cur_bit >>= 1) == 0) {
        cur_bit = 0x80;
        cur_byte++;
        cur_byte = (uint8_t)(cur_byte % src_line_pitch);
      }
    }
    dptr += pitch;
    if ((((int16_t)(line_y + src_y + 1)) % (int16_t)height) != 0)
      bmp_data += src_line_pitch;
    else
      bmp_data = bmp_data_src;
    cur_bit = base_bit;
    cur_byte = base_byte;
  }
}
