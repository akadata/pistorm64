// SPDX-License-Identifier: MIT
// Pi-side Paula DMA audio harness (raw sample playback).
// MOD replay engine will be layered on top of this.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include "gpio/ps_protocol.h"
#include "paula.h"

// ps_protocol.c expects this symbol from the emulator core.
void m68k_set_irq(unsigned int level) {
  (void)level;
}

#define DMAF_SETCLR 0x8000
#define DMAF_MASTER 0x0200
#define DMAF_AUD0   0x0001
#define CHIP_ADDR_MASK 0x0FFFFFu

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
          "Built-in tune:\n"
          "  --saints            Play \"When the Saints\" on AUD0\n"
          "  --tempo <bpm>       Tune tempo (default 180)\n"
          "  --gate <0.0-1.0>    Note gate ratio (default 0.70)\n"
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
  uint32_t addr_masked = addr & CHIP_ADDR_MASK;
  size_t len_words_full = (len + 1u) / 2u;
  if (len_words_full > 0xFFFFu) {
    fprintf(stderr, "Sample too long for AUD0LEN (%zu words). Trim or split.\n", len_words_full);
    len_words_full = 0xFFFFu;
    len = len_words_full * 2u;
  }
  uint16_t len_words = (uint16_t)len_words_full;

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

static double paula_clock_hz(int is_pal) {
  return is_pal ? 3546895.0 : 3579545.0;
}

static void gen_sine(uint8_t *dst, size_t samples, double freq_hz, double rate_hz) {
  const double two_pi = 6.283185307179586;
  size_t attack = (size_t)(rate_hz * 0.01);
  size_t release = (size_t)(rate_hz * 0.03);
  if (attack < 1) attack = 1;
  if (release < 1) release = 1;
  if (attack + release > samples) {
    attack = samples / 4;
    release = samples / 4;
  }

  for (size_t i = 0; i < samples; i++) {
    double amp = 1.0;
    if (i < attack) {
      amp = (double)i / (double)attack;
    } else if (i + release > samples) {
      amp = (double)(samples - i) / (double)release;
    }
    double s = sin(two_pi * freq_hz * ((double)i / rate_hz));
    int v = 128 + (int)(amp * 127.0 * s);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    dst[i] = (uint8_t)v;
  }
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

static void audio_program_note(uint32_t addr, const uint8_t *buf, size_t len,
                               uint16_t period, uint16_t vol) {
  uint32_t addr_masked = addr & CHIP_ADDR_MASK;
  size_t len_words_full = (len + 1u) / 2u;
  if (len_words_full > 0xFFFFu) {
    len_words_full = 0xFFFFu;
    len = len_words_full * 2u;
  }
  uint16_t len_words = (uint16_t)len_words_full;

  write_chip_ram(addr, buf, len);
  ps_write_16(AUD0VOL, 0);
  ps_write_16(DMACON, DMAF_MASTER | DMAF_AUD0);
  ps_write_16(AUD0LCH, (addr_masked >> 16) & 0x1Fu);
  ps_write_16(AUD0LCL, addr_masked & 0xFFFFu);
  ps_write_16(AUD0LEN, len_words);
  ps_write_16(AUD0PER, period);
  ps_write_16(AUD0VOL, vol);
  ps_write_16(DMACON, DMAF_SETCLR | DMAF_MASTER | DMAF_AUD0);
}

static int play_saints_song(uint32_t addr, double rate_hz, unsigned bpm,
                            uint16_t period, uint16_t vol, double gate_ratio) {
  struct note {
    double freq;
    double beats;
  };
  const double C4 = 261.63;
  const double D4 = 293.66;
  const double E4 = 329.63;
  const double F4 = 349.23;
  const double G4 = 392.00;

  const struct note seq[] = {
    {C4,1},{E4,1},{F4,1},{G4,3},{0,1},
    {C4,1},{E4,1},{F4,1},{G4,3},{0,1},
    {C4,1},{E4,1},{F4,1},{G4,1},{E4,1},{C4,1},{E4,1},{D4,2},
    {E4,1},{E4,1},{D4,1},{C4,1},{E4,1},{G4,2},{F4,1},
    {F4,1},{E4,1},{F4,1},{G4,1},{E4,1},{C4,1},{D4,1},{C4,2},
  };

  const double beat_sec = 60.0 / (double)bpm;
  const size_t max_samples = 0xFFFFu * 2u;
  uint8_t *buf = (uint8_t *)malloc(max_samples);
  if (!buf) return -1;

  for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
    if (stop_requested) break;
    double step_sec = seq[i].beats * beat_sec;
    if (seq[i].freq <= 0.0) {
      ps_write_16(AUD0VOL, 0);
      sleep_seconds(step_sec);
      continue;
    }

    if (gate_ratio < 0.05) gate_ratio = 0.05;
    if (gate_ratio > 0.95) gate_ratio = 0.95;
    double gate_sec = step_sec * gate_ratio;
    double rest_sec = step_sec - gate_sec;
    size_t remaining = (size_t)(gate_sec * rate_hz);
    if (remaining < 16) remaining = 16;

    while (remaining > 0 && !stop_requested) {
      size_t chunk = remaining > max_samples ? max_samples : remaining;
      gen_sine(buf, chunk, seq[i].freq, rate_hz);
      audio_program_note(addr, buf, chunk, period, vol);
      sleep_seconds((double)chunk / rate_hz);
      remaining -= chunk;
    }
    ps_write_16(AUD0VOL, 0);
    sleep_seconds(rest_sec);
  }

  audio_stop();
  free(buf);
  return 0;
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
  int play_saints_flag = 0;
  unsigned tempo = 180;
  double gate_ratio = 0.70;

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
    if (!strcmp(arg, "--saints")) {
      play_saints_flag = 1;
      continue;
    }
    if (!strcmp(arg, "--tempo")) {
      if (i + 1 >= argc) usage(argv[0]);
      tempo = (unsigned)parse_u32(argv[++i]);
      if (tempo < 1) tempo = 1;
      if (tempo > 600) tempo = 600;
      continue;
    }
    if (!strcmp(arg, "--gate")) {
      if (i + 1 >= argc) usage(argv[0]);
      gate_ratio = strtod(argv[++i], NULL);
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

  if (play_saints_flag) {
    if (raw_path || mod_path) {
      fprintf(stderr, "--saints cannot be combined with --raw/--mod\n");
      return 1;
    }
    double clock = paula_clock_hz(is_pal);
    double rate_hz = clock / (double)period;
    uint32_t addr_masked = addr & CHIP_ADDR_MASK;
    printf("[SAINTS] addr=0x%06X rate=%.1fHz period=%u vol=%u bpm=%u gate=%.2f PAL=%d\n",
           addr_masked, rate_hz, period, vol, tempo, gate_ratio, is_pal);
    if (play_saints_song(addr, rate_hz, tempo, period, vol, gate_ratio) != 0) {
      fprintf(stderr, "Failed to play tune.\n");
      return 1;
    }
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
