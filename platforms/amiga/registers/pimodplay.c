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
#include <ctype.h>

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
#define MOD_MAX_SAMPLES 31
#define MOD_CHANNELS 4

static const uint32_t AUD_LCH[MOD_CHANNELS] = {AUD0LCH, AUD1LCH, AUD2LCH, AUD3LCH};
static const uint32_t AUD_LCL[MOD_CHANNELS] = {AUD0LCL, AUD1LCL, AUD2LCL, AUD3LCL};
static const uint32_t AUD_LEN[MOD_CHANNELS] = {AUD0LEN, AUD1LEN, AUD2LEN, AUD3LEN};
static const uint32_t AUD_PER[MOD_CHANNELS] = {AUD0PER, AUD1PER, AUD2PER, AUD3PER};
static const uint32_t AUD_VOL[MOD_CHANNELS] = {AUD0VOL, AUD1VOL, AUD2VOL, AUD3VOL};
static const uint16_t AUD_DMA_MASK[MOD_CHANNELS] = {0x0001, 0x0002, 0x0004, 0x0008};

static double paula_clock_hz(int is_pal);
static void sleep_seconds(double seconds);
static void audio_program_note(uint32_t addr, const uint8_t *buf, size_t len,
                               uint16_t period, uint16_t vol);
static void audio_program_note_ch(int ch, uint32_t addr, const uint8_t *buf,
                                  size_t len, uint16_t period, uint16_t vol);
static void apply_lpf_mono(int8_t *data, size_t samples, double rate_hz,
                           double cutoff_hz);
static void apply_lpf_stereo(int8_t *data, size_t frames, double rate_hz,
                             double cutoff_hz);

static volatile sig_atomic_t stop_requested = 0;

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [--raw <file> | --mod <file>] [options]\n"
          "\n"
          "Raw sample playback (AUD0 DMA):\n"
          "  --raw <file>        8-bit unsigned sample data\n"
          "  --wav <file>        WAV PCM mono/stereo (8/16-bit)\n"
          "  --addr <hex>        Chip RAM load address (default 0x00080000)\n"
          "  --period <val>      Paula period (default 200)\n"
          "  --rate <hz>         Sample rate (overrides --period)\n"
          "  --vol <0-64>        Volume (default 64)\n"
          "  --seconds <n>       Playback duration (default 5)\n"
          "  --stream            Stream long samples in chunks\n"
          "  --chunk-bytes <n>   Stream chunk size (default 131070 bytes)\n"
          "  --u8                Raw input is unsigned 8-bit (default for --raw)\n"
          "  --s8                Raw input is signed 8-bit\n"
          "  --stereo            Raw/WAV is stereo interleaved (AUD0/AUD1)\n"
          "  --mono              Force mono (downmix if WAV is stereo)\n"
          "  --lpf <hz>          Apply low-pass filter (Hz)\n"
          "\n"
          "Built-in tune:\n"
          "  --saints            Play \"When the Saints\" on AUD0\n"
          "  --tempo <bpm>       Tune tempo (default 180)\n"
          "  --gate <0.0-1.0>    Note gate ratio (default 0.70)\n"
          "\n"
          "MOD playback (basic):\n"
          "  --mod <file>        ProTracker MOD (4-channel, basic effects)\n"
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

static uint16_t period_from_rate(double rate_hz, int is_pal) {
  double clock = paula_clock_hz(is_pal);
  double period = clock / rate_hz;
  if (period < 1.0) period = 1.0;
  if (period > 65535.0) period = 65535.0;
  return (uint16_t)(period + 0.5);
}

