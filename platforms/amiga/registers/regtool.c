// SPDX-License-Identifier: MIT
// Simple Amiga register peek/poke harness for PiStorm bus access.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpio/ps_protocol.h"
#include "paula.h"
#include "cia.h"

// ps_protocol.c expects this symbol from the emulator core.
void m68k_set_irq(unsigned int level) {
  (void)level;
}

// Local DMAF bits (shared DMACON register; see agnus/paula docs).
#define DMAF_SETCLR 0x8000
#define DMAF_MASTER 0x0200
#define DMAF_AUD0   0x0001

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [--force] COMMAND [args]\n"
          "\n"
          "Read/write:\n"
          "  --read8  <addr>\n"
          "  --read16 <addr>\n"
          "  --read32 <addr>\n"
          "  --write8  <addr> <value>\n"
          "  --write16 <addr> <value>\n"
          "  --write32 <addr> <value>\n"
          "\n"
          "Dump:\n"
          "  --dump <addr> <len> --width <8|16>\n"
          "\n"
          "Audio test (AUD0):\n"
          "  --audio-test [--audio-addr <addr>] [--audio-len <bytes>]\n"
          "               [--audio-period <val>] [--audio-vol <val>]\n"
          "  --audio-stop\n"
          "\n"
          "Disk LED (CIAA port A, active low):\n"
          "  --disk-led <on|off>\n"
          "  --kbd-led <on|off>   (alias; same as --disk-led)\n"
          "\n"
          "Power LED:\n"
          "  --power-led <on|off> (A500 power LED is not software controlled)\n"
          "\n"
          "Notes:\n"
          "- Use --force to allow writes.\n"
          "- CIAA uses odd addresses; CIAB uses even addresses.\n",
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

static void dump_mem(uint32_t addr, uint32_t len, int width) {
  uint32_t i = 0;
  if (width == 16 && (len & 1)) {
    fprintf(stderr, "dump: len must be even for 16-bit width\n");
    exit(1);
  }
  while (i < len) {
    printf("0x%08X:", addr + i);
    if (width == 8) {
      for (int j = 0; j < 16 && i < len; j++, i++) {
        uint8_t v = (uint8_t)ps_read_8(addr + i);
        printf(" %02X", v);
      }
    } else {
      for (int j = 0; j < 8 && i < len; j++, i += 2) {
        uint16_t v = (uint16_t)ps_read_16(addr + i);
        printf(" %04X", v);
      }
    }
    printf("\n");
  }
}

static void audio_test(uint32_t addr, uint32_t len_bytes, uint16_t period, uint16_t vol) {
  uint32_t i;
  uint32_t addr_masked = addr & 0x1FFFFu;
  uint16_t len_words = (uint16_t)((len_bytes + 1u) / 2u);

  // Write a simple square wave sample into chip RAM.
  for (i = 0; i < len_bytes; i++) {
    uint8_t v = (i & 0x20u) ? 0x7F : 0x81;
    ps_write_8(addr + i, v);
  }

  ps_write_16(AUD0LCH, (addr_masked >> 16) & 0x1Fu);
  ps_write_16(AUD0LCL, addr_masked & 0xFFFFu);
  ps_write_16(AUD0LEN, len_words);
  ps_write_16(AUD0PER, period);
  ps_write_16(AUD0VOL, vol);

  // Enable DMA master + AUD0.
  ps_write_16(DMACON, DMAF_SETCLR | DMAF_MASTER | DMAF_AUD0);
}

static void audio_stop(void) {
  // Clear DMA master + AUD0.
  ps_write_16(DMACON, DMAF_MASTER | DMAF_AUD0);
}

static void disk_led(int on) {
  uint8_t ddr = (uint8_t)ps_read_8(CIAADDR_A);
  uint8_t pra = (uint8_t)ps_read_8(CIAAPRA);

  ddr |= CIAA_LED;
  if (on) {
    pra &= (uint8_t)~CIAA_LED;
  } else {
    pra |= CIAA_LED;
  }

  ps_write_8(CIAADDR_A, ddr);
  ps_write_8(CIAAPRA, pra);
}

static void power_led(int on) {
  (void)on;
  fprintf(stderr, "power-led: not software controllable on A500 (no-op)\n");
}

