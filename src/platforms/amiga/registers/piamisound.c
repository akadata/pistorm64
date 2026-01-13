// SPDX-License-Identifier: MIT
// Minimal Paula channel exerciser: play a short tone on a chosen channel.

#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "src/gpio/ps_protocol.h"
#include "paula.h"

#define DMAF_SETCLR 0x8000
#define DMAF_MASTER 0x0200
#define CHIP_SIZE_BYTES 0x100000u  // 1MB Agnus
#define CHIP_ADDR_MASK (CHIP_SIZE_BYTES - 1u)

// ps_protocol.c expects this symbol from the emulator core.
void m68k_set_irq(unsigned int level) {
  (void)level;
}

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int sig) {
  (void)sig;
  stop_requested = 1;
}

static double paula_clock_hz(int is_pal) {
  return is_pal ? 3546895.0 : 3579545.0;
}

static uint16_t period_from_rate(double rate_hz, int is_pal) {
  double clock = paula_clock_hz(is_pal);
  double period = clock / rate_hz;
  if (period < 1.0) period = 1.0;
  if (period > 65535.0) period = 65535.0;
  return (uint16_t)(period + 0.5);
}

static void sleep_seconds(double seconds) {
  if (seconds <= 0.0) return;
  struct timespec ts;
  ts.tv_sec = (time_t)seconds;
  ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1000000000.0);
  if (ts.tv_nsec < 0) ts.tv_nsec = 0;
  if (ts.tv_nsec >= 1000000000L) ts.tv_nsec = 999999999L;
  nanosleep(&ts, NULL);
}

static void write_chip_ram(uint32_t addr, const uint8_t *buf, size_t len) {
  size_t i = 0;
  for (; i + 1 < len; i += 2) {
    uint16_t v = ((uint16_t)buf[i] << 8) | (uint16_t)buf[i + 1];
    ps_write_16(addr + (uint32_t)i, v);
  }
  if (i < len) ps_write_8(addr + (uint32_t)i, buf[i]);
}

static void stop_channel(int ch) {
  uint16_t mask = (uint16_t)(1u << ch);
  ps_write_16(DMACON, DMAF_MASTER | mask);
  switch (ch) {
    case 0: ps_write_16(AUD0VOL, 0); break;
    case 1: ps_write_16(AUD1VOL, 0); break;
    case 2: ps_write_16(AUD2VOL, 0); break;
    case 3: ps_write_16(AUD3VOL, 0); break;
  }
}

static void start_channel(int ch, uint32_t addr, uint16_t len_words,
                          uint16_t period, uint16_t vol) {
  uint16_t mask = (uint16_t)(1u << ch);
  uint32_t addr_masked = addr & CHIP_ADDR_MASK;

  switch (ch) {
    case 0:
      ps_write_16(AUD0VOL, 0);
      ps_write_16(AUD0LCH, (addr_masked >> 16) & 0x1Fu);
      ps_write_16(AUD0LCL, addr_masked & 0xFFFFu);
      ps_write_16(AUD0LEN, len_words);
      ps_write_16(AUD0PER, period);
      ps_write_16(AUD0VOL, vol);
      break;
    case 1:
      ps_write_16(AUD1VOL, 0);
      ps_write_16(AUD1LCH, (addr_masked >> 16) & 0x1Fu);
      ps_write_16(AUD1LCL, addr_masked & 0xFFFFu);
      ps_write_16(AUD1LEN, len_words);
      ps_write_16(AUD1PER, period);
      ps_write_16(AUD1VOL, vol);
      break;
    case 2:
      ps_write_16(AUD2VOL, 0);
      ps_write_16(AUD2LCH, (addr_masked >> 16) & 0x1Fu);
      ps_write_16(AUD2LCL, addr_masked & 0xFFFFu);
      ps_write_16(AUD2LEN, len_words);
      ps_write_16(AUD2PER, period);
      ps_write_16(AUD2VOL, vol);
      break;
    case 3:
      ps_write_16(AUD3VOL, 0);
      ps_write_16(AUD3LCH, (addr_masked >> 16) & 0x1Fu);
      ps_write_16(AUD3LCL, addr_masked & 0xFFFFu);
      ps_write_16(AUD3LEN, len_words);
      ps_write_16(AUD3PER, period);
      ps_write_16(AUD3VOL, vol);
      break;
  }

  ps_write_16(DMACON, DMAF_SETCLR | DMAF_MASTER | mask);
}

