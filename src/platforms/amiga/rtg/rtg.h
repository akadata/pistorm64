// SPDX-License-Identifier: MIT

#define PIGFX_RTG_BASE 0x70000000
#define PIGFX_REG_SIZE 0x00010000
#define PIGFX_RTG_SIZE 0x02000000
#define PIGFX_SCRATCH_SIZE 0x00800000
#define PIGFX_SCRATCH_AREA 0x72010000
#define PIGFX_UPPER 0x72810000

#define CARD_OFFSET 0

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "rtg_enums.h"

struct emulator_config;

static inline uint8_t* rtg_pixel_at(uint8_t *base, size_t index, uint16_t format) {
  return base + ((size_t)index * rtg_pixel_size[format]);
}

static inline uint32_t rtg_load_pixel(const uint8_t *dest, uint16_t format) {
  switch (format) {
  case RTGFMT_8BIT_CLUT: {
    return *dest;
  }
  case RTGFMT_RGB565_LE:
  case RTGFMT_RGB565_BE:
  case RTGFMT_BGR565_LE:
  case RTGFMT_RGB555_LE:
  case RTGFMT_RGB555_BE:
  case RTGFMT_BGR555_LE: {
    uint16_t tmp;
    memcpy(&tmp, dest, sizeof tmp);
    return tmp;
  }
  case RTGFMT_RGB32_ABGR:
  case RTGFMT_RGB32_ARGB:
  case RTGFMT_RGB32_BGRA:
  case RTGFMT_RGB32_RGBA: {
    uint32_t tmp;
    memcpy(&tmp, dest, sizeof tmp);
    return tmp;
  }
  default: {
    return *dest;
  }
  }
}

static inline void rtg_store_pixel(uint8_t *dest, uint16_t format, uint32_t value) {
  switch (format) {
  case RTGFMT_8BIT_CLUT: {
    uint8_t tmp = (uint8_t)value;
    memcpy(dest, &tmp, sizeof tmp);
    break;
  }
  case RTGFMT_RGB565_LE:
  case RTGFMT_RGB565_BE:
  case RTGFMT_BGR565_LE:
  case RTGFMT_RGB555_LE:
  case RTGFMT_RGB555_BE:
  case RTGFMT_BGR555_LE: {
    uint16_t tmp = (uint16_t)value;
    memcpy(dest, &tmp, sizeof tmp);
    break;
  }
  case RTGFMT_RGB32_ABGR:
  case RTGFMT_RGB32_ARGB:
  case RTGFMT_RGB32_BGRA:
  case RTGFMT_RGB32_RGBA: {
    uint32_t tmp = value;
    memcpy(dest, &tmp, sizeof tmp);
    break;
  }
  default: {
    uint8_t tmp = (uint8_t)value;
    memcpy(dest, &tmp, sizeof tmp);
    break;
  }
  }
}

static inline void rtg_store_pixel_mask(uint8_t *dest, uint16_t format, uint32_t value,
                                        uint32_t mask) {
  if (format == RTGFMT_8BIT_CLUT) {
    uint8_t current = *dest;
    uint8_t tmp = (uint8_t)value ^ (uint8_t)(current & ~mask);
    memcpy(dest, &tmp, sizeof tmp);
    return;
  }
  rtg_store_pixel(dest, format, value);
}

static inline void rtg_invert_pixel(uint8_t *dest, uint16_t format, uint32_t mask) {
  switch (format) {
  case RTGFMT_8BIT_CLUT: {
    uint8_t tmp = *dest ^ (uint8_t)mask;
    memcpy(dest, &tmp, sizeof tmp);
    break;
  }
  default: {
    uint32_t current = rtg_load_pixel(dest, format);
    rtg_store_pixel(dest, format, ~current);
    break;
  }
  }
}

void rtg_write(uint32_t address, uint32_t value, uint8_t mode);
unsigned int rtg_read(uint32_t address, uint8_t mode);
void rtg_set_clut_entry(uint8_t index, uint32_t xrgb);
void rtg_init_display(void);
void rtg_shutdown_display(void);
void rtg_enable_mouse_cursor(uint8_t enable);

