// SPDX-License-Identifier: MIT
#include <stdint.h>

#include "arm64_accel_regs.h"

#define JULIA_MAGIC 0x4A554C49u /* "JULI" */
#define JULIA_VERSION 1u
#define JULIA_Q16_ONE 65536
#define JULIA_Q48_SHIFT 48
#define JULIA_Q48_ONE (1LL << JULIA_Q48_SHIFT)
#define JULIA_SCALE_DEFAULT_Q48 (2LL * JULIA_Q48_ONE)
#define JULIA_SCALE_MIN_Q48 1LL
#define JULIA_SCALE_MAX_Q48 (4LL * JULIA_Q48_ONE)
#define JULIA_C_RE_DEFAULT_Q48 (-225179981368525LL) /* -0.8 * 2^48 */
#define JULIA_C_IM_DEFAULT_Q48 (43910133442195LL)   /*  0.156 * 2^48 */

#define JULIA_OFF_MAGIC 0x40u
#define JULIA_OFF_VERSION 0x44u
#define JULIA_OFF_WIDTH 0x48u
#define JULIA_OFF_HEIGHT 0x4Cu
#define JULIA_OFF_PIXELS_OFFSET 0x50u
#define JULIA_OFF_PIXELS_SIZE 0x54u
#define JULIA_OFF_CENTER_X_Q16 0x58u
#define JULIA_OFF_CENTER_Y_Q16 0x5Cu
#define JULIA_OFF_SCALE_Q16 0x60u
#define JULIA_OFF_FLAGS 0x64u
#define JULIA_OFF_CENTER_X_Q48_HI 0x68u
#define JULIA_OFF_CENTER_X_Q48_LO 0x6Cu
#define JULIA_OFF_CENTER_Y_Q48_HI 0x70u
#define JULIA_OFF_CENTER_Y_Q48_LO 0x74u
#define JULIA_OFF_SCALE_Q48_HI 0x78u
#define JULIA_OFF_SCALE_Q48_LO 0x7Cu
#define JULIA_OFF_C_RE_Q48_HI 0x80u
#define JULIA_OFF_C_RE_Q48_LO 0x84u
#define JULIA_OFF_C_IM_Q48_HI 0x88u
#define JULIA_OFF_C_IM_Q48_LO 0x8Cu

#define JULIA_MODE_JULIA 0u
#define JULIA_MODE_MANDELBROT 1u
#define JULIA_FLAGS_MODE_MASK 0xFFu
#define JULIA_FLAGS_POWER_SHIFT 8u
#define JULIA_FLAGS_POWER_MASK (0xFFu << JULIA_FLAGS_POWER_SHIFT)

static uint32_t be32_read(const uint8_t *p) {
  return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) | ((uint32_t)p[2] << 8u) |
         (uint32_t)p[3];
}

static void be32_write(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)((v >> 24u) & 0xFFu);
  p[1] = (uint8_t)((v >> 16u) & 0xFFu);
  p[2] = (uint8_t)((v >> 8u) & 0xFFu);
  p[3] = (uint8_t)(v & 0xFFu);
}

static uint64_t be64_read(const uint8_t *base, uint32_t hi_off, uint32_t lo_off) {
  uint64_t hi = (uint64_t)be32_read(base + hi_off);
  uint64_t lo = (uint64_t)be32_read(base + lo_off);
  return (hi << 32u) | lo;
}

static void be64_write(uint8_t *base, uint32_t hi_off, uint32_t lo_off, uint64_t v) {
  be32_write(base + hi_off, (uint32_t)(v >> 32u));
  be32_write(base + lo_off, (uint32_t)(v & 0xFFFFFFFFu));
}

static int64_t clamp_scale_q48(int64_t s) {
  if (s < JULIA_SCALE_MIN_Q48) {
    return JULIA_SCALE_MIN_Q48;
  }
  if (s > JULIA_SCALE_MAX_Q48) {
    return JULIA_SCALE_MAX_Q48;
  }
  return s;
}

