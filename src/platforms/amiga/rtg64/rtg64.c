// SPDX-License-Identifier: MIT
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "rtg64.h"
#include "rtg64-bench.h"
#include "rtg64-transport.h"
#include "rtg64-vc6.h"

typedef struct rtg64_state {
  rtg64_transport_t transport;
  rtg64_mode_t mode;
  uint8_t *framebuffer;
  uint32_t framebuffer_bytes;
  uint32_t status_flags;
  uint32_t frame_counter;
  rtg64_vc6_backend_t vc6;
  uint8_t initialized;
} rtg64_state_t;

static rtg64_state_t g_rtg64;

static uint32_t rtg64_bytes_per_pixel(uint16_t format) {
  switch (format) {
  case RTG64_FMT_CLUT8:
    return 1;
  case RTG64_FMT_RGB565:
    return 2;
  case RTG64_FMT_XRGB8888:
  case RTG64_FMT_ARGB8888:
    return 4;
  default:
    return 0;
  }
}

static void rtg64_set_result(uint32_t result, uint32_t add_status_flags) {
  if (result != RTG64_RES_OK) {
    g_rtg64.status_flags |= RTG64_STATUS_ERROR;
  }
  g_rtg64.status_flags |= add_status_flags;
  rtg64_transport_complete(&g_rtg64.transport, result, g_rtg64.status_flags);
}

static int rtg64_ensure_framebuffer(void) {
  if (!g_rtg64.framebuffer || g_rtg64.framebuffer_bytes == 0) {
    rtg64_set_result(RTG64_RES_NOT_READY, 0);
    return -1;
  }
  return 0;
}

static int rtg64_apply_mode_args(uint32_t width, uint32_t height, uint32_t format, uint32_t stride) {
  if (width == 0 || height == 0 || width > 4096 || height > 2160) {
    return -1;
  }

  const uint32_t bpp = rtg64_bytes_per_pixel((uint16_t)format);
  if (bpp == 0) {
    return -1;
  }

  const uint32_t min_stride = width * bpp;
  if (stride < min_stride) {
    stride = min_stride;
  }

  g_rtg64.mode.width = (uint16_t)width;
  g_rtg64.mode.height = (uint16_t)height;
  g_rtg64.mode.format = (uint16_t)format;
  g_rtg64.mode.stride = stride;
  g_rtg64.status_flags |= RTG64_STATUS_MODE_SET;
  return 0;
}

static uint32_t rtg64_dispatch_command(const rtg64_transport_command_t *cmd) {
  if (!cmd) {
    return RTG64_RES_BAD_ARG;
  }

  switch (cmd->cmd) {
  case RTG64_CMD_NONE:
    return RTG64_RES_OK;
  case RTG64_CMD_INIT:
    g_rtg64.status_flags = RTG64_STATUS_READY;
    return RTG64_RES_OK;
  case RTG64_CMD_SET_MODE:
    if (rtg64_apply_mode_args(cmd->arg[0], cmd->arg[1], cmd->arg[2], cmd->arg[3]) != 0) {
      return RTG64_RES_BAD_ARG;
    }
    return RTG64_RES_OK;
  case RTG64_CMD_ALLOC_FB:
    if (rtg64_alloc_framebuffer(cmd->arg[0]) != 0) {
      return RTG64_RES_NO_MEM;
    }
    return RTG64_RES_OK;
  case RTG64_CMD_FREE_FB:
    rtg64_free_framebuffer();
    return RTG64_RES_OK;
  case RTG64_CMD_FILL_RECT:
    if (rtg64_fill_rect((uint16_t)cmd->arg[0], (uint16_t)cmd->arg[1], (uint16_t)cmd->arg[2],
                        (uint16_t)cmd->arg[3], cmd->arg[4]) != 0) {
      return RTG64_RES_FAILED;
    }
    return RTG64_RES_OK;
  case RTG64_CMD_BLIT_RECT:
    if (rtg64_blit_rect((uint16_t)cmd->arg[0], (uint16_t)cmd->arg[1], (uint16_t)cmd->arg[2],
                        (uint16_t)cmd->arg[3], (uint16_t)cmd->arg[4], (uint16_t)cmd->arg[5]) != 0) {
      return RTG64_RES_FAILED;
    }
    return RTG64_RES_OK;
  case RTG64_CMD_SET_PALETTE:
    return rtg64_set_palette_entry((uint16_t)cmd->arg[0], cmd->arg[1]) == 0
               ? RTG64_RES_OK
               : RTG64_RES_BAD_ARG;
  case RTG64_CMD_PRESENT:
    return rtg64_present() == 0 ? RTG64_RES_OK : RTG64_RES_FAILED;
  case RTG64_CMD_BENCH_PATTERN:
    if (rtg64_ensure_framebuffer() != 0) {
      return RTG64_RES_NOT_READY;
    }
    rtg64_bench_pattern32(g_rtg64.framebuffer, g_rtg64.mode.width, g_rtg64.mode.height,
                          g_rtg64.mode.stride, cmd->arg[0]);
    return RTG64_RES_OK;
  case RTG64_CMD_BENCH_SCROLL:
    if (rtg64_ensure_framebuffer() != 0) {
      return RTG64_RES_NOT_READY;
    }
    rtg64_bench_scroll32(g_rtg64.framebuffer, g_rtg64.mode.width, g_rtg64.mode.height,
                         g_rtg64.mode.stride, cmd->arg[0]);
    return RTG64_RES_OK;
  default:
    return RTG64_RES_BAD_CMD;
  }
}

