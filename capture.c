// SPDX-License-Identifier: MIT
// Simple Amiga screen capture via PiStorm GPIO reads.
// Captures planar bitplanes from chip RAM and writes a PPM (P6) image.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpio/ps_protocol.h"
#include "platforms/amiga/registers/agnus.h"
#include "platforms/amiga/registers/denise.h"

#define CHIP_MASK 0x1FFFFF

// ps_protocol.c expects this symbol from the emulator core.
void m68k_set_irq(unsigned int level) {
  (void)level;
}

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [--out <file>] [--width <px>] [--height <px>] [--planes <n>]\n"
          "          [--ehb] [--ham] [--info]\n"
          "          [--bpl1 <addr>] ... [--bpl6 <addr>] [--mod1 <val>] [--mod2 <val>]\n"
          "          [--rowbytes <val>] [--line-step <val>]\n"
          "\n"
          "Defaults: width=320 height=256 planes=auto out=capture.ppm\n"
          "Notes:\n"
          "- This reads chip RAM via the GPIO protocol while the emulator runs.\n"
          "- For HAM/EHB, pass --ham or --ehb to override mode detection.\n"
          "- Some custom registers are write-only on real hardware; use overrides if reads look invalid.\n",
          prog);
}

static uint32_t parse_u32(const char *s) {
  char *end = NULL;
  unsigned long v = strtoul(s, &end, 0);
  if (!s[0] || (end && *end)) {
    fprintf(stderr, "Invalid number: %s\n", s);
    exit(1);
  }
  return (uint32_t)v;
}

static void read_palette(uint8_t rgb[32][3]) {
  for (int i = 0; i < 32; i++) {
    uint16_t raw = (uint16_t)ps_read_16(COLOR00 + (uint32_t)(i * 2));
    uint8_t r = (uint8_t)((raw >> 8) & 0x0F);
    uint8_t g = (uint8_t)((raw >> 4) & 0x0F);
    uint8_t b = (uint8_t)(raw & 0x0F);
    rgb[i][0] = (uint8_t)(r * 17);
    rgb[i][1] = (uint8_t)(g * 17);
    rgb[i][2] = (uint8_t)(b * 17);
  }
}

static uint32_t read_bpl_ptr(uint32_t pth, uint32_t ptl) {
  uint32_t hi = (uint32_t)ps_read_16(pth) & 0xFFFFu;
  uint32_t lo = (uint32_t)ps_read_16(ptl) & 0xFFFFu;
  return ((hi << 16) | lo) & CHIP_MASK;
}

static void read_row(uint8_t *dst, uint32_t base, uint32_t bytes) {
  uint32_t i = 0;
  for (; i + 1 < bytes; i += 2) {
    uint16_t v = (uint16_t)ps_read_16(base + i);
    dst[i] = (uint8_t)(v >> 8);
    dst[i + 1] = (uint8_t)(v & 0xFF);
  }
  if (i < bytes) {
    dst[i] = (uint8_t)ps_read_8(base + i);
  }
}