unsigned int rtg_get_fb(void);
void rtg_set_mouse_cursor_pos(int16_t x, int16_t y);
void rtg_set_cursor_clut_entry(uint8_t r, uint8_t g, uint8_t b, uint8_t idx);
void rtg_set_mouse_cursor_image(uint8_t* src, uint8_t w, uint8_t h);

void rtg_show_fps(uint8_t enable);
void rtg_set_scale_mode(uint16_t scale_mode);
uint16_t rtg_get_scale_mode(void);
void rtg_set_scale_rect(uint16_t scale_mode, int16_t x1, int16_t y1, int16_t x2, int16_t y2);
void rtg_set_scale_filter(uint16_t _filter_mode);
void rtg_set_screen_width(uint32_t width);
void rtg_set_screen_height(uint32_t height);
void rtg_show_clut_cursor(uint8_t show);
void rtg_set_clut_cursor(uint8_t* bmp, uint32_t* pal, int16_t offs_x, int16_t offs_y, uint16_t w,
                         uint16_t h, uint8_t mask_color);
uint16_t rtg_get_scale_filter(void);
void rtg_palette_debug(uint8_t enable);

int init_rtg_data(struct emulator_config* cfg);
void shutdown_rtg(void);

void rtg_fillrect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color, uint16_t pitch,
                  uint16_t format, uint8_t mask);
void rtg_fillrect_solid(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color,
                        uint16_t pitch, uint16_t format);
void rtg_invertrect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t pitch, uint16_t format,
                    uint8_t mask);
void rtg_blitrect(uint16_t x, uint16_t y, uint16_t dx, uint16_t dy, uint16_t w, uint16_t h,
                  uint16_t pitch, uint16_t format, uint8_t mask);
void rtg_blitrect_solid(uint16_t x, uint16_t y, uint16_t dx, uint16_t dy, uint16_t w, uint16_t h,
                        uint16_t pitch, uint16_t format);
void rtg_blitrect_nomask_complete(uint16_t sx, uint16_t sy, uint16_t dx, uint16_t dy, uint16_t w,
                                  uint16_t h, uint16_t srcpitch, uint16_t dstpitch,
                                  uint32_t src_addr, uint32_t dst_addr, uint16_t format,
                                  uint8_t minterm);
void rtg_blittemplate(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t src_addr,
                      uint32_t fgcol, uint32_t bgcol, uint16_t pitch, uint16_t t_pitch,
                      uint16_t format, uint16_t offset_x, uint8_t mask, uint8_t draw_mode);
void rtg_blitpattern(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t src_addr,
                     uint32_t fgcol, uint32_t bgcol, uint16_t pitch, uint16_t format,
                     uint16_t offset_x, uint16_t offset_y, uint8_t mask, uint8_t draw_mode,
                     uint8_t loop_rows);
void rtg_drawline_solid(int16_t x1_, int16_t y1_, int16_t x2_, int16_t y2_, uint16_t len,
                        uint32_t fgcol, uint16_t pitch, uint16_t format);
void rtg_drawline(int16_t x1_, int16_t y1_, int16_t x2_, int16_t y2_, uint16_t len,
                  uint16_t pattern, uint16_t pattern_offset, uint32_t fgcol, uint32_t bgcol,
                  uint16_t pitch, uint16_t format, uint8_t mask, uint8_t draw_mode);

void rtg_p2c(int16_t sx, int16_t sy, int16_t dx, int16_t dy, int16_t w, int16_t h,
             uint8_t draw_mode, uint8_t planes, uint8_t mask, uint8_t layer_mask,
             uint16_t src_line_pitch, uint8_t* bmp_data_src);
void rtg_p2d(int16_t sx, int16_t sy, int16_t dx, int16_t dy, int16_t w, int16_t h,
             uint8_t draw_mode, uint8_t planes, uint8_t mask, uint8_t layer_mask,
             uint16_t src_line_pitch, uint8_t* bmp_data_src);
