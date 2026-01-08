// SPDX-License-Identifier: MIT
// Pi-side Paula DMA audio harness (raw sample playback).
// MOD replay engine will be layered on top of this.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "gpio/ps_protocol.h"
#include "paula.h"

// ps_protocol.c expects this symbol from the emulator core.
void m68k_set_irq(unsigned int level) {
  (void)level;
}

#define DMAF_SETCLR 0x8000
#define DMAF_MASTER 0x0200
#define DMAF_AUD0   0x0001

static volatile sig_atomic_t stop_requested = 0;

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [--raw <file> | --mod <file>] [options]\n"
          "\n"
          "Raw sample playback (AUD0 DMA):\n"
          "  --raw <file>        8-bit unsigned sample data\n"
          "  --addr <hex>        Chip RAM load address (default 0x00080000)\n"
          "  --period <val>      Paula period (default 200)\n"
          "  --vol <0-64>        Volume (default 64)\n"
          "  --seconds <n>       Playback duration (default 5)\n"
          "\n"
          "MOD playback (planned):\n"
          "  --mod <file>        ProTracker MOD (not yet implemented)\n"
          "\n"
          "Timing:\n"
          "  --pal (default)     50 Hz tick base\n"
          "  --ntsc              60 Hz tick base\n"
          "\n"
          "Control:\n"
          "  --stop              Stop audio DMA and mute\n",
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

static void handle_sigint(int sig) {
  (void)sig;
  stop_requested = 1;
}

static uint8_t *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror(path);
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long len = ftell(f);
  if (len < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  uint8_t *buf = (uint8_t *)malloc((size_t)len);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  *out_len = (size_t)len;
  return buf;
}

static void write_chip_ram(uint32_t addr, const uint8_t *buf, size_t len) {
  size_t i = 0;
  for (; i + 1 < len; i += 2) {
    uint16_t v = ((uint16_t)buf[i] << 8) | (uint16_t)buf[i + 1];
    ps_write_16(addr + (uint32_t)i, v);
  }
  if (i < len) {
    ps_write_8(addr + (uint32_t)i, buf[i]);
  }
}

static void audio_stop(void) {
  ps_write_16(AUD0VOL, 0);
  ps_write_16(DMACON, DMAF_MASTER | DMAF_AUD0);
}

static void audio_play_raw(uint32_t addr, const uint8_t *buf, size_t len,
                           uint16_t period, uint16_t vol, unsigned seconds) {
  uint32_t addr_masked = addr & 0x1FFFFu;
  uint16_t len_words = (uint16_t)((len + 1u) / 2u);

  write_chip_ram(addr, buf, len);

  ps_write_16(AUD0LCH, (addr_masked >> 16) & 0x1Fu);
  ps_write_16(AUD0LCL, addr_masked & 0xFFFFu);
  ps_write_16(AUD0LEN, len_words);
  ps_write_16(AUD0PER, period);
  ps_write_16(AUD0VOL, vol);
  ps_write_16(DMACON, DMAF_SETCLR | DMAF_MASTER | DMAF_AUD0);

  for (unsigned i = 0; i < seconds && !stop_requested; i++) {
    sleep(1);
  }
  audio_stop();
}

int main(int argc, char **argv) {
  const char *raw_path = NULL;
  const char *mod_path = NULL;
  uint32_t addr = 0x00080000u;
  uint16_t period = 200u;
  uint16_t vol = 64u;
  unsigned seconds = 5;
  int stop_only = 0;
  int is_pal = 1;

  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "--raw")) {
      if (i + 1 >= argc) usage(argv[0]);
      raw_path = argv[++i];
      continue;
    }
    if (!strcmp(arg, "--mod")) {
      if (i + 1 >= argc) usage(argv[0]);
      mod_path = argv[++i];
      continue;
    }
    if (!strcmp(arg, "--addr")) {
      if (i + 1 >= argc) usage(argv[0]);
      addr = parse_u32(argv[++i]);
      continue;
    }
    if (!strcmp(arg, "--period")) {
      if (i + 1 >= argc) usage(argv[0]);
      period = (uint16_t)parse_u32(argv[++i]);
      continue;
    }
    if (!strcmp(arg, "--vol")) {
      if (i + 1 >= argc) usage(argv[0]);
      vol = (uint16_t)parse_u32(argv[++i]);
      if (vol > 64) vol = 64;
      continue;
    }
    if (!strcmp(arg, "--seconds")) {
      if (i + 1 >= argc) usage(argv[0]);
      seconds = (unsigned)parse_u32(argv[++i]);
      continue;
    }
    if (!strcmp(arg, "--pal")) {
      is_pal = 1;
      continue;
    }
    if (!strcmp(arg, "--ntsc")) {
      is_pal = 0;
      continue;
    }
    if (!strcmp(arg, "--stop")) {
      stop_only = 1;
      continue;
    }
    usage(argv[0]);
    return 1;
  }

  ps_setup_protocol();
  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);

  if (stop_only) {
    audio_stop();
    return 0;
  }

  if (mod_path) {
    fprintf(stderr,
            "MOD playback not yet implemented (raw DMA works).\n"
            "Use --raw for now. PAL=%d\n", is_pal);
    return 1;
  }

  if (!raw_path) {
    usage(argv[0]);
    return 1;
  }

  size_t len = 0;
  uint8_t *buf = read_file(raw_path, &len);
  if (!buf || len == 0) {
    fprintf(stderr, "Failed to read sample: %s\n", raw_path);
    free(buf);
    return 1;
  }

  printf("[RAW] addr=0x%06X bytes=%zu period=%u vol=%u seconds=%u PAL=%d\n",
         addr & 0x1FFFFu, len, period, vol, seconds, is_pal);
  audio_play_raw(addr, buf, len, period, vol, seconds);
  free(buf);
  return 0;
}