int main(int argc, char **argv) {
  int force = 0;
  int width = 8;
  uint32_t audio_addr = 0x00010000u;
  uint32_t audio_len = 256u;
  uint16_t audio_period = 200u;
  uint16_t audio_vol = 64u;

  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  ps_setup_protocol();

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];

    if (!strcmp(arg, "--force")) {
      force = 1;
      continue;
    }

    if (!strcmp(arg, "--width")) {
      if (i + 1 >= argc) usage(argv[0]);
      width = (int)parse_u32(argv[++i]);
      continue;
    }

    if (!strcmp(arg, "--audio-addr")) {
      if (i + 1 >= argc) usage(argv[0]);
      audio_addr = parse_u32(argv[++i]);
      continue;
    }

    if (!strcmp(arg, "--audio-len")) {
      if (i + 1 >= argc) usage(argv[0]);
      audio_len = parse_u32(argv[++i]);
      continue;
    }

    if (!strcmp(arg, "--audio-period")) {
      if (i + 1 >= argc) usage(argv[0]);
      audio_period = (uint16_t)parse_u32(argv[++i]);
      continue;
    }

    if (!strcmp(arg, "--audio-vol")) {
      if (i + 1 >= argc) usage(argv[0]);
      audio_vol = (uint16_t)parse_u32(argv[++i]);
      continue;
    }

    if (!strcmp(arg, "--read8")) {
      if (i + 1 >= argc) usage(argv[0]);
      uint32_t addr = parse_u32(argv[++i]);
      printf("0x%08X: 0x%02X\n", addr, (unsigned)ps_read_8(addr) & 0xFFu);
      return 0;
    }

    if (!strcmp(arg, "--read16")) {
      if (i + 1 >= argc) usage(argv[0]);
      uint32_t addr = parse_u32(argv[++i]);
      printf("0x%08X: 0x%04X\n", addr, (unsigned)ps_read_16(addr) & 0xFFFFu);
      return 0;
    }

    if (!strcmp(arg, "--read32")) {
      if (i + 1 >= argc) usage(argv[0]);
      uint32_t addr = parse_u32(argv[++i]);
      printf("0x%08X: 0x%08X\n", addr, ps_read_32(addr));
      return 0;
    }

    if (!strcmp(arg, "--write8")) {
      if (i + 2 >= argc) usage(argv[0]);
      if (!force) {
        fprintf(stderr, "write8 requires --force\n");
        return 1;
      }
      uint32_t addr = parse_u32(argv[++i]);
      uint32_t val = parse_u32(argv[++i]);
      ps_write_8(addr, val & 0xFFu);
      return 0;
    }

    if (!strcmp(arg, "--write16")) {
      if (i + 2 >= argc) usage(argv[0]);
      if (!force) {
        fprintf(stderr, "write16 requires --force\n");
        return 1;
      }
      uint32_t addr = parse_u32(argv[++i]);
      uint32_t val = parse_u32(argv[++i]);
      ps_write_16(addr, val & 0xFFFFu);
      return 0;
    }

    if (!strcmp(arg, "--write32")) {
      if (i + 2 >= argc) usage(argv[0]);
      if (!force) {
        fprintf(stderr, "write32 requires --force\n");
        return 1;
      }
      uint32_t addr = parse_u32(argv[++i]);
      uint32_t val = parse_u32(argv[++i]);
      ps_write_32(addr, val);
      return 0;
    }

    if (!strcmp(arg, "--dump")) {
      if (i + 2 >= argc) usage(argv[0]);
      uint32_t addr = parse_u32(argv[++i]);
      uint32_t len = parse_u32(argv[++i]);
      if (width != 8 && width != 16) {
        fprintf(stderr, "--width must be 8 or 16\n");
        return 1;
      }
      dump_mem(addr, len, width);
      return 0;
    }

    if (!strcmp(arg, "--audio-test")) {
      if (!force) {
        fprintf(stderr, "audio-test requires --force\n");
        return 1;
      }
      audio_test(audio_addr, audio_len, audio_period, audio_vol);
      return 0;
    }

    if (!strcmp(arg, "--audio-stop")) {
      if (!force) {
        fprintf(stderr, "audio-stop requires --force\n");
        return 1;
      }
      audio_stop();
      return 0;
    }

    if (!strcmp(arg, "--disk-led")) {
      if (i + 1 >= argc) usage(argv[0]);
      if (!force) {
        fprintf(stderr, "disk-led requires --force\n");
        return 1;
      }
      const char *mode = argv[++i];
      if (!strcmp(mode, "on")) {
        disk_led(1);
      } else if (!strcmp(mode, "off")) {
        disk_led(0);
      } else {
        fprintf(stderr, "disk-led expects on|off\n");
        return 1;
      }
      return 0;
    }

    if (!strcmp(arg, "--kbd-led")) {
      if (i + 1 >= argc) usage(argv[0]);
      if (!force) {
        fprintf(stderr, "kbd-led requires --force\n");
        return 1;
      }
      const char *mode = argv[++i];
      fprintf(stderr, "kbd-led is an alias for disk-led on A500\n");
      if (!strcmp(mode, "on")) {
        disk_led(1);
      } else if (!strcmp(mode, "off")) {
        disk_led(0);
      } else {
        fprintf(stderr, "kbd-led expects on|off\n");
        return 1;
      }
      return 0;
    }

    if (!strcmp(arg, "--power-led")) {
      if (i + 1 >= argc) usage(argv[0]);
      if (!force) {
        fprintf(stderr, "power-led requires --force\n");
        return 1;
      }
      const char *mode = argv[++i];
      if (!strcmp(mode, "on") || !strcmp(mode, "off")) {
        power_led(!strcmp(mode, "on"));
      } else {
        fprintf(stderr, "power-led expects on|off\n");
        return 1;
      }
      return 0;
    }

    usage(argv[0]);
    return 1;
  }

  usage(argv[0]);
  return 1;
}
