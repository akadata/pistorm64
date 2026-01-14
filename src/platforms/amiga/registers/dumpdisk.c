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
#define CHIP_BUF_ADDR   0x00008000u  // chip RAM buffer for DMA (keep within 16-bit word ptr)

static volatile sig_atomic_t stop_requested = 0;
static uint8_t prb_shadow = 0xFF;
static uint8_t ciaa_pra_shadow = 0xFF;
static uint8_t ddrb_shadow = 0xFF;

static void on_sigint(int signo) {
  (void)signo;
  stop_requested = 1;
  // Try to shut down motor promptly on Ctrl-C.
  prb_shadow |= CIAB_DSKMOTOR;
  ps_write_8(CIABPRB, prb_shadow);
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

static void overlay_off(void) {
  // Ensure CIAA overlay bit is cleared so custom chip space is visible.
  uint32_t ddra = CIAA_BASE + CIADDRA;
  uint32_t pra  = CIAA_BASE + CIAPRA;
  uint8_t ddr = (uint8_t)ps_read_8(ddra);
  ddr |= CIAA_OVERLAY;
  ps_write_8(ddra, ddr);
  ciaa_pra_shadow = (uint8_t)ps_read_8(pra);
  ciaa_pra_shadow &= (uint8_t)~CIAA_OVERLAY;
  ps_write_8(pra, ciaa_pra_shadow);
}

static void init_disk_port(void) {
  // Disk control is all of CIAB port B: motor, select, side, direction, step.
  ddrb_shadow = 0xFF;  // all outputs
  ps_write_8(CIABDDRB, ddrb_shadow);
  // Clear CIAB control registers to plain I/O mode.
  ps_write_8(CIABCRA, 0x00);
  ps_write_8(CIABCRB, 0x00);
  prb_shadow = (uint8_t)ps_read_8(CIABPRB);
  // Default to drive 0 selected, motor on, side 0 (active low), STEP idle high.
  prb_shadow = (uint8_t)(CIAB_DSKSEL1 | CIAB_DSKSEL2 | CIAB_DSKSEL3);  // high
  prb_shadow &= (uint8_t)~(CIAB_DSKSEL0 | CIAB_DSKMOTOR | CIAB_DSKSIDE | CIAB_DSKDIREC);
  prb_shadow |= CIAB_DSKSTEP;  // idle high (active low)
  ps_write_8(CIABPRB, prb_shadow);
  usleep(1000);
}

static void force_drive0_outputs(void) {
  ddrb_shadow = 0xFF;
  ps_write_8(CIABDDRB, ddrb_shadow);
  // Drive0 selected (bit3=0), others high, motor on (bit7=0), side0 (bit2=0).
  prb_shadow = (uint8_t)(CIAB_DSKSEL1 | CIAB_DSKSEL2 | CIAB_DSKSEL3);  // high on other selects
  prb_shadow &= (uint8_t)~(CIAB_DSKSEL0 | CIAB_DSKMOTOR);              // select 0 + motor on
  prb_shadow &= (uint8_t)~CIAB_DSKSIDE;                                // side 0
  prb_shadow &= (uint8_t)~CIAB_DSKDIREC;                               // dir inward (0)
  prb_shadow |= CIAB_DSKSTEP;                                          // idle high
  ps_write_8(CIABPRB, prb_shadow);
  usleep(1000);
}

static void write_ddrb_locked(uint8_t val, const char *why) {
  ddrb_shadow = val;
  ps_write_8(CIABDDRB, val);
  uint8_t rd = (uint8_t)ps_read_8(CIABDDRB);
  printf("%s: CIABDDRB=0x%02X (readback)\n", why, rd);
  if (rd != val) {
    fprintf(stderr, "%s: DDRB write verify FAILED (wanted 0x%02X got 0x%02X)\n", why, val, rd);
  }
}

static void write_prb_locked(uint8_t val, const char *why) {
  prb_shadow = val;
  ps_write_8(CIABPRB, val);
  uint8_t rd = (uint8_t)ps_read_8(CIABPRB);
  printf("%s: CIABPRB=0x%02X (readback)\n", why, rd);
  if (rd != val) {
    fprintf(stderr, "%s: PRB write verify FAILED (wanted 0x%02X got 0x%02X)\n", why, val, rd);
  }
}

static int wait_for_ready(int timeout_ms) {
  const int polls = timeout_ms / 10;
  for (int i = 0; i < polls; i++) {
    uint8_t pra = (uint8_t)ps_read_8(CIAAPRA);
    if ((pra & CIAA_DSKRDY) == 0) return 0;  // active low -> ready
    usleep(10000);
  }
  printf("WARN: drive not ready after %dms (CIAAPRA=0x%02X)\n",
         timeout_ms, (unsigned)ps_read_8(CIAAPRA) & 0xFFu);
  return -1;
}

static void motor_on(void) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKMOTOR);
  prb_shadow &= (uint8_t)~CIAB_DSKMOTOR;  // active low on Amiga drives
  ps_write_8(CIABPRB, prb_shadow);
}

