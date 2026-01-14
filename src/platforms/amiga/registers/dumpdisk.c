// SPDX-License-Identifier: MIT
// Dump a floppy disk to a raw track file from the Pi side.
// NOTE: This reads raw MFM track data via Paula disk DMA; output is raw track dumps,
// not decoded ADF. Further MFM decode is required to produce sector data.

#define _XOPEN_SOURCE 600
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "src/gpio/ps_protocol.h"
#include "amiga_custom_chips.h"
#include "paula.h"
#include "agnus.h"
#include "cia.h"

// ps_protocol.c expects this symbol; we don't drive a CPU here.
void m68k_set_irq(unsigned int level) { (void)level; }

// Paula disk registers
#define DSK_SYNC_WORD 0x4489

// Raw track parameters (standard DD ~0x1A00 bytes per track per side)
#define TRACK_RAW_BYTES 0x1A00u
#define TRACK_RAW_WORDS (TRACK_RAW_BYTES / 2u)
#define CHIP_BUF_ADDR   0x00040000u  // chip RAM buffer for DMA

static volatile sig_atomic_t stop_requested = 0;

static void on_sigint(int signo) {
  (void)signo;
  stop_requested = 1;
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

// CIAB helpers ---------------------------------------------------------------
static void ensure_output(uint32_t ddr_addr, uint8_t mask) {
  uint8_t ddr = (uint8_t)ps_read_8(ddr_addr);
  if ((ddr & mask) != mask) {
    ddr |= mask;
    ps_write_8(ddr_addr, ddr);
  }
}

static void motor_on(void) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKMOTOR);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  prb |= CIAB_DSKMOTOR;  // active high
  ps_write_8(CIABPRB, prb);
}

static void motor_off(void) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKMOTOR);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  prb &= (uint8_t)~CIAB_DSKMOTOR;
  ps_write_8(CIABPRB, prb);
}

static void select_drive(int drive) {
  if (drive < 0 || drive > 3) {
    fprintf(stderr, "drive must be 0-3\n");
    exit(1);
  }
  uint8_t mask = CIAB_DSKSEL0 | CIAB_DSKSEL1 | CIAB_DSKSEL2 | CIAB_DSKSEL3;
  uint8_t sel_bit = (uint8_t)(CIAB_DSKSEL0 << drive);
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, mask);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  prb |= mask;           // deassert all (active low)
  prb &= (uint8_t)~sel_bit;  // assert target
  ps_write_8(CIABPRB, prb);
}

static void set_side(int side) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKSIDE);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  if (side) prb |= CIAB_DSKSIDE;
  else prb &= (uint8_t)~CIAB_DSKSIDE;
  ps_write_8(CIABPRB, prb);
}

static void step_track(int steps, int outwards) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKDIREC | CIAB_DSKSTEP);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  if (outwards) prb |= CIAB_DSKDIREC;
  else prb &= (uint8_t)~CIAB_DSKDIREC;
  ps_write_8(CIABPRB, prb);
  for (int i = 0; i < steps; i++) {
    prb |= CIAB_DSKSTEP;
    ps_write_8(CIABPRB, prb);
    usleep(5000);
    prb &= (uint8_t)~CIAB_DSKSTEP;
    ps_write_8(CIABPRB, prb);
    usleep(5000);
    if (stop_requested) break;
  }
}

// Paula disk DMA -------------------------------------------------------------
static int read_track_raw(uint32_t chip_addr, uint32_t words) {
  // Clear any pending disk interrupt.
  ps_write_16(INTREQ, INTF_DSKBLK);
  // Enable word sync on 0x4489.
  ps_write_16(ADKCON, ADKF_SETCLR | ADKF_MSBSYNC);
  // Program DMA pointer.
  ps_write_16(DSKPTH, (chip_addr >> 16) & 0xFFFFu);
  ps_write_16(DSKPTL, chip_addr & 0xFFFFu);
  // Sync word and length.
  ps_write_16(DSKSYNC, DSK_SYNC_WORD);
  // Enable disk DMA master + disk.
  ps_write_16(DMACON, DMAF_SETCLR | DMAF_MASTER | DMAF_DISK);
  // Kick DMA: bit15 enable, bit14 direction (0 = read), bits 0-13 length words.
  uint16_t len = (uint16_t)(words & 0x3FFFu);
  ps_write_16(DSKLEN, 0x8000u | len);
  // Wait for interrupt or timeout.
  const int max_poll = 1000000;  // ~1s in 1us polls
  for (int i = 0; i < max_poll; i++) {
    if (stop_requested) {
      ps_write_16(DSKLEN, 0);
      ps_write_16(DMACON, DMAF_DISK);
      return -2;
    }
    uint16_t intreq = (uint16_t)ps_read_16(INTREQR);
    if (intreq & INTF_DSKBLK) {
      ps_write_16(INTREQ, INTF_DSKBLK);  // clear
      // Stop DMA.
      ps_write_16(DSKLEN, 0);
      ps_write_16(DMACON, DMAF_DISK);
      return 0;
    }
    usleep(1);
  }
  // Stop DMA on timeout.
  ps_write_16(DSKLEN, 0);
  ps_write_16(DMACON, DMAF_DISK);
  return -1;
}

int main(int argc, char **argv) {
  const char *outfile = "dump.raw";
  int drive = 0;
  int tracks = 80;
  int sides = 2;

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "--out") && i + 1 < argc) {
      outfile = argv[++i];
      continue;
    }
    if (!strcmp(arg, "--drive") && i + 1 < argc) {
      drive = (int)parse_u32(argv[++i]);
      continue;
    }
    if (!strcmp(arg, "--tracks") && i + 1 < argc) {
      tracks = (int)parse_u32(argv[++i]);
      continue;
    }
    if (!strcmp(arg, "--sides") && i + 1 < argc) {
      sides = (int)parse_u32(argv[++i]);
      continue;
    }
    fprintf(stderr, "Usage: %s [--out file] [--drive 0-3] [--tracks 80] [--sides 2]\n", argv[0]);
    return 1;
  }

  signal(SIGINT, on_sigint);
  ps_setup_protocol();

  FILE *fp = fopen(outfile, "wb");
  if (!fp) {
    perror("fopen");
    return 1;
  }

  select_drive(drive);
  motor_on();
  usleep(50000);  // spin-up
  // Seek to track 0 by stepping outward generously.
  step_track(82, 1);

  for (int t = 0; t < tracks && !stop_requested; t++) {
    for (int s = 0; s < sides && !stop_requested; s++) {
      set_side(s);
      if (t > 0 || s > 0) {
        // Step one track inward between logical tracks after side 1, else stay on same cylinder for side toggle.
        if (s == 0 && t > 0) {
          step_track(1, 0);
        }
      }

      int rc = read_track_raw(CHIP_BUF_ADDR, TRACK_RAW_WORDS);
      if (rc == -2) {
        fprintf(stderr, "Abort requested\n");
        fclose(fp);
        motor_off();
        return 1;
      }
      if (rc != 0) {
        fprintf(stderr, "Track %d side %d: DMA timeout\n", t, s);
        fclose(fp);
        motor_off();
        return 1;
      }

      // Copy raw track bytes from chip RAM to file.
      for (uint32_t i = 0; i < TRACK_RAW_BYTES; i++) {
        uint8_t v = (uint8_t)ps_read_8(CHIP_BUF_ADDR + i);
        fputc(v, fp);
      }
      printf("Track %d side %d dumped\n", t, s);
      fflush(stdout);
    }
  }

  motor_off();
  fclose(fp);
  printf("Dump complete: %s\n", outfile);
  return 0;
}
