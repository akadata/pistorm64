// SPDX-License-Identifier: MIT
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rtg64-bench.h"

static inline uint32_t make_color(uint32_t x, uint32_t y, uint32_t seed) {
  const uint32_t r = (x + seed) & 0xFFu;
  const uint32_t g = (y + (seed << 1)) & 0xFFu;
  const uint32_t b = ((x ^ y) + (seed << 2)) & 0xFFu;
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void rtg64_bench_pattern32(uint8_t *fb,
                           uint32_t width,
                           uint32_t height,
                           uint32_t stride,
                           uint32_t frame_seed) {
  if (!fb || width == 0 || height == 0 || stride < width * 4u) {
    return;
  }

  for (uint32_t y = 0; y < height; y++) {
    uint32_t *row = (uint32_t *)(void *)(fb + ((size_t)y * stride));
    for (uint32_t x = 0; x < width; x++) {
      row[x] = make_color(x, y, frame_seed);
    }
  }
}

void rtg64_bench_scroll32(uint8_t *fb,
                          uint32_t width,
                          uint32_t height,
                          uint32_t stride,
                          uint32_t scroll_px) {
  if (!fb || width == 0 || height == 0 || stride < width * 4u) {
    return;
  }

  const uint32_t shift = scroll_px % width;
  if (shift == 0) {
    return;
  }

  const uint32_t copy_pixels = width - shift;
  const size_t copy_bytes = (size_t)copy_pixels * sizeof(uint32_t);
  for (uint32_t y = 0; y < height; y++) {
    uint32_t *row = (uint32_t *)(void *)(fb + ((size_t)y * stride));
    memmove(row + shift, row, copy_bytes);
    for (uint32_t x = 0; x < shift; x++) {
      row[x] = 0xFF101010u;
    }
  }
}