static void motor_off(void) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKMOTOR);
  // Latch motor-off: set motor high, briefly deassert selects, then reassert the current drive
  // to clock the motor bit, and finally release selects.
  prb_shadow |= CIAB_DSKMOTOR;
  // Deassert all selects.
  prb_shadow |= (uint8_t)(CIAB_DSKSEL0 | CIAB_DSKSEL1 | CIAB_DSKSEL2 | CIAB_DSKSEL3);
  ps_write_8(CIABPRB, prb_shadow);
  // Pulse drive 0 select low to latch motor-off.
  prb_shadow &= (uint8_t)~CIAB_DSKSEL0;
  ps_write_8(CIABPRB, prb_shadow);
  usleep(1000);
  // Release select.
  prb_shadow |= CIAB_DSKSEL0;
  ps_write_8(CIABPRB, prb_shadow);
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
  prb_shadow |= mask;                // deassert all (active low)
  prb_shadow &= (uint8_t)~sel_bit;   // assert target
  ps_write_8(CIABPRB, prb_shadow);
  // Ensure motor is still on after select.
  ps_write_8(CIABPRB, prb_shadow);
  usleep(1000);
}

static void set_side(int side) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKSIDE);
  if (side) prb_shadow |= CIAB_DSKSIDE;   // 1 = lower head on standard drives
  else prb_shadow &= (uint8_t)~CIAB_DSKSIDE;
  ps_write_8(CIABPRB, prb_shadow);
}

static void step_pulse(int outwards) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKDIREC | CIAB_DSKSTEP);
  static int last_dir = -1;
  if (outwards) prb_shadow |= CIAB_DSKDIREC;  // 1 = out, 0 = in (per hardware manual)
  else prb_shadow &= (uint8_t)~CIAB_DSKDIREC;
  ps_write_8(CIABPRB, prb_shadow);
  // After a direction change, allow the required settle before stepping.
  if (last_dir != outwards) {
    usleep(20000);  // >=18ms per hardware spec
  }
  last_dir = outwards;

  // STEP* is active low; idle high then pulse low briefly.
  prb_shadow |= CIAB_DSKSTEP;  // ensure idle high
  ps_write_8(CIABPRB, prb_shadow);
  usleep(5);
  prb_shadow &= (uint8_t)~CIAB_DSKSTEP;
  ps_write_8(CIABPRB, prb_shadow);
  usleep(1000);  // low pulse width (~1ms, above 1us min)
  prb_shadow |= CIAB_DSKSTEP;
  ps_write_8(CIABPRB, prb_shadow);
  usleep(3000);  // settle >=3ms
}

static void step_track(int steps, int outwards) {
  for (int i = 0; i < steps && !stop_requested; i++) {
    step_pulse(outwards);
  }
}

static int seek_track0(void) {
  // Try outward first, then inward if needed.
  for (int attempt = 0; attempt < 2; attempt++) {
    int outwards = (attempt == 0) ? 1 : 0;
    if (outwards) prb_shadow |= CIAB_DSKDIREC;
    else prb_shadow &= (uint8_t)~CIAB_DSKDIREC;
    ps_write_8(CIABPRB, prb_shadow);
    for (int i = 0; i < 90; i++) {
      step_pulse(outwards);
      uint8_t pra = (uint8_t)ps_read_8(CIAAPRA);
      if ((pra & CIAA_DSKTRACK0) == 0) {
        printf("Reached track0 (attempt %d, steps %d, dir %s)\n",
               attempt, i + 1, outwards ? "out" : "in");
        return 0;
      }
      if (stop_requested) return -1;
    }
  }
  printf("WARN: track0 not detected after seeks\n");
  return -1;
}

