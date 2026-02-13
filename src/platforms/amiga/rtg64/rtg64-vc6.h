// SPDX-License-Identifier: MIT
#ifndef PISTORM_RTG64_VC6_H
#define PISTORM_RTG64_VC6_H

#include <stdint.h>

#include "rtg64.h"

typedef struct rtg64_vc6_backend {
  uint32_t mailbox_base;
  uint16_t display_width;
  uint16_t display_height;
  uint16_t active_width;
  uint16_t active_height;
  uint16_t active_format;
  uint16_t reserved;
  uint32_t active_stride;
  uint32_t framebuffer_bytes;
  uint64_t frame_counter;
  uint32_t feature_flags;
  uint8_t initialized;
  uint8_t mode_set;
  uint8_t framebuffer_attached;
  uint8_t reserved_flags;
} rtg64_vc6_backend_t;

int rtg64_vc6_init(rtg64_vc6_backend_t *vc6);
void rtg64_vc6_shutdown(rtg64_vc6_backend_t *vc6);
int rtg64_vc6_query_display(rtg64_vc6_backend_t *vc6, uint16_t *width, uint16_t *height);

int rtg64_vc6_set_mode(rtg64_vc6_backend_t *vc6, const rtg64_mode_t *mode);
int rtg64_vc6_attach_framebuffer(rtg64_vc6_backend_t *vc6, uint8_t *fb, uint32_t fb_bytes);
int rtg64_vc6_present(rtg64_vc6_backend_t *vc6, const uint8_t *fb, uint32_t fb_bytes);
int rtg64_vc6_set_palette_entry(rtg64_vc6_backend_t *vc6, uint16_t index, uint32_t argb);

#endif
