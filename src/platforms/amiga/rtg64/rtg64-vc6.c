// SPDX-License-Identifier: MIT
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "rtg64-vc6.h"

/*
 * This backend is intentionally conservative: it establishes VC6-facing
 * state and flow (mode -> framebuffer -> present) without binding to any
 * bare-metal register map from Emu68. VC6 transport submission will be
 * filled in later via a Linux-safe display path.
 */

#define RTG64_VC6_FEATURE_MAILBOX_TAGS (1u << 0)
#define RTG64_VC6_FEATURE_HVS_PLANE    (1u << 1)
#define RTG64_VC6_FEATURE_PALETTE      (1u << 2)
#define RTG64_VC6_FEATURE_PRESENT      (1u << 3)

/* Standard firmware mailbox property tags (reference-only for future use). */
enum rtg64_vc6_mbox_tag {
  RTG64_VC6_TAG_GET_DISPLAY_SIZE = 0x00040003,
  RTG64_VC6_TAG_SET_PHYS_SIZE = 0x00048003,
  RTG64_VC6_TAG_SET_VIRT_SIZE = 0x00048004,
  RTG64_VC6_TAG_SET_DEPTH = 0x00048005,
  RTG64_VC6_TAG_ALLOC_FB = 0x00040001,
  RTG64_VC6_TAG_GET_PITCH = 0x00040008,
};

static uint16_t env_u16_or_default(const char *name, uint16_t fallback) {
  const char *value = getenv(name);
  if (!value || !*value) {
    return fallback;
  }

  char *end = NULL;
  unsigned long parsed = strtoul(value, &end, 10);
  if (end == value || parsed == 0 || parsed > 65535ul) {
    return fallback;
  }

  return (uint16_t)parsed;
}

int rtg64_vc6_init(rtg64_vc6_backend_t *vc6) {
  if (!vc6) {
    return -1;
  }

  memset(vc6, 0, sizeof(*vc6));
  vc6->display_width = env_u16_or_default("RTG64_VC6_WIDTH", 1920);
  vc6->display_height = env_u16_or_default("RTG64_VC6_HEIGHT", 1080);
  vc6->feature_flags = RTG64_VC6_FEATURE_MAILBOX_TAGS |
                       RTG64_VC6_FEATURE_HVS_PLANE |
                       RTG64_VC6_FEATURE_PALETTE |
                       RTG64_VC6_FEATURE_PRESENT;
  vc6->initialized = 1;

  LOG_INFO("[RTG64][VC6] Backend initialized (display=%ux%u).\n",
           vc6->display_width, vc6->display_height);
  return 0;
}

void rtg64_vc6_shutdown(rtg64_vc6_backend_t *vc6) {
  if (!vc6) {
    return;
  }
  memset(vc6, 0, sizeof(*vc6));
}

int rtg64_vc6_query_display(rtg64_vc6_backend_t *vc6, uint16_t *width, uint16_t *height) {
  if (!vc6 || !vc6->initialized) {
    return -1;
  }

  if (width) {
    *width = vc6->display_width;
  }
  if (height) {
    *height = vc6->display_height;
  }
  return 0;
}

int rtg64_vc6_set_mode(rtg64_vc6_backend_t *vc6, const rtg64_mode_t *mode) {
  if (!vc6 || !vc6->initialized || !mode) {
    return -1;
  }

  vc6->active_width = mode->width;
  vc6->active_height = mode->height;
  vc6->active_format = mode->format;
  vc6->active_stride = mode->stride;
  vc6->mode_set = 1;

  LOG_DEBUG("[RTG64][VC6] Mode set %ux%u fmt=%u stride=%u.\n",
            vc6->active_width,
            vc6->active_height,
            vc6->active_format,
            vc6->active_stride);
  return 0;
}

int rtg64_vc6_attach_framebuffer(rtg64_vc6_backend_t *vc6, uint8_t *fb, uint32_t fb_bytes) {
  if (!vc6 || !vc6->initialized || !vc6->mode_set || !fb || fb_bytes == 0) {
    return -1;
  }

  vc6->framebuffer_bytes = fb_bytes;
  vc6->framebuffer_attached = 1;
  return 0;
}

int rtg64_vc6_present(rtg64_vc6_backend_t *vc6, const uint8_t *fb, uint32_t fb_bytes) {
  if (!vc6 || !vc6->initialized || !vc6->mode_set || !vc6->framebuffer_attached || !fb) {
    return -1;
  }

  if (fb_bytes == 0 || fb_bytes > vc6->framebuffer_bytes) {
    return -1;
  }

  vc6->frame_counter++;

  /* Placeholder: future submit path (KMS/HVS plane update) goes here. */
  return 0;
}

int rtg64_vc6_set_palette_entry(rtg64_vc6_backend_t *vc6, uint16_t index, uint32_t argb) {
  (void)argb;

  if (!vc6 || !vc6->initialized) {
    return -1;
  }

  if (index >= 256u) {
    return -1;
  }

  return 0;
}