static void log_status(const char *label) {
  uint8_t pra = (uint8_t)ps_read_8(CIAAPRA);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  uint8_t ddrb = (uint8_t)ps_read_8(CIABDDRB);
  uint16_t dsklen = (uint16_t)ps_read_16(DSKLEN);  // note: DSKLEN is write-only; readback is undefined
  uint16_t dskbytr = (uint16_t)ps_read_16(DSKBYTR);
  uint16_t intreq = (uint16_t)ps_read_16(INTREQR);
  uint16_t intena = (uint16_t)ps_read_16(INTENAR);
  uint16_t dmaconr = (uint16_t)ps_read_16(DMACONR);
  uint16_t adkconr = (uint16_t)ps_read_16(ADKCONR);
  printf("%s: CIAAPRA=0x%02X (RDY=%d TRK0=%d PROT=%d CHG=%d) CIABPRB=0x%02X CIABDDRB=0x%02X "
         "DSKLEN=0x%04X DSKBYTR=0x%04X INTREQR=0x%04X INTENAR=0x%04X DMACONR=0x%04X ADKCONR=0x%04X\n",
         label,
         pra,
         (pra & CIAA_DSKRDY) ? 0 : 1,       // active low
         (pra & CIAA_DSKTRACK0) ? 0 : 1,    // active low
         (pra & CIAA_DSKPROT) ? 1 : 0,      // 1 = write-protected
         (pra & CIAA_DSKCHANGE) ? 1 : 0,    // 1 = disk change detected
         prb, ddrb, dsklen, dskbytr, intreq, intena, dmaconr, adkconr);
}

// Paula disk DMA -------------------------------------------------------------
static int read_track_raw(uint32_t chip_addr, uint32_t words) {
  // Clear any pending disk interrupt.
  ps_write_16(INTREQ, INTF_DSKBLK | INTF_DSKSYN);
  // Stop any previous DMA via DSKLEN and DMACON.
  ps_write_16(DSKLEN, 0x4000);
  ps_write_16(DMACON, DMAF_DISK);
  // Set ADKCON for MFM fast, word sync, MFM precomp (read path).
  ps_write_16(ADKCON, ADKF_SETCLR | ADKF_FAST | ADKF_WORDSYNC | ADKF_MFMPREC);
  // Arm disk interrupts (DSKBLK) + master.
  ps_write_16(INTENA, INTF_SETCLR | INTF_DSKBLK | INTF_INTEN);
  // Program DMA pointer (word address; lower bit must be 0).
  uint32_t ptr = (chip_addr & 0x1FFFFEu) >> 1;  // word address into chip RAM
  uint16_t ptr_hi = (uint16_t)((ptr >> 16) & 0xFFFFu);
  uint16_t ptr_lo = (uint16_t)(ptr & 0xFFFFu);
  ps_write_16(DSKPTH, ptr_hi);
  ps_write_16(DSKPTL, ptr_lo);
  // Sync word and length.
  ps_write_16(DSKSYNC, DSK_SYNC_WORD);
  // Enable disk DMA master + disk.
  ps_write_16(DMACON, DMAF_SETCLR | DMAF_MASTER | DMAF_DISK);
  uint16_t dmaconr = (uint16_t)ps_read_16(DMACONR);
  // Kick DMA: bit15 enable, bit14 direction (0 = read), bits 0-13 length words.
  uint16_t len = (uint16_t)(words & 0x3FFFu);
  uint16_t want_len = 0x8000u | len;  // bit15 enables DMA, bit14=0 => read
  ps_write_16(DSKLEN, 0x4000);        // force off per HW sequence
  ps_write_16(DSKLEN, want_len);      // write-only; readback is undefined on real hardware
  ps_write_16(DSKLEN, want_len);      // must be written twice to start DMA
  uint16_t arm_bytr = (uint16_t)ps_read_16(DSKBYTR);
  uint16_t arm_int = (uint16_t)ps_read_16(INTREQR);
  printf("DMA armed: DSKPTH=0x%04X DSKPTL=0x%04X DSKLEN(w)=0x%04X DSKBYTR=0x%04X INTREQR=0x%04X DMACONR=0x%04X\n",
         ptr_hi, ptr_lo, want_len, arm_bytr, arm_int, dmaconr);
  // Wait for interrupt or timeout.
  const int max_poll = 1000000;  // ~1s in 1us polls
  for (int i = 0; i < max_poll; i++) {
    if (stop_requested) {
      ps_write_16(DSKLEN, 0x4000);
      ps_write_16(DMACON, DMAF_DISK);
      return -2;
    }
    uint16_t intreq = (uint16_t)ps_read_16(INTREQR);
    if (intreq & INTF_DSKBLK) {
      ps_write_16(INTREQ, INTF_DSKBLK);  // clear
      // Stop DMA.
      ps_write_16(DSKLEN, 0x4000);
      ps_write_16(DMACON, DMAF_DISK);
      return 0;
    }
      if ((i % 100000) == 0) {
        uint16_t bytr = (uint16_t)ps_read_16(DSKBYTR);
        printf(" ... poll %d DSKBYTR=0x%04X INTREQR=0x%04X\n",
               i, bytr, (uint16_t)ps_read_16(INTREQR));
      }
    usleep(10);  // slow the loop slightly
  }
  // Stop DMA on timeout.
  ps_write_16(DSKLEN, 0x4000);
  ps_write_16(DMACON, DMAF_DISK);
  return -1;
}

