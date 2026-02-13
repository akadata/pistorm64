// SPDX-License-Identifier: MIT
#ifndef PISTORM_RTG64_BENCH_H
#define PISTORM_RTG64_BENCH_H

#include <stdint.h>

void rtg64_bench_pattern32(uint8_t *fb,
                           uint32_t width,
                           uint32_t height,
                           uint32_t stride,
                           uint32_t frame_seed);
void rtg64_bench_scroll32(uint8_t *fb,
                          uint32_t width,
                          uint32_t height,
                          uint32_t stride,
                          uint32_t scroll_px);

#endif