static uint8_t *read_wav_s8(const char *path, size_t *out_len,
                            unsigned *out_rate_hz, int *out_channels,
                            int force_mono) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror(path);
    return NULL;
  }

  char riff[4];
  if (fread(riff, 1, 4, f) != 4 || memcmp(riff, "RIFF", 4) != 0) {
    fclose(f);
    return NULL;
  }
  if (fseek(f, 4, SEEK_CUR) != 0) {
    fclose(f);
    return NULL;
  }
  char wave[4];
  if (fread(wave, 1, 4, f) != 4 || memcmp(wave, "WAVE", 4) != 0) {
    fclose(f);
    return NULL;
  }

  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint32_t sample_rate = 0;
  uint16_t bits_per_sample = 0;
  long data_offset = -1;
  uint32_t data_size = 0;

  while (1) {
    char chunk_id[4];
    uint32_t chunk_size = 0;
    if (fread(chunk_id, 1, 4, f) != 4) break;
    if (fread(&chunk_size, 4, 1, f) != 1) break;

    if (!memcmp(chunk_id, "fmt ", 4)) {
      uint16_t fmt_tag = 0;
      uint16_t fmt_channels = 0;
      uint32_t fmt_rate = 0;
      uint16_t fmt_bps = 0;
      if (fread(&fmt_tag, sizeof(fmt_tag), 1, f) != 1 ||
          fread(&fmt_channels, sizeof(fmt_channels), 1, f) != 1 ||
          fread(&fmt_rate, sizeof(fmt_rate), 1, f) != 1) {
        fclose(f);
        return NULL;
      }
      if (fseek(f, 6, SEEK_CUR) != 0) {
        fclose(f);
        return NULL;
      }
      if (fread(&fmt_bps, sizeof(fmt_bps), 1, f) != 1) {
        fclose(f);
        return NULL;
      }
      audio_format = fmt_tag;
      channels = fmt_channels;
      sample_rate = fmt_rate;
      bits_per_sample = fmt_bps;
      long remain = (long)chunk_size - 16;
      if (remain > 0) fseek(f, remain, SEEK_CUR);
    } else if (!memcmp(chunk_id, "data", 4)) {
      data_offset = ftell(f);
      data_size = chunk_size;
      fseek(f, chunk_size, SEEK_CUR);
    } else {
      fseek(f, chunk_size, SEEK_CUR);
    }
    if (data_offset >= 0 && sample_rate != 0) break;
  }

  if (audio_format != 1 || sample_rate == 0 ||
      (bits_per_sample != 8 && bits_per_sample != 16) ||
      data_offset < 0 || data_size == 0 ||
      (channels != 1 && channels != 2)) {
    fclose(f);
    return NULL;
  }

  if (fseek(f, data_offset, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  uint8_t *out = NULL;
  size_t frames = 0;
  if (bits_per_sample == 8) {
    frames = data_size / channels;
    size_t out_samples = force_mono ? frames : frames * channels;
    out = (uint8_t *)malloc(out_samples);
    if (!out) {
      fclose(f);
      return NULL;
    }
    if (fread(out, 1, data_size, f) != data_size) {
      fclose(f);
      free(out);
      return NULL;
    }
    if (channels == 2 && force_mono) {
      for (size_t i = 0; i < frames; i++) {
        int l = (int)out[i * 2] - 128;
        int r = (int)out[i * 2 + 1] - 128;
        int v = (l + r) / 2;
        out[i] = (uint8_t)(int8_t)v;
      }
      *out_len = frames;
    } else {
      for (size_t i = 0; i < data_size; i++) {
        out[i] = (uint8_t)((int)out[i] - 128);
      }
      *out_len = data_size;
    }
  } else {
    frames = data_size / (2u * channels);
    size_t out_samples = force_mono ? frames : frames * channels;
    out = (uint8_t *)malloc(out_samples);
    if (!out) {
      fclose(f);
      return NULL;
    }
    if (channels == 1) {
      for (size_t i = 0; i < frames; i++) {
        int16_t s = 0;
        if (fread(&s, sizeof(s), 1, f) != 1) {
          fclose(f);
          free(out);
          return NULL;
        }
        int v = s / 256;
        if (v < -128) v = -128;
        if (v > 127) v = 127;
        out[i] = (uint8_t)(int8_t)v;
      }
      *out_len = frames;
    } else if (force_mono) {
      for (size_t i = 0; i < frames; i++) {
        int16_t l = 0;
        int16_t r = 0;
        if (fread(&l, sizeof(l), 1, f) != 1 ||
            fread(&r, sizeof(r), 1, f) != 1) {
          fclose(f);
          free(out);
          return NULL;
        }
        int v = ((int)l + (int)r) / 512;
        if (v < -128) v = -128;
        if (v > 127) v = 127;
        out[i] = (uint8_t)(int8_t)v;
      }
      *out_len = frames;
    } else {
      for (size_t i = 0; i < frames * 2u; i++) {
        int16_t s = 0;
        if (fread(&s, sizeof(s), 1, f) != 1) {
          fclose(f);
          free(out);
          return NULL;
        }
        int v = s / 256;
        if (v < -128) v = -128;
        if (v > 127) v = 127;
        out[i] = (uint8_t)(int8_t)v;
      }
      *out_len = frames * 2u;
    }
  }

  fclose(f);
  *out_rate_hz = sample_rate;
  *out_channels = force_mono ? 1 : channels;
  return out;
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

static void audio_stop_all(void) {
  ps_write_16(AUD0VOL, 0);
  ps_write_16(AUD1VOL, 0);
  ps_write_16(AUD2VOL, 0);
  ps_write_16(AUD3VOL, 0);
  ps_write_16(DMACON, DMAF_MASTER | DMAF_AUD0 | 0x0002 | 0x0004 | 0x0008);
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

static void audio_play_stream(uint32_t addr, const uint8_t *buf, size_t len,
                              uint16_t period, uint16_t vol,
                              double rate_hz, unsigned seconds,
                              size_t chunk_bytes) {
  uint32_t addr_masked = addr & CHIP_ADDR_MASK;
  size_t max_chunk = 0xFFFFu * 2u;
  if (chunk_bytes == 0 || chunk_bytes > max_chunk) chunk_bytes = max_chunk;
  if (rate_hz <= 0.0) rate_hz = 1.0;

  size_t offset = 0;
  double elapsed = 0.0;
  while (offset < len && !stop_requested) {
    size_t chunk = len - offset;
    if (chunk > chunk_bytes) chunk = chunk_bytes;
    if (chunk < 2) break;

    audio_program_note(addr_masked, buf + offset, chunk, period, vol);

    double chunk_sec = (double)chunk / rate_hz;
    if (seconds > 0 && elapsed + chunk_sec > (double)seconds) {
      chunk_sec = (double)seconds - elapsed;
      if (chunk_sec <= 0.0) break;
    }
    sleep_seconds(chunk_sec);
    elapsed += chunk_sec;
    offset += chunk;

    if (seconds > 0 && elapsed >= (double)seconds) break;
  }
  audio_stop();
}

static void audio_play_stream_stereo(uint32_t addr, const uint8_t *buf, size_t len,
                                     uint16_t period, uint16_t vol,
                                     double rate_hz, unsigned seconds,
                                     size_t chunk_bytes) {
  uint32_t addr_l = addr & CHIP_ADDR_MASK;
  uint32_t addr_r = (addr + 0x20000u) & CHIP_ADDR_MASK;
  size_t max_chunk = 0xFFFFu * 2u;
  if (chunk_bytes == 0 || chunk_bytes > max_chunk) chunk_bytes = max_chunk;
  if (rate_hz <= 0.0) rate_hz = 1.0;
  if (len < 2) return;
  if (addr_r + chunk_bytes > 0x200000u) {
    fprintf(stderr, "Stereo addr range exceeds 2MB chip RAM; use --addr lower.\n");
    return;
  }

  size_t total_frames = len / 2u;
  size_t offset_frames = 0;
  double elapsed = 0.0;
  uint8_t *left = (uint8_t *)malloc(chunk_bytes);
  uint8_t *right = (uint8_t *)malloc(chunk_bytes);
  if (!left || !right) {
    free(left);
    free(right);
    return;
  }

  size_t chunk_frames = chunk_bytes;
  while (offset_frames < total_frames && !stop_requested) {
    size_t frames = total_frames - offset_frames;
    if (frames > chunk_frames) frames = chunk_frames;
    if (frames < 2) break;

    for (size_t i = 0; i < frames; i++) {
      left[i] = buf[(offset_frames + i) * 2u + 0];
      right[i] = buf[(offset_frames + i) * 2u + 1];
    }

    audio_program_note_ch(0, addr_l, left, frames, period, vol);
    audio_program_note_ch(1, addr_r, right, frames, period, vol);

    double chunk_sec = (double)frames / rate_hz;
    if (seconds > 0 && elapsed + chunk_sec > (double)seconds) {
      chunk_sec = (double)seconds - elapsed;
      if (chunk_sec <= 0.0) break;
    }
    sleep_seconds(chunk_sec);
    elapsed += chunk_sec;
    offset_frames += frames;

    if (seconds > 0 && elapsed >= (double)seconds) break;
  }

  audio_stop_all();
  free(left);
  free(right);
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

static void apply_lpf_mono(int8_t *data, size_t samples, double rate_hz,
                           double cutoff_hz) {
  if (!data || samples == 0 || cutoff_hz <= 0.0 || rate_hz <= 0.0) return;
  double x = 2.0 * 3.141592653589793 * cutoff_hz;
  double alpha = x / (x + rate_hz);
  double y = 0.0;
  for (size_t i = 0; i < samples; i++) {
    double in = (double)data[i];
    y = y + alpha * (in - y);
    int v = (int)lrint(y);
    if (v < -128) v = -128;
    if (v > 127) v = 127;
    data[i] = (int8_t)v;
  }
}

static void apply_lpf_stereo(int8_t *data, size_t frames, double rate_hz,
                             double cutoff_hz) {
  if (!data || frames == 0 || cutoff_hz <= 0.0 || rate_hz <= 0.0) return;
  double x = 2.0 * 3.141592653589793 * cutoff_hz;
  double alpha = x / (x + rate_hz);
  double yl = 0.0;
  double yr = 0.0;
  for (size_t i = 0; i < frames; i++) {
    double in_l = (double)data[i * 2u + 0];
    double in_r = (double)data[i * 2u + 1];
    yl = yl + alpha * (in_l - yl);
    yr = yr + alpha * (in_r - yr);
    int vl = (int)lrint(yl);
    int vr = (int)lrint(yr);
    if (vl < -128) vl = -128;
    if (vl > 127) vl = 127;
    if (vr < -128) vr = -128;
    if (vr > 127) vr = 127;
    data[i * 2u + 0] = (int8_t)vl;
    data[i * 2u + 1] = (int8_t)vr;
  }
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

static void audio_program_note_ch(int ch, uint32_t addr, const uint8_t *buf,
                                  size_t len, uint16_t period, uint16_t vol) {
  uint32_t addr_masked = addr & CHIP_ADDR_MASK;
  size_t len_words_full = (len + 1u) / 2u;
  if (len_words_full > 0xFFFFu) {
    len_words_full = 0xFFFFu;
    len = len_words_full * 2u;
  }
  uint16_t len_words = (uint16_t)len_words_full;

  write_chip_ram(addr, buf, len);
  ps_write_16(AUD_VOL[ch], 0);
  ps_write_16(AUD_LCH[ch], (addr_masked >> 16) & 0x1Fu);
  ps_write_16(AUD_LCL[ch], addr_masked & 0xFFFFu);
  ps_write_16(AUD_LEN[ch], len_words);
  ps_write_16(AUD_PER[ch], period);
  ps_write_16(AUD_VOL[ch], vol);
  ps_write_16(DMACON, DMAF_SETCLR | DMAF_MASTER | AUD_DMA_MASK[ch]);
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

typedef struct {
  char name[22];
  uint32_t length_bytes;
  int8_t finetune;
  uint8_t volume;
  uint32_t loop_start_bytes;
  uint32_t loop_len_bytes;
  uint32_t chip_addr;
  const uint8_t *data;
} mod_sample_t;

typedef struct {
  uint8_t sample;
  uint16_t period;
  uint8_t effect;
  uint8_t param;
} mod_event_t;

typedef struct {
  char name[21];
  uint8_t song_len;
  uint8_t restart;
  uint8_t orders[128];
  uint8_t num_patterns;
  mod_sample_t samples[MOD_MAX_SAMPLES];
  mod_event_t *patterns;
  uint8_t *data;
  size_t data_len;
  size_t pattern_offset;
  size_t sample_offset;
} mod_file_t;

typedef struct {
  uint8_t sample;
  uint16_t period;
  uint8_t volume;
  uint32_t loop_start_bytes;
  uint32_t loop_len_bytes;
  double rate_hz;
  double time_left;
} mod_chan_t;

static int mod_read_u16(const uint8_t *p) {
  return ((int)p[0] << 8) | (int)p[1];
}

static int mod_is_4ch_tag(const uint8_t *tag) {
  return !memcmp(tag, "M.K.", 4) || !memcmp(tag, "M!K!", 4) ||
         !memcmp(tag, "4CHN", 4) || !memcmp(tag, "FLT4", 4);
}

static int read_mod(const char *path, mod_file_t *out) {
  memset(out, 0, sizeof(*out));
  out->data = read_file(path, &out->data_len);
  if (!out->data || out->data_len < 1084) return -1;

  const uint8_t *p = out->data;
  memcpy(out->name, p, 20);
  out->name[20] = '\0';
  p += 20;

  for (int i = 0; i < MOD_MAX_SAMPLES; i++) {
    mod_sample_t *s = &out->samples[i];
    memcpy(s->name, p, 22);
    p += 22;
    uint16_t len_words = (uint16_t)mod_read_u16(p);
    p += 2;
    uint8_t finetune = *p++;
    uint8_t volume = *p++;
    uint16_t loop_start = (uint16_t)mod_read_u16(p);
    p += 2;
    uint16_t loop_len = (uint16_t)mod_read_u16(p);
    p += 2;

    s->length_bytes = (uint32_t)len_words * 2u;
    s->finetune = (int8_t)(finetune & 0x0F);
    if (s->finetune & 0x08) s->finetune -= 16;
    s->volume = volume > 64 ? 64 : volume;
    s->loop_start_bytes = (uint32_t)loop_start * 2u;
    s->loop_len_bytes = (uint32_t)loop_len * 2u;
  }

  out->song_len = *p++;
  out->restart = *p++;
  memcpy(out->orders, p, 128);
  p += 128;

  if ((size_t)(p - out->data + 4) > out->data_len) return -1;
  if (!mod_is_4ch_tag(p)) {
    fprintf(stderr, "Unsupported MOD tag: %.4s (only 4ch supported)\n", p);
    return -1;
  }
  p += 4;

  uint8_t max_pat = 0;
  for (int i = 0; i < 128; i++) {
    if (out->orders[i] > max_pat) max_pat = out->orders[i];
  }
  out->num_patterns = (uint8_t)(max_pat + 1u);
  out->pattern_offset = 1084;
  out->sample_offset = out->pattern_offset + (size_t)out->num_patterns * 1024u;
  if (out->sample_offset > out->data_len) return -1;

  size_t total_events = (size_t)out->num_patterns * 64u * MOD_CHANNELS;
  out->patterns = (mod_event_t *)calloc(total_events, sizeof(mod_event_t));
  if (!out->patterns) return -1;

  for (uint8_t pat = 0; pat < out->num_patterns; pat++) {
    size_t base = out->pattern_offset + (size_t)pat * 1024u;
    for (int row = 0; row < 64; row++) {
      for (int ch = 0; ch < MOD_CHANNELS; ch++) {
        size_t off = base + (size_t)row * 16u + (size_t)ch * 4u;
        if (off + 3 >= out->data_len) return -1;
        uint8_t b0 = out->data[off + 0];
        uint8_t b1 = out->data[off + 1];
        uint8_t b2 = out->data[off + 2];
        uint8_t b3 = out->data[off + 3];
        mod_event_t *e = &out->patterns[(pat * 64u + row) * MOD_CHANNELS + ch];
        e->sample = (uint8_t)((b0 & 0xF0) | ((b2 & 0xF0) >> 4));
        e->period = (uint16_t)(((b0 & 0x0F) << 8) | b1);
        e->effect = (uint8_t)(b2 & 0x0F);
        e->param = b3;
      }
    }
  }

  size_t sample_pos = out->sample_offset;
  for (int i = 0; i < MOD_MAX_SAMPLES; i++) {
    mod_sample_t *s = &out->samples[i];
    if (s->length_bytes == 0) {
      s->data = NULL;
      continue;
    }
    if (sample_pos + s->length_bytes > out->data_len) return -1;
    s->data = out->data + sample_pos;
    sample_pos += s->length_bytes;
  }
  return 0;
}

static void free_mod(mod_file_t *mod) {
  free(mod->patterns);
  free(mod->data);
  memset(mod, 0, sizeof(*mod));
}

static void program_channel(int ch, uint32_t addr, uint32_t len_bytes,
                            uint16_t period, uint8_t vol) {
  uint32_t addr_masked = addr & CHIP_ADDR_MASK;
  size_t len_words_full = (len_bytes + 1u) / 2u;
  if (len_words_full == 0) return;
  if (len_words_full > 0xFFFFu) len_words_full = 0xFFFFu;
  uint16_t len_words = (uint16_t)len_words_full;

  ps_write_16(AUD_VOL[ch], 0);
  ps_write_16(AUD_LCH[ch], (addr_masked >> 16) & 0x1Fu);
  ps_write_16(AUD_LCL[ch], addr_masked & 0xFFFFu);
  ps_write_16(AUD_LEN[ch], len_words);
  ps_write_16(AUD_PER[ch], period);
  ps_write_16(AUD_VOL[ch], vol);
  ps_write_16(DMACON, DMAF_SETCLR | DMAF_MASTER | AUD_DMA_MASK[ch]);
}

static int play_mod(const char *path, uint32_t base_addr, int is_pal) {
  mod_file_t mod;
  if (read_mod(path, &mod) != 0) {
    fprintf(stderr, "Failed to load MOD: %s\n", path);
    free_mod(&mod);
    return -1;
  }

  uint32_t addr = base_addr & CHIP_ADDR_MASK;
  uint32_t offset = 0;
  for (int i = 0; i < MOD_MAX_SAMPLES; i++) {
    mod_sample_t *s = &mod.samples[i];
    if (!s->data || s->length_bytes == 0) continue;
    s->chip_addr = (addr + offset) & CHIP_ADDR_MASK;
    write_chip_ram(s->chip_addr, s->data, s->length_bytes);
    offset += (s->length_bytes + 1u) & ~1u;
    if ((addr + offset) > 0x200000u) {
      fprintf(stderr, "MOD samples exceed 2MB Chip RAM. Aborting.\n");
      free_mod(&mod);
      return -1;
    }
  }

  unsigned speed = 6;
  unsigned bpm = 125;
  double tick_sec = 2.5 / (double)bpm;
  uint8_t pos = 0;
  uint8_t row = 0;
  unsigned tick = 0;
  mod_chan_t chan[MOD_CHANNELS];
  memset(chan, 0, sizeof(chan));

  printf("[MOD] \"%s\" patterns=%u song_len=%u bpm=%u speed=%u PAL=%d\n",
         mod.name, mod.num_patterns, mod.song_len, bpm, speed, is_pal);

  while (!stop_requested && pos < mod.song_len) {
    uint8_t pat = mod.orders[pos];
    if (pat >= mod.num_patterns) break;
    mod_event_t *row_events = &mod.patterns[(pat * 64u + row) * MOD_CHANNELS];

    if (tick == 0) {
      int jump_pos = -1;
      int break_row = -1;

      for (int ch = 0; ch < MOD_CHANNELS; ch++) {
        mod_event_t *e = &row_events[ch];
        if (e->sample > 0 && e->sample <= MOD_MAX_SAMPLES) {
          chan[ch].sample = e->sample;
          mod_sample_t *s = &mod.samples[e->sample - 1];
          chan[ch].volume = s->volume;
          chan[ch].loop_start_bytes = s->loop_start_bytes;
          chan[ch].loop_len_bytes = s->loop_len_bytes;
        }

        if (e->period) {
          chan[ch].period = e->period;
          mod_sample_t *s = &mod.samples[(chan[ch].sample ? chan[ch].sample : 1) - 1];
          double rate_hz = paula_clock_hz(is_pal) / (double)e->period;
          chan[ch].rate_hz = rate_hz;
          uint32_t intro_bytes = s->loop_start_bytes ? s->loop_start_bytes : s->length_bytes;
          chan[ch].time_left = rate_hz > 0 ? (double)intro_bytes / rate_hz : 0.0;
          program_channel(ch, s->chip_addr, s->length_bytes, e->period, chan[ch].volume);
        }

        if (e->effect == 0x0C) {
          uint8_t v = e->param > 64 ? 64 : e->param;
          chan[ch].volume = v;
          ps_write_16(AUD_VOL[ch], v);
        } else if (e->effect == 0x0F) {
          if (e->param <= 32 && e->param > 0) {
            speed = e->param;
          } else if (e->param > 32) {
            bpm = e->param;
            tick_sec = 2.5 / (double)bpm;
          }
        } else if (e->effect == 0x0B) {
          jump_pos = e->param;
        } else if (e->effect == 0x0D) {
          break_row = ((e->param >> 4) & 0x0F) * 10 + (e->param & 0x0F);
        }
      }

      if (jump_pos >= 0) {
        pos = (uint8_t)jump_pos;
        row = 0;
        tick = 0;
        continue;
      }
      if (break_row >= 0) {
        pos = (uint8_t)(pos + 1);
        row = (uint8_t)break_row;
        tick = 0;
        continue;
      }
    } else {
      for (int ch = 0; ch < MOD_CHANNELS; ch++) {
        if (chan[ch].loop_len_bytes > 2 && chan[ch].rate_hz > 0.0) {
          chan[ch].time_left -= tick_sec;
          if (chan[ch].time_left <= 0.0) {
            mod_sample_t *s = &mod.samples[(chan[ch].sample ? chan[ch].sample : 1) - 1];
            program_channel(ch,
                            s->chip_addr + chan[ch].loop_start_bytes,
                            chan[ch].loop_len_bytes,
                            chan[ch].period,
                            chan[ch].volume);
            chan[ch].time_left = (double)chan[ch].loop_len_bytes / chan[ch].rate_hz;
          }
        }
      }
    }

    sleep_seconds(tick_sec);
    tick++;
    if (tick >= speed) {
      tick = 0;
      row++;
      if (row >= 64) {
        row = 0;
        pos++;
      }
    }
  }

  audio_stop_all();
  free_mod(&mod);
  return 0;
}

int main(int argc, char **argv) {
  const char *raw_path = NULL;
  const char *wav_path = NULL;
  const char *mod_path = NULL;
  uint32_t addr = 0x00080000u;
  uint16_t period = 200u;
  unsigned rate_hz = 0;
  uint16_t vol = 64u;
  unsigned seconds = 5;
  int seconds_set = 0;
  int stop_only = 0;
  int is_pal = 1;
  int play_saints_flag = 0;
  unsigned tempo = 180;
  double gate_ratio = 0.70;
  int stream = 0;
  size_t chunk_bytes = 0;
  int raw_unsigned = 1;
  int stereo = 0;
  int force_mono = 0;
  double lpf_hz = 0.0;

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
    if (!strcmp(arg, "--wav")) {
      if (i + 1 >= argc) usage(argv[0]);
      wav_path = argv[++i];
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
    if (!strcmp(arg, "--rate")) {
      if (i + 1 >= argc) usage(argv[0]);
      rate_hz = (unsigned)parse_u32(argv[++i]);
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
      seconds_set = 1;
      continue;
    }
    if (!strcmp(arg, "--stream")) {
      stream = 1;
      continue;
    }
    if (!strcmp(arg, "--chunk-bytes")) {
      if (i + 1 >= argc) usage(argv[0]);
      chunk_bytes = (size_t)parse_u32(argv[++i]);
      continue;
    }
    if (!strcmp(arg, "--u8")) {
      raw_unsigned = 1;
      continue;
    }
    if (!strcmp(arg, "--s8")) {
      raw_unsigned = 0;
      continue;
    }
    if (!strcmp(arg, "--stereo")) {
      stereo = 1;
      continue;
    }
    if (!strcmp(arg, "--mono")) {
      force_mono = 1;
      continue;
    }
    if (!strcmp(arg, "--lpf")) {
      if (i + 1 >= argc) usage(argv[0]);
      lpf_hz = strtod(argv[++i], NULL);
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

  if (mod_path && (raw_path || wav_path)) {
    fprintf(stderr, "--mod cannot be combined with --raw/--wav\n");
    return 1;
  }

  if (mod_path) {
    int rc = play_mod(mod_path, addr, is_pal);
    return rc == 0 ? 0 : 1;
  }

  if (!raw_path && !wav_path) {
    usage(argv[0]);
    return 1;
  }

  size_t len = 0;
  uint8_t *buf = NULL;
  int channels = 1;
  if (wav_path) {
    unsigned wav_rate = 0;
    buf = read_wav_s8(wav_path, &len, &wav_rate, &channels, force_mono);
    if (!buf || len == 0) {
      fprintf(stderr, "Failed to read WAV: %s\n", wav_path);
      free(buf);
      return 1;
    }
    if (rate_hz == 0) rate_hz = wav_rate;
    if (channels == 2 && force_mono) channels = 1;
    if (channels == 2 && !stereo) {
      fprintf(stderr, "WAV is stereo; pass --stereo or --mono.\n");
      free(buf);
      return 1;
    }
  } else {
    buf = read_file(raw_path, &len);
    if (buf && raw_unsigned) {
      for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)((int)buf[i] - 128);
      }
    }
    if (stereo) channels = 2;
  }
  if (!buf || len == 0) {
    fprintf(stderr, "Failed to read sample.\n");
    free(buf);
    return 1;
  }
  if (channels == 2 && (len % 2u) != 0u) {
    fprintf(stderr, "Stereo input must be even length (interleaved LR).\n");
    free(buf);
    return 1;
  }

  if (rate_hz) {
    period = period_from_rate((double)rate_hz, is_pal);
  } else {
    rate_hz = (unsigned)(paula_clock_hz(is_pal) / (double)period + 0.5);
  }
  if (stream) {
    if (!seconds_set) seconds = 0;
    size_t effective_chunk = chunk_bytes ? chunk_bytes : 0xFFFFu * 2u;
    if (lpf_hz > 0.0) {
      if (channels == 2) {
        apply_lpf_stereo((int8_t *)buf, len / 2u, (double)rate_hz, lpf_hz);
      } else {
        apply_lpf_mono((int8_t *)buf, len, (double)rate_hz, lpf_hz);
      }
    }
    if (channels == 2) {
      printf("[STREAM] stereo addr=0x%06X bytes=%zu period=%u vol=%u rate=%uHz seconds=%u chunk=%zu PAL=%d\n",
             addr & 0x1FFFFu, len, period, vol, rate_hz, seconds, effective_chunk, is_pal);
      audio_play_stream_stereo(addr, buf, len, period, vol, (double)rate_hz, seconds, chunk_bytes);
    } else {
      printf("[STREAM] addr=0x%06X bytes=%zu period=%u vol=%u rate=%uHz seconds=%u chunk=%zu PAL=%d\n",
             addr & 0x1FFFFu, len, period, vol, rate_hz, seconds, effective_chunk, is_pal);
      audio_play_stream(addr, buf, len, period, vol, (double)rate_hz, seconds, chunk_bytes);
    }
  } else {
    if (lpf_hz > 0.0) {
      if (channels == 2) {
        apply_lpf_stereo((int8_t *)buf, len / 2u, (double)rate_hz, lpf_hz);
      } else {
        apply_lpf_mono((int8_t *)buf, len, (double)rate_hz, lpf_hz);
      }
    }
    if (channels == 2) {
      fprintf(stderr, "Stereo requires --stream (AUD0/AUD1).\n");
      free(buf);
      return 1;
    }
    printf("[RAW] addr=0x%06X bytes=%zu period=%u vol=%u seconds=%u PAL=%d\n",
           addr & 0x1FFFFu, len, period, vol, seconds, is_pal);
    audio_play_raw(addr, buf, len, period, vol, seconds);
  }
  free(buf);
  return 0;
}