uint64_t arm_job_entry(void *job_ptr) {
  uint8_t *job = (uint8_t *)job_ptr;
  uint8_t *base = job - ARM64_ACCEL_JOBDESC_OFFSET;
  uint32_t magic = be32_read(job + JULIA_OFF_MAGIC);
  uint32_t version = be32_read(job + JULIA_OFF_VERSION);
  uint32_t width = be32_read(job + JULIA_OFF_WIDTH);
  uint32_t height = be32_read(job + JULIA_OFF_HEIGHT);
  uint32_t pixels_offset = be32_read(job + JULIA_OFF_PIXELS_OFFSET);
  uint32_t pixels_size = be32_read(job + JULIA_OFF_PIXELS_SIZE);
  int64_t center_x_q48 =
      (int64_t)be64_read(job, JULIA_OFF_CENTER_X_Q48_HI, JULIA_OFF_CENTER_X_Q48_LO);
  int64_t center_y_q48 =
      (int64_t)be64_read(job, JULIA_OFF_CENTER_Y_Q48_HI, JULIA_OFF_CENTER_Y_Q48_LO);
  int64_t scale_q48 = (int64_t)be64_read(job, JULIA_OFF_SCALE_Q48_HI, JULIA_OFF_SCALE_Q48_LO);
  int64_t c_re_q48 = (int64_t)be64_read(job, JULIA_OFF_C_RE_Q48_HI, JULIA_OFF_C_RE_Q48_LO);
  int64_t c_im_q48 = (int64_t)be64_read(job, JULIA_OFF_C_IM_Q48_HI, JULIA_OFF_C_IM_Q48_LO);
  uint32_t flags = be32_read(job + JULIA_OFF_FLAGS);
  uint32_t mode = flags & JULIA_FLAGS_MODE_MASK;
  uint32_t power = (flags & JULIA_FLAGS_POWER_MASK) >> JULIA_FLAGS_POWER_SHIFT;
  uint8_t *pixels;
  uint32_t x;
  uint32_t y;

  if (magic != JULIA_MAGIC || version != JULIA_VERSION) {
    width = 512u;
    height = 384u;
    center_x_q48 = 0;
    center_y_q48 = 0;
    scale_q48 = JULIA_SCALE_DEFAULT_Q48;
    c_re_q48 = JULIA_C_RE_DEFAULT_Q48;
    c_im_q48 = JULIA_C_IM_DEFAULT_Q48;
    mode = JULIA_MODE_JULIA;
    power = 2u;
  }

  if (width == 0u) {
    width = 1u;
  }
  if (height == 0u) {
    height = 1u;
  }
  if (scale_q48 == 0) {
    int32_t scale_q16 = (int32_t)be32_read(job + JULIA_OFF_SCALE_Q16);
    if (scale_q16 != 0) {
      scale_q48 = ((int64_t)scale_q16) << 32;
    } else {
      scale_q48 = JULIA_SCALE_DEFAULT_Q48;
    }
  }
  if (center_x_q48 == 0 && center_y_q48 == 0) {
    center_x_q48 = ((int64_t)(int32_t)be32_read(job + JULIA_OFF_CENTER_X_Q16)) << 32;
    center_y_q48 = ((int64_t)(int32_t)be32_read(job + JULIA_OFF_CENTER_Y_Q16)) << 32;
  }
  if (c_re_q48 == 0 && c_im_q48 == 0) {
    c_re_q48 = JULIA_C_RE_DEFAULT_Q48;
    c_im_q48 = JULIA_C_IM_DEFAULT_Q48;
  }
  if (mode != JULIA_MODE_MANDELBROT) {
    mode = JULIA_MODE_JULIA;
  }
  if (power < 2u || power > 8u) {
    power = 2u;
  }
  scale_q48 = clamp_scale_q48(scale_q48);

  if (pixels_offset >= ARM64_ACCEL_Z2_SIZE) {
    return 0xBAD00002u;
  }
  if (pixels_size > (ARM64_ACCEL_Z2_SIZE - pixels_offset)) {
    return 0xBAD00002u;
  }
  if (pixels_size < (width * height)) {
    return 0xBAD00002u;
  }

  pixels = base + pixels_offset;

  {
    const int32_t max_iter = 160;
    const double c_re_j = (double)c_re_q48 / (double)(1ULL << JULIA_Q48_SHIFT);
    const double c_im_j = (double)c_im_q48 / (double)(1ULL << JULIA_Q48_SHIFT);
    double scale = (double)scale_q48 / (double)(1ULL << JULIA_Q48_SHIFT);
    double cx = (double)center_x_q48 / (double)(1ULL << JULIA_Q48_SHIFT);
    double cy = (double)center_y_q48 / (double)(1ULL << JULIA_Q48_SHIFT);
    double span_x = scale * 2.0;
    double span_y = span_x * ((double)height / (double)width);
    double start_x = cx - (span_x * 0.5);
    double start_y = cy - (span_y * 0.5);
    double step_x = span_x / (double)width;
    double step_y = span_y / (double)height;

    for (y = 0u; y < height; y++) {
      double point_im = start_y + (double)y * step_y;
      double point_re = start_x;
      for (x = 0u; x < width; x++) {
        double zr;
        double zi;
        double c_re;
        double c_im;
        int32_t iter;

        if (mode == JULIA_MODE_MANDELBROT) {
          zr = 0.0;
          zi = 0.0;
          c_re = point_re;
          c_im = point_im;
        } else {
          zr = point_re;
          zi = point_im;
          c_re = c_re_j;
          c_im = c_im_j;
        }

        for (iter = 0; iter < max_iter; iter++) {
          double zr2 = zr * zr;
          double zi2 = zi * zi;
          if ((zr2 + zi2) > 4.0) {
            break;
          }
          if (power == 2u) {
            zi = (2.0 * zr * zi) + c_im;
            zr = (zr2 - zi2) + c_re;
          } else {
            uint32_t p;
            double pr = zr;
            double pi = zi;
            for (p = 1u; p < power; p++) {
              double tr = (pr * zr) - (pi * zi);
              double ti = (pr * zi) + (pi * zr);
              pr = tr;
              pi = ti;
            }
            zr = pr + c_re;
            zi = pi + c_im;
          }
        }

        if (iter >= max_iter) {
          pixels[(y * width) + x] = 0u;
        } else {
          uint32_t color = (uint32_t)(((iter * 11) + ((uint32_t)x & 7u) + ((uint32_t)y & 3u)) & 0xFFu);
          pixels[(y * width) + x] = (uint8_t)color;
        }
        point_re += step_x;
      }
    }
  }

  be32_write(job + JULIA_OFF_MAGIC, JULIA_MAGIC);
  be32_write(job + JULIA_OFF_VERSION, JULIA_VERSION);
  be32_write(job + JULIA_OFF_WIDTH, width);
  be32_write(job + JULIA_OFF_HEIGHT, height);
  be32_write(job + JULIA_OFF_PIXELS_OFFSET, pixels_offset);
  be32_write(job + JULIA_OFF_PIXELS_SIZE, pixels_size);
  be32_write(job + JULIA_OFF_CENTER_X_Q16, (uint32_t)(center_x_q48 >> 32));
  be32_write(job + JULIA_OFF_CENTER_Y_Q16, (uint32_t)(center_y_q48 >> 32));
  be32_write(job + JULIA_OFF_SCALE_Q16, (uint32_t)(scale_q48 >> 32));
  be64_write(job, JULIA_OFF_CENTER_X_Q48_HI, JULIA_OFF_CENTER_X_Q48_LO, (uint64_t)center_x_q48);
  be64_write(job, JULIA_OFF_CENTER_Y_Q48_HI, JULIA_OFF_CENTER_Y_Q48_LO, (uint64_t)center_y_q48);
  be64_write(job, JULIA_OFF_SCALE_Q48_HI, JULIA_OFF_SCALE_Q48_LO, (uint64_t)scale_q48);
  be64_write(job, JULIA_OFF_C_RE_Q48_HI, JULIA_OFF_C_RE_Q48_LO, (uint64_t)c_re_q48);
  be64_write(job, JULIA_OFF_C_IM_Q48_HI, JULIA_OFF_C_IM_Q48_LO, (uint64_t)c_im_q48);
  be32_write(job + JULIA_OFF_FLAGS, flags);
  return ((uint64_t)width << 32u) | (uint64_t)height;
}