struct BitMap;
void rtg_p2c_ex(int16_t sx, int16_t sy, int16_t dx, int16_t dy, int16_t w, int16_t h,
                uint8_t minterm, struct BitMap* bm, uint8_t mask, uint16_t dst_pitch,
                uint16_t src_pitch);

#define PATTERN_LOOPX                                                                              \
  if (sptr) {                                                                                      \
    cur_byte = (uint8_t)sptr[tmpl_x];                                                              \
  } else {                                                                                         \
    cur_byte = (uint8_t)m68k_read_memory_8(src_addr + tmpl_x);                                     \
  }                                                                                                \
  if (invert) {                                                                                    \
    cur_byte ^= 0xFF;                                                                              \
  }                                                                                                \
  tmpl_x ^= 0x01;

#define PATTERN_LOOPY                                                                              \
  if (sptr) {                                                                                      \
    sptr += 2;                                                                                     \
  }                                                                                                \
  src_addr += 2;                                                                                   \
  if ((ys + offset_y + 1) % loop_rows == 0) {                                                      \
    if (sptr)                                                                                      \
      sptr = sptr_base;                                                                            \
    src_addr = src_addr_base;                                                                      \
  }                                                                                                \
  tmpl_x = (offset_x / 8) % 2;                                                                     \
  cur_bit = base_bit;                                                                              \
  dptr += pitch;

#define TEMPLATE_LOOPX                                                                             \
  if (sptr) {                                                                                      \
    cur_byte = (uint8_t)sptr[tmpl_x];                                                              \
  } else {                                                                                         \
    cur_byte = (uint8_t)m68k_read_memory_8(src_addr + tmpl_x);                                     \
  }                                                                                                \
  if (invert) {                                                                                    \
    cur_byte ^= 0xFF;                                                                              \
  }                                                                                                \
  tmpl_x++;

#define TEMPLATE_LOOPY                                                                             \
  if (sptr)                                                                                        \
    sptr += t_pitch;                                                                               \
  src_addr += t_pitch;                                                                             \
  dptr += pitch;                                                                                   \
  tmpl_x = offset_x / 8;                                                                           \
  cur_bit = base_bit;

#define INVERT_RTG_PIXELS(dest, format)                                                            \
  for (int __rtg_i = 0; __rtg_i < 8; ++__rtg_i) {                                                   \
    if (cur_byte & (0x80 >> __rtg_i)) {                                                             \
      rtg_invert_pixel(rtg_pixel_at((uint8_t *)(dest), (size_t)__rtg_i, format), format, mask);      \
    }                                                                                               \
  }

#define SET_RTG_PIXELS_MASK(dest, src, format)                                                     \
  do {                                                                                              \
    uint8_t* __rtg_dest = (uint8_t*)(dest);                                                         \
    for (int __rtg_i = 0; __rtg_i < 8; ++__rtg_i) {                                                 \
      if (cur_byte & (0x80 >> __rtg_i)) {                                                           \
        SET_RTG_PIXEL_MASK(__rtg_dest + ((size_t)__rtg_i) * rtg_pixel_size[format], src, format);  \
      }                                                                                             \
    }                                                                                               \
  } while (0)

#define SET_RTG_PIXELS2_COND_MASK(dest, src, src2, format)                                         \
  do {                                                                                              \
    uint8_t* __rtg_dest = (uint8_t*)(dest);                                                         \
    for (int __rtg_i = 0; __rtg_i < 8; ++__rtg_i) {                                                 \
      uint8_t* __rtg_ptr = __rtg_dest + ((size_t)__rtg_i) * rtg_pixel_size[format];                \
      if (cur_byte & (0x80 >> __rtg_i)) {                                                           \
        SET_RTG_PIXEL(__rtg_ptr, src, format);                                                      \
      } else {                                                                                      \
        SET_RTG_PIXEL_MASK(__rtg_ptr, src2, format);                                                \
      }                                                                                             \
    }                                                                                               \
  } while (0)