static void gen_sine(uint8_t *dst, size_t samples, double freq_hz, double rate_hz) {
  const double two_pi = 6.283185307179586;
  for (size_t i = 0; i < samples; i++) {
    double s = sin(two_pi * freq_hz * ((double)i / rate_hz));
    int v = 128 + (int)(127.0 * s);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    dst[i] = (uint8_t)v;
  }
}

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s --channel <0-3> [--seconds S] [--period P | --rate Hz]\n"
          "             [--vol 0-64] [--addr 0xHEX] [--freq Hz] [--pal|--ntsc]\n"
          "\n"
          "Plays a simple beep on one Paula channel. Default: 1s, period 200, vol 64, freq 440Hz, PAL.\n",
          prog);
}

int main(int argc, char **argv) {
  int ch = -1;
  unsigned seconds = 1;
  uint16_t period = 200;
  unsigned rate_hz = 0;
  uint16_t vol = 64;
  uint32_t addr = 0x00060000u;
  double freq_hz = 440.0;
  int is_pal = 1;

  if (argc < 3) {
    usage(argv[0]);
    return 1;
  }

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "--channel")) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      ch = atoi(argv[++i]);
      continue;
    }
    if (!strcmp(arg, "--seconds")) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      seconds = (unsigned)strtoul(argv[++i], NULL, 0);
      if (seconds == 0) seconds = 1;
      continue;
    }
    if (!strcmp(arg, "--period")) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      period = (uint16_t)strtoul(argv[++i], NULL, 0);
      continue;
    }
    if (!strcmp(arg, "--rate")) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      rate_hz = (unsigned)strtoul(argv[++i], NULL, 0);
      continue;
    }
    if (!strcmp(arg, "--vol")) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      vol = (uint16_t)strtoul(argv[++i], NULL, 0);
      if (vol > 64) vol = 64;
      continue;
    }
    if (!strcmp(arg, "--addr")) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      addr = (uint32_t)strtoul(argv[++i], NULL, 0);
      continue;
    }
    if (!strcmp(arg, "--freq")) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      freq_hz = strtod(argv[++i], NULL);
      continue;
    }
    if (!strcmp(arg, "--pal")) { is_pal = 1; continue; }
    if (!strcmp(arg, "--ntsc")) { is_pal = 0; continue; }
    usage(argv[0]);
    return 1;
  }

  if (ch < 0 || ch > 3) {
    usage(argv[0]);
    return 1;
  }

  if (rate_hz) {
    period = period_from_rate((double)rate_hz, is_pal);
  } else {
    rate_hz = (unsigned)(paula_clock_hz(is_pal) / (double)period + 0.5);
  }

  // Max Paula length is 0xFFFF words (131070 bytes).
  size_t samples = (size_t)rate_hz * (size_t)seconds;
  if (samples > 131070u) samples = 131070u;
  if (samples < 32u) samples = 32u;

  if ((uint64_t)addr + samples > CHIP_SIZE_BYTES) {
    fprintf(stderr, "Address range exceeds 1MB chip RAM.\n");
    return 1;
  }

  uint8_t *buf = (uint8_t *)malloc(samples);
  if (!buf) return 1;
  gen_sine(buf, samples, freq_hz, (double)rate_hz);

  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);
  ps_setup_protocol();
  stop_channel(ch);

  write_chip_ram(addr, buf, samples);
  uint16_t len_words = (uint16_t)((samples + 1u) / 2u);

  printf("[PIAMISOUND] ch=%d addr=0x%06X samples=%zu len_words=%u period=%u vol=%u freq=%.1fHz rate=%uHz PAL=%d\n",
         ch, addr & CHIP_ADDR_MASK, samples, len_words, period, vol, freq_hz, rate_hz, is_pal);

  start_channel(ch, addr, len_words, period, vol);
  sleep_seconds((double)samples / (double)rate_hz);
  stop_channel(ch);

  free(buf);
  return 0;
}