int main(int argc, char **argv) {
  const char *outfile = "dump.raw";
  int drive = 0;
  int tracks = 80;
  int sides = 2;
  int spin_test = 0;

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
    if (!strcmp(arg, "--spin-test")) {
      spin_test = 1;
      continue;
    }
    fprintf(stderr, "Usage: %s [--out file] [--drive 0-3] [--tracks 80] [--sides 2] [--spin-test]\n", argv[0]);
    return 1;
  }

  signal(SIGINT, on_sigint);
  ps_setup_protocol();

  overlay_off();
  init_disk_port();
  // Hard-lock CIAB lines: DDRB=0xFF, PRB=0x70 (motor+DS0 asserted, side0).
  write_ddrb_locked(0xFF, "ddrb init");
  write_prb_locked(0x70, "prb init");
  if (ddrb_shadow != 0xFF || prb_shadow != 0x70) {
    fprintf(stderr, "ERROR: Failed to latch CIAB DDRB/PRB (ddrb=0x%02X prb=0x%02X)\n",
            ddrb_shadow, prb_shadow);
    return 1;
  }
  motor_on();
  usleep(800000);  // spin-up
  wait_for_ready(500);
  log_status("after motor on");

  if (spin_test) {
    for (int i = 0; i < 10 && !stop_requested; i++) {
      write_ddrb_locked(0xFF, "spin ddrb");
      write_prb_locked(0x70, "spin reassert");
      if (ddrb_shadow != 0xFF || prb_shadow != 0x70) {
        fprintf(stderr, "ERROR: Spin write verify failed (ddrb=0x%02X prb=0x%02X)\n",
                ddrb_shadow, prb_shadow);
        break;
      }
      log_status("spin poll");
      wait_for_ready(250);
    }
    motor_off();
    return 0;
  }

  FILE *fp = fopen(outfile, "wb");
  if (!fp) {
    perror("fopen");
    motor_off();
    return 1;
  }
  // Seek to track 0 with sensor check.
  seek_track0();
  log_status("after seek to track0");

  // Skip long sweeps for quick single-track dumps; keep it minimal.
  if (!(tracks == 1 && sides == 1)) {
    // Seek to track 80 (outermost), then back to track 0.
    step_track(80, 0);  // inward toward high tracks
    log_status("after seek to track80");
    seek_track0();
    log_status("after re-seek to track0");

    // Exercise head movement backwards in 5-track steps from 80 to 0.
    for (int back = 75; back >= 0; back -= 5) {
      step_track(5, 0);  // inward 5 tracks
      printf("Head step to approx track %d\n", back);
      usleep(20000);
    }
    seek_track0();
    log_status("after backstep sweep to track0");
  }

  for (int t = 0; t < tracks && !stop_requested; t++) {
    for (int s = 0; s < sides && !stop_requested; s++) {
      // Reassert motor/select in case a previous iteration turned anything off.
      select_drive(drive);
      motor_on();
      usleep(500000);  // allow motor/select to settle (motor spec: ~500ms spin-up)
  set_side(s);
  if (t > 0 || s > 0) {
    // Step one track inward between logical tracks after side 1, else stay on same cylinder for side toggle.
    if (s == 0 && t > 0) {
      step_track(1, 0);
    }
  }
  // Small settle time after head move/side change.
  usleep(20000);
  wait_for_ready(500);
      // Ensure DDRB/PRB are sane immediately before DMA.
      force_drive0_outputs();
      select_drive(drive);
      set_side(s);
      wait_for_ready(200);
      log_status("before DMA");

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