#define SET_RTG_PIXELS(dest, src, format)                                                          \
  do {                                                                                              \
    uint8_t* __rtg_dest = (uint8_t*)(dest);                                                         \
    for (int __rtg_i = 0; __rtg_i < 8; ++__rtg_i) {                                                 \
      if (cur_byte & (0x80 >> __rtg_i)) {                                                           \
        SET_RTG_PIXEL(__rtg_dest + ((size_t)__rtg_i) * rtg_pixel_size[format], src, format);        \
      }                                                                                             \
    }                                                                                               \
  } while (0)

#define SET_RTG_PIXELS2_COND(dest, src, src2, format)                                              \
  do {                                                                                              \
    uint8_t* __rtg_dest = (uint8_t*)(dest);                                                         \
    for (int __rtg_i = 0; __rtg_i < 8; ++__rtg_i) {                                                 \
      uint8_t* __rtg_ptr = __rtg_dest + ((size_t)__rtg_i) * rtg_pixel_size[format];                \
      if (cur_byte & (0x80 >> __rtg_i)) {                                                           \
        SET_RTG_PIXEL(__rtg_ptr, src, format);                                                      \
      } else {                                                                                      \
        SET_RTG_PIXEL(__rtg_ptr, src2, format);                                                     \
      }                                                                                             \
    }                                                                                               \
  } while (0)

#define SET_RTG_PIXEL(dest, src, format)                                                           \
  rtg_store_pixel((uint8_t *)(dest), (uint16_t)(format), (uint32_t)(src));

#define SET_RTG_PIXEL_MASK(dest, src, format)                                                      \
  rtg_store_pixel_mask((uint8_t *)(dest), (uint16_t)(format), (uint32_t)(src), mask);

#define INVERT_RTG_PIXEL(dest, format)                                                             \
  rtg_invert_pixel((uint8_t *)(dest), (uint16_t)(format), mask);

#define HANDLE_MINTERM_PIXEL(s, d, f)                                                              \
  switch (draw_mode) {                                                                             \
  case MINTERM_NOR:                                                                                \
    s &= ~(d);                                                                                     \
    SET_RTG_PIXEL_MASK(&d, s, f);                                                                  \
    break;                                                                                         \
  case MINTERM_ONLYDST:                                                                            \
    d = d & ~(s);                                                                                  \
    break;                                                                                         \
  case MINTERM_NOTSRC:                                                                             \
    SET_RTG_PIXEL_MASK(&d, s, f);                                                                  \
    break;                                                                                         \
  case MINTERM_ONLYSRC:                                                                            \
    s &= (d ^ 0xFF);                                                                               \
    SET_RTG_PIXEL_MASK(&d, s, f);                                                                  \
    break;                                                                                         \
  case MINTERM_INVERT:                                                                             \
    d ^= 0xFF;                                                                                     \
    break;                                                                                         \
  case MINTERM_EOR:                                                                                \
    d ^= s;                                                                                        \
    break;                                                                                         \
  case MINTERM_NAND:                                                                               \
    s = ~(d & ~(s)) & mask;                                                                        \
    SET_RTG_PIXEL_MASK(&d, s, f);                                                                  \
    break;                                                                                         \
  case MINTERM_AND:                                                                                \
    s &= d;                                                                                        \
    SET_RTG_PIXEL_MASK(&d, s, f);                                                                  \
    break;                                                                                         \
  case MINTERM_NEOR:                                                                               \
    d ^= (s & mask);                                                                               \
    break;                                                                                         \
  case MINTERM_DST: /* This one does nothing. */                                                   \
    return;                                                                                        \
    break;                                                                                         \
  case MINTERM_NOTONLYSRC:                                                                         \
    d |= (s & mask);                                                                               \
    break;                                                                                         \
  case MINTERM_SRC:                                                                                \
    SET_RTG_PIXEL_MASK(&d, s, f);                                                                  \
    break;                                                                                         \
  case MINTERM_NOTONLYDST:                                                                         \
    s = ~(d & s) & mask;                                                                           \
    SET_RTG_PIXEL_MASK(&d, s, f);                                                                  \
    break;                                                                                         \
  case MINTERM_OR:                                                                                 \
    d |= (s & mask);                                                                               \
    break;                                                                                         \
  }

