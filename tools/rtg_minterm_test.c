// SPDX-License-Identifier: MIT

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "platforms/amiga/rtg/rtg.h"

static void check_clut_masked_src(void) {
  bool no_write = false;
  uint32_t out = rtg_apply_minterm_pixel(MINTERM_SRC, RTGFMT_8BIT_CLUT, 0x5Au, 0xA5u, 0x0Fu,
                                         &no_write);
  assert(!no_write);
  assert(out == 0xFAu);
}

static void check_16bit_width_and_invert(void) {
  bool no_write = false;
  uint32_t out = rtg_apply_minterm_pixel(MINTERM_INVERT, RTGFMT_RGB565_BE, 0x0000u, 0x1234u,
                                         0x00FFu, &no_write);
  assert(!no_write);
  assert(out == 0xEDCBu);

  out = rtg_apply_minterm_pixel(MINTERM_SRC, RTGFMT_RGB565_BE, 0xBEEFu, 0x1234u, 0x000Fu,
                                &no_write);
  assert(!no_write);
  assert(out == 0xBEEFu);
}

static void check_32bit_width_and_logic(void) {
  bool no_write = false;
  uint32_t out = rtg_apply_minterm_pixel(MINTERM_EOR, RTGFMT_RGB32_ARGB, 0xAA00AA00u,
                                         0x0F0F0F0Fu, 0x000000FFu, &no_write);
  assert(!no_write);
  assert(out == 0xA50FA50Fu);

  out = rtg_apply_minterm_pixel(MINTERM_ONLYSRC, RTGFMT_RGB32_ARGB, 0x0F0F0F0Fu, 0xF0F0FFFFu,
                                0xFFFFFFFFu, &no_write);
  assert(!no_write);
  assert(out == (0x0F0F0F0Fu & (0xF0F0FFFFu ^ 0xFFFFFFFFu)));
}

static void check_dst_no_write(void) {
  bool no_write = false;
  uint32_t out = rtg_apply_minterm_pixel(MINTERM_DST, RTGFMT_RGB32_BGRA, 0x12345678u,
                                         0xCAFEBABEu, 0xFFFFFFFFu, &no_write);
  assert(no_write);
  assert(out == 0xCAFEBABEu);
}

int main(void) {
  check_clut_masked_src();
  check_16bit_width_and_invert();
  check_32bit_width_and_logic();
  check_dst_no_write();
  return 0;
}