static void rtg64_process_pending_commands(void) {
  rtg64_transport_command_t cmd;
  while (rtg64_transport_pop_command(&g_rtg64.transport, &cmd) == 1) {
    uint32_t status = g_rtg64.status_flags;
    const uint32_t result = rtg64_dispatch_command(&cmd);
    if (result != RTG64_RES_OK) {
      status |= RTG64_STATUS_ERROR;
    }
    rtg64_set_result(result, status);
  }
}

int rtg64_init(void) {
  if (g_rtg64.initialized) {
    return 0;
  }

  memset(&g_rtg64, 0, sizeof(g_rtg64));
  if (rtg64_transport_open(&g_rtg64.transport) != 0) {
    return -1;
  }
  if (rtg64_vc6_init(&g_rtg64.vc6) != 0) {
    rtg64_transport_close(&g_rtg64.transport);
    return -1;
  }

  g_rtg64.status_flags = RTG64_STATUS_READY;
  g_rtg64.initialized = 1;
  LOG_INFO("[RTG64] Initialized experimental RTG64 scaffold backend.\n");
  return 0;
}

void rtg64_shutdown(void) {
  if (!g_rtg64.initialized) {
    return;
  }

  rtg64_free_framebuffer();
  rtg64_vc6_shutdown(&g_rtg64.vc6);
  rtg64_transport_close(&g_rtg64.transport);
  memset(&g_rtg64, 0, sizeof(g_rtg64));
}

void rtg64_reset(void) {
  if (!g_rtg64.initialized) {
    return;
  }

  rtg64_free_framebuffer();
  memset(&g_rtg64.mode, 0, sizeof(g_rtg64.mode));
  g_rtg64.status_flags = RTG64_STATUS_READY;
  rtg64_vc6_shutdown(&g_rtg64.vc6);
  rtg64_vc6_init(&g_rtg64.vc6);
  rtg64_transport_reset(&g_rtg64.transport);
}

int rtg64_set_mode(const rtg64_mode_t *mode) {
  if (!g_rtg64.initialized || !mode) {
    return -1;
  }

  if (rtg64_apply_mode_args(mode->width, mode->height, mode->format, mode->stride) != 0) {
    return -1;
  }
  if (rtg64_vc6_set_mode(&g_rtg64.vc6, &g_rtg64.mode) != 0) {
    return -1;
  }

  return 0;
}

int rtg64_alloc_framebuffer(uint32_t bytes) {
  if (!g_rtg64.initialized || bytes == 0) {
    return -1;
  }

  if (bytes == g_rtg64.framebuffer_bytes && g_rtg64.framebuffer) {
    return 0;
  }

  uint8_t *new_fb = calloc(1, bytes);
  if (!new_fb) {
    return -1;
  }

  free(g_rtg64.framebuffer);
  g_rtg64.framebuffer = new_fb;
  g_rtg64.framebuffer_bytes = bytes;
  if (rtg64_vc6_attach_framebuffer(&g_rtg64.vc6, g_rtg64.framebuffer, g_rtg64.framebuffer_bytes) != 0) {
    free(g_rtg64.framebuffer);
    g_rtg64.framebuffer = NULL;
    g_rtg64.framebuffer_bytes = 0;
    return -1;
  }
  g_rtg64.status_flags |= RTG64_STATUS_FB_ALLOC;
  return 0;
}

