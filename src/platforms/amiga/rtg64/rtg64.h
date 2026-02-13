// SPDX-License-Identifier: MIT
#ifndef PISTORM_RTG64_H
#define PISTORM_RTG64_H

#include <stddef.h>
#include <stdint.h>

#include "rtg64-protocol.h"

typedef struct rtg64_mode {
  uint16_t width;
  uint16_t height;
  uint16_t format;
  uint16_t reserved;
  uint32_t stride;
} rtg64_mode_t;

int rtg64_init(void);
void rtg64_shutdown(void);
void rtg64_reset(void);

int rtg64_set_mode(const rtg64_mode_t *mode);
int rtg64_alloc_framebuffer(uint32_t bytes);
void rtg64_free_framebuffer(void);

int rtg64_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
int rtg64_blit_rect(uint16_t sx, uint16_t sy, uint16_t dx, uint16_t dy, uint16_t w, uint16_t h);
int rtg64_set_palette_entry(uint16_t index, uint32_t argb);

int rtg64_present(void);
int rtg64_run_smoketest(void);

uint32_t rtg64_mmio_read(uint32_t offset, uint8_t access_size);
void rtg64_mmio_write(uint32_t offset, uint32_t value, uint8_t access_size);

uint8_t *rtg64_get_framebuffer(uint32_t *bytes_out, rtg64_mode_t *mode_out);

#endif