int main(int argc, char **argv) {
  const char *out_path = "capture.ppm";
  uint32_t width = 320;
  uint32_t height = 256;
  int planes = -1;
  int use_ehb = 0;
  int use_ham = 0;
  int show_info = 0;
  uint32_t bpl_override[6] = {0};
  int have_bpl[6] = {0};
  int have_mod1 = 0;
  int have_mod2 = 0;
  uint16_t mod1_override = 0;
  uint16_t mod2_override = 0;
  uint32_t rowbytes_override = 0;
  int have_line_step = 0;
  uint32_t line_step_override = 0;

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "--out")) {
      if (i + 1 >= argc) usage(argv[0]);
      out_path = argv[++i];
    } else if (!strcmp(arg, "--width")) {
      if (i + 1 >= argc) usage(argv[0]);
      width = parse_u32(argv[++i]);
    } else if (!strcmp(arg, "--height")) {
      if (i + 1 >= argc) usage(argv[0]);
      height = parse_u32(argv[++i]);
    } else if (!strcmp(arg, "--planes")) {
      if (i + 1 >= argc) usage(argv[0]);
      planes = (int)parse_u32(argv[++i]);
    } else if (!strcmp(arg, "--ehb")) {
      use_ehb = 1;
    } else if (!strcmp(arg, "--ham")) {
      use_ham = 1;
    } else if (!strcmp(arg, "--info")) {
      show_info = 1;
    } else if (!strcmp(arg, "--bpl1")) {
      if (i + 1 >= argc) usage(argv[0]);
      bpl_override[0] = parse_u32(argv[++i]);
      have_bpl[0] = 1;
    } else if (!strcmp(arg, "--bpl2")) {
      if (i + 1 >= argc) usage(argv[0]);
      bpl_override[1] = parse_u32(argv[++i]);
      have_bpl[1] = 1;
    } else if (!strcmp(arg, "--bpl3")) {
      if (i + 1 >= argc) usage(argv[0]);
      bpl_override[2] = parse_u32(argv[++i]);
      have_bpl[2] = 1;
    } else if (!strcmp(arg, "--bpl4")) {
      if (i + 1 >= argc) usage(argv[0]);
      bpl_override[3] = parse_u32(argv[++i]);
      have_bpl[3] = 1;
    } else if (!strcmp(arg, "--bpl5")) {
      if (i + 1 >= argc) usage(argv[0]);
      bpl_override[4] = parse_u32(argv[++i]);
      have_bpl[4] = 1;
    } else if (!strcmp(arg, "--bpl6")) {
      if (i + 1 >= argc) usage(argv[0]);
      bpl_override[5] = parse_u32(argv[++i]);
      have_bpl[5] = 1;
    } else if (!strcmp(arg, "--mod1")) {
      if (i + 1 >= argc) usage(argv[0]);
      mod1_override = (uint16_t)parse_u32(argv[++i]);
      have_mod1 = 1;
    } else if (!strcmp(arg, "--mod2")) {
      if (i + 1 >= argc) usage(argv[0]);
      mod2_override = (uint16_t)parse_u32(argv[++i]);
      have_mod2 = 1;
    } else if (!strcmp(arg, "--rowbytes")) {
      if (i + 1 >= argc) usage(argv[0]);
      rowbytes_override = parse_u32(argv[++i]);
    } else if (!strcmp(arg, "--line-step")) {
      if (i + 1 >= argc) usage(argv[0]);
      line_step_override = parse_u32(argv[++i]);
      have_line_step = 1;
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  ps_setup_protocol();

  uint16_t bplcon0 = (uint16_t)ps_read_16(BPLCON0);
  uint16_t bpl1mod = (uint16_t)ps_read_16(BPL1MOD);
  uint16_t bpl2mod = (uint16_t)ps_read_16(BPL2MOD);
  uint16_t diwstrt = (uint16_t)ps_read_16(DIWSTRT);
  uint16_t diwstop = (uint16_t)ps_read_16(DIWSTOP);
  uint16_t ddfstrt = (uint16_t)ps_read_16(DDFSTRT);
  uint16_t ddfstop = (uint16_t)ps_read_16(DDFSTOP);

  if (planes < 0) {
    int bpu = (int)((bplcon0 >> 12) & 0x7);
    if (bplcon0 == 0xFFFF || bpu == 0 || bpu == 7) {
      fprintf(stderr,
              "BPLCON0 read looks invalid (0x%04X). "
              "Pass --planes and --bplN overrides.\n",
              bplcon0);
      return 1;
    }
    planes = bpu;
  }

  if (!use_ham && !use_ehb && bplcon0 != 0xFFFF) {
    use_ham = (bplcon0 & 0x0800u) != 0;
    use_ehb = (bplcon0 & 0x0400u) != 0;
    if (use_ham && use_ehb) {
      fprintf(stderr, "Warning: BPLCON0 indicates HAM+EHB; using HAM.\n");
      use_ehb = 0;
    }
  }

  if (have_mod1) {
    bpl1mod = mod1_override;
  } else if (bpl1mod == 0xFFFF) {
    bpl1mod = 0;
  }
  if (have_mod2) {
    bpl2mod = mod2_override;
  } else if (bpl2mod == 0xFFFF) {
    bpl2mod = 0;
  }

  if (show_info) {
    printf("BPLCON0=0x%04X planes=%d\n", bplcon0, planes);
    printf("DIWSTRT=0x%04X DIWSTOP=0x%04X DDFSTRT=0x%04X DDFSTOP=0x%04X\n",
           diwstrt, diwstop, ddfstrt, ddfstop);
    printf("BPL1MOD=0x%04X BPL2MOD=0x%04X\n", bpl1mod, bpl2mod);
  }

  if (planes < 1 || planes > 6) {
    fprintf(stderr, "Unsupported planes=%d (expected 1-6 for OCS/ECS)\n", planes);
    return 1;
  }

  uint32_t bytes_per_row = (width + 7) / 8;
  if (rowbytes_override) {
    bytes_per_row = rowbytes_override;
  }
  if (width % 16) {
    fprintf(stderr, "Warning: width not multiple of 16; capture may be skewed.\n");
  }

  uint32_t ptrs[6];
  for (int p = 0; p < 6; p++) {
    if (have_bpl[p]) {
      ptrs[p] = bpl_override[p] & CHIP_MASK;
      continue;
    }
    uint32_t ptr = 0;
    if (p == 0) ptr = read_bpl_ptr(BPL1PTH, BPL1PTL);
    if (p == 1) ptr = read_bpl_ptr(BPL2PTH, BPL2PTL);
    if (p == 2) ptr = read_bpl_ptr(BPL3PTH, BPL3PTL);
    if (p == 3) ptr = read_bpl_ptr(BPL4PTH, BPL4PTL);
    if (p == 4) ptr = read_bpl_ptr(BPL5PTH, BPL5PTL);
    if (p == 5) ptr = read_bpl_ptr(BPL6PTH, BPL6PTL);
    if (ptr == CHIP_MASK) {
      fprintf(stderr,
              "BPL%d pointer read looks invalid (0x%06X). "
              "Pass --bpl%d <addr>.\n",
              p + 1, ptr, p + 1);
      return 1;
    }
    ptrs[p] = ptr;
  }

  if (show_info) {
    for (int i = 0; i < planes; i++) {
      printf("BPL%d ptr=0x%06X\n", i + 1, ptrs[i]);
    }
  }

  uint8_t palette[32][3];
  read_palette(palette);

  FILE *out = fopen(out_path, "wb");
  if (!out) {
    perror("fopen");
    return 1;
  }

  fprintf(out, "P6\n%u %u\n255\n", width, height);

  uint8_t *row_planes[6];
  for (int p = 0; p < planes; p++) {
    row_planes[p] = (uint8_t *)calloc(1, bytes_per_row);
    if (!row_planes[p]) {
      fprintf(stderr, "Out of memory\n");
      return 1;
    }
  }

  for (uint32_t y = 0; y < height; y++) {
    uint32_t line_off_odd = (uint32_t)((int32_t)bytes_per_row + (int16_t)bpl1mod);
    uint32_t line_off_even = (uint32_t)((int32_t)bytes_per_row + (int16_t)bpl2mod);
    for (int p = 0; p < planes; p++) {
      uint32_t line_off = (p & 1) ? line_off_even : line_off_odd;
      if (have_line_step) {
        line_off = line_step_override;
      }
      uint32_t addr = ptrs[p] + y * line_off;
      read_row(row_planes[p], addr, bytes_per_row);
    }

    uint8_t cur_r = 0, cur_g = 0, cur_b = 0;
    for (uint32_t x = 0; x < width; x++) {
      uint8_t mask = (uint8_t)(0x80u >> (x & 7u));
      uint32_t byte_idx = x >> 3;
      uint8_t pix = 0;
      for (int p = 0; p < planes; p++) {
        if (row_planes[p][byte_idx] & mask) {
          pix |= (uint8_t)(1u << p);
        }
      }

      uint8_t r, g, b;
      if (use_ham) {
        uint8_t ctrl = (uint8_t)((pix >> 4) & 0x3);
        uint8_t data = (uint8_t)(pix & 0xF);
        if (ctrl == 0) {
          r = palette[data][0];
          g = palette[data][1];
          b = palette[data][2];
        } else if (ctrl == 1) {
          r = cur_r;
          g = cur_g;
          b = (uint8_t)(data * 17);
        } else if (ctrl == 2) {
          r = (uint8_t)(data * 17);
          g = cur_g;
          b = cur_b;
        } else {
          r = cur_r;
          g = (uint8_t)(data * 17);
          b = cur_b;
        }
        cur_r = r;
        cur_g = g;
        cur_b = b;
      } else if (use_ehb && pix >= 32) {
        uint8_t base = (uint8_t)(pix - 32);
        r = (uint8_t)(palette[base][0] >> 1);
        g = (uint8_t)(palette[base][1] >> 1);
        b = (uint8_t)(palette[base][2] >> 1);
      } else {
        uint8_t idx = (uint8_t)(pix & 0x1F);
        r = palette[idx][0];
        g = palette[idx][1];
        b = palette[idx][2];
      }

      fputc(r, out);
      fputc(g, out);
      fputc(b, out);
    }
  }

  for (int p = 0; p < planes; p++) {
    free(row_planes[p]);
  }

  fclose(out);
  fprintf(stdout, "Wrote %s (%ux%u)\n", out_path, width, height);
  return 0;
}