#define DECODE_PLANAR_PIXEL(a)                                                                     \
  switch (planes) {                                                                                \
  case 8:                                                                                          \
    if (layer_mask & 0x80 && bmp_data[(plane_size * 7) + cur_byte] & cur_bit)                      \
      a |= 0x80;                                                                                   \
    /* Fallthrough */                                                                              \
  case 7:                                                                                          \
    if (layer_mask & 0x40 && bmp_data[(plane_size * 6) + cur_byte] & cur_bit)                      \
      a |= 0x40;                                                                                   \
    /* Fallthrough */                                                                              \
  case 6:                                                                                          \
    if (layer_mask & 0x20 && bmp_data[(plane_size * 5) + cur_byte] & cur_bit)                      \
      a |= 0x20;                                                                                   \
    /* Fallthrough */                                                                              \
  case 5:                                                                                          \
    if (layer_mask & 0x10 && bmp_data[(plane_size * 4) + cur_byte] & cur_bit)                      \
      a |= 0x10;                                                                                   \
    /* Fallthrough */                                                                              \
  case 4:                                                                                          \
    if (layer_mask & 0x08 && bmp_data[(plane_size * 3) + cur_byte] & cur_bit)                      \
      a |= 0x08;                                                                                   \
    /* Fallthrough */                                                                              \
  case 3:                                                                                          \
    if (layer_mask & 0x04 && bmp_data[(plane_size * 2) + cur_byte] & cur_bit)                      \
      a |= 0x04;                                                                                   \
    /* Fallthrough */                                                                              \
  case 2:                                                                                          \
    if (layer_mask & 0x02 && bmp_data[plane_size + cur_byte] & cur_bit)                            \
      a |= 0x02;                                                                                   \
    /* Fallthrough */                                                                              \
  case 1:                                                                                          \
    if (layer_mask & 0x01 && bmp_data[cur_byte] & cur_bit)                                         \
      a |= 0x01;                                                                                   \
    break;                                                                                         \
  }

#define DECODE_INVERTED_PLANAR_PIXEL(a)                                                            \
  switch (planes) {                                                                                \
  case 8:                                                                                          \
    if (layer_mask & 0x80 && (bmp_data[(plane_size * 7) + cur_byte] ^ 0xFF) & cur_bit)             \
      a |= 0x80;                                                                                   \
    /* Fallthrough */                                                                              \
  case 7:                                                                                          \
    if (layer_mask & 0x40 && (bmp_data[(plane_size * 6) + cur_byte] ^ 0xFF) & cur_bit)             \
      a |= 0x40;                                                                                   \
    /* Fallthrough */                                                                              \
  case 6:                                                                                          \
    if (layer_mask & 0x20 && (bmp_data[(plane_size * 5) + cur_byte] ^ 0xFF) & cur_bit)             \
      a |= 0x20;                                                                                   \
    /* Fallthrough */                                                                              \
  case 5:                                                                                          \
    if (layer_mask & 0x10 && (bmp_data[(plane_size * 4) + cur_byte] ^ 0xFF) & cur_bit)             \
      a |= 0x10;                                                                                   \
    /* Fallthrough */                                                                              \
  case 4:                                                                                          \
    if (layer_mask & 0x08 && (bmp_data[(plane_size * 3) + cur_byte] ^ 0xFF) & cur_bit)             \
      a |= 0x08;                                                                                   \
    /* Fallthrough */                                                                              \
  case 3:                                                                                          \
    if (layer_mask & 0x04 && (bmp_data[(plane_size * 2) + cur_byte] ^ 0xFF) & cur_bit)             \
      a |= 0x04;                                                                                   \
    /* Fallthrough */                                                                              \
  case 2:                                                                                          \
    if (layer_mask & 0x02 && (bmp_data[plane_size + cur_byte] ^ 0xFF) & cur_bit)                   \
      a |= 0x02;                                                                                   \
    /* Fallthrough */                                                                              \
  case 1:                                                                                          \
    if (layer_mask & 0x01 && (bmp_data[cur_byte] ^ 0xFF) & cur_bit)                                \
      a |= 0x01;                                                                                   \
    break;                                                                                         \
  }