void rtg64_free_framebuffer(void) {
  free(g_rtg64.framebuffer);
  g_rtg64.framebuffer = NULL;
  g_rtg64.framebuffer_bytes = 0;
  g_rtg64.status_flags &= ~RTG64_STATUS_FB_ALLOC;
}

int rtg64_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
  if (rtg64_ensure_framebuffer() != 0) {
    return -1;
  }

  if (g_rtg64.mode.format != RTG64_FMT_XRGB8888 && g_rtg64.mode.format != RTG64_FMT_ARGB8888) {
    return -1;
  }

  if (x >= g_rtg64.mode.width || y >= g_rtg64.mode.height) {
    return -1;
  }

  uint16_t x_end = x + w;
  uint16_t y_end = y + h;
  if (x_end > g_rtg64.mode.width) {
    x_end = g_rtg64.mode.width;
  }
  if (y_end > g_rtg64.mode.height) {
    y_end = g_rtg64.mode.height;
  }

  for (uint16_t row = y; row < y_end; row++) {
    uint32_t *dst = (uint32_t *)(void *)(g_rtg64.framebuffer +
                                         ((size_t)row * g_rtg64.mode.stride) +
                                         ((size_t)x * sizeof(uint32_t)));
    for (uint16_t col = x; col < x_end; col++) {
      *dst++ = color;
    }
  }

  return 0;
}

int rtg64_blit_rect(uint16_t sx, uint16_t sy, uint16_t dx, uint16_t dy, uint16_t w, uint16_t h) {
  if (rtg64_ensure_framebuffer() != 0) {
    return -1;
  }

  if (g_rtg64.mode.format != RTG64_FMT_XRGB8888 && g_rtg64.mode.format != RTG64_FMT_ARGB8888) {
    return -1;
  }

  if ((sx + w) > g_rtg64.mode.width || (dx + w) > g_rtg64.mode.width ||
      (sy + h) > g_rtg64.mode.height || (dy + h) > g_rtg64.mode.height) {
    return -1;
  }

  for (uint16_t row = 0; row < h; row++) {
    uint8_t *src = g_rtg64.framebuffer + ((size_t)(sy + row) * g_rtg64.mode.stride) +
                   ((size_t)sx * sizeof(uint32_t));
    uint8_t *dst = g_rtg64.framebuffer + ((size_t)(dy + row) * g_rtg64.mode.stride) +
                   ((size_t)dx * sizeof(uint32_t));
    memmove(dst, src, (size_t)w * sizeof(uint32_t));
  }

  return 0;
}

int rtg64_set_palette_entry(uint16_t index, uint32_t argb) {
  if (index >= 256u) {
    return -1;
  }
  return rtg64_vc6_set_palette_entry(&g_rtg64.vc6, index, argb);
}

int rtg64_present(void) {
  if (rtg64_ensure_framebuffer() != 0) {
    return -1;
  }
  if (rtg64_vc6_present(&g_rtg64.vc6, g_rtg64.framebuffer, g_rtg64.framebuffer_bytes) != 0) {
    return -1;
  }

  g_rtg64.frame_counter++;
  return 0;
}

int rtg64_run_smoketest(void) {
  rtg64_mode_t mode = {
      .width = 640,
      .height = 480,
      .format = RTG64_FMT_XRGB8888,
      .stride = 640u * 4u,
  };

  if (rtg64_set_mode(&mode) != 0) {
    return -1;
  }

  if (rtg64_alloc_framebuffer(mode.stride * mode.height) != 0) {
    return -1;
  }

  rtg64_bench_pattern32(g_rtg64.framebuffer, mode.width, mode.height, mode.stride,
                        g_rtg64.frame_counter);
  rtg64_bench_scroll32(g_rtg64.framebuffer, mode.width, mode.height, mode.stride, 8);
  return 0;
}

uint32_t rtg64_mmio_read(uint32_t offset, uint8_t access_size) {
  return rtg64_transport_read(&g_rtg64.transport, offset, access_size);
}

void rtg64_mmio_write(uint32_t offset, uint32_t value, uint8_t access_size) {
  rtg64_transport_write(&g_rtg64.transport, offset, value, access_size);
  if (offset == RTG64_REG_CMD) {
    rtg64_process_pending_commands();
  }
}

uint8_t *rtg64_get_framebuffer(uint32_t *bytes_out, rtg64_mode_t *mode_out) {
  if (bytes_out) {
    *bytes_out = g_rtg64.framebuffer_bytes;
  }
  if (mode_out) {
    *mode_out = g_rtg64.mode;
  }
  return g_rtg64.framebuffer;
}
