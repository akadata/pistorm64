// SPDX-License-Identifier: MIT
// Interactive I/O harness for Amiga custom chip access from the Pi side.
// Exercises LEDs, disk control lines (motor/select/step/side), joystick/mouse counters,
// POT inputs, serial/parallel status lines. Uses ps_protocol to read/write bus.

#include <ctype.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/gpio/ps_protocol.h"
#include "amiga_custom_chips.h"
#include "cia.h"
#include "paula.h"
#include "denise.h"

static volatile sig_atomic_t stop_poll = 0;

static void on_sigint(int signo) {
  (void)signo;
  stop_poll = 1;
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

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s COMMAND [options]\n"
          "\n"
          "LEDs:\n"
          "  --disk-led <on|off>         Toggle CIAA LED (A500/A2000 front LED)\n"
          "\n"
          "Joystick/Mouse/POT polling:\n"
          "  --poll-joy [count] [ms]     Print JOY0/JOY1 counters + fire bits\n"
          "  --poll-pot [count] [ms]     Print POT0/1 values\n"
          "  --kbd-poll [count] [ms]     Poll CIAA keyboard serial byte (raw keycodes)\n"
          "\n"
          "Disk control (CIAB port B):\n"
          "  --disk-motor <on|off>       Set disk motor bit\n"
          "  --disk-select <0-3>         Assert a drive select (active low)\n"
          "  --disk-side <0|1>           Select disk side (0=upper,1=lower)\n"
          "  --disk-step <in|out>        Pulse STEP with direction (in=0,out=1)\n"
          "\n"
          "Serial/Parallel (CIAB port A):\n"
          "  --serial-set <dtr> <rts>    Set DTR/RTS (0/1)\n"
          "  --serial-read               Read CTS/DSR/CD pins\n"
          "  --parallel-read             Read printer BUSY/POUT/SELECT pins\n"
          "\n"
          "Notes:\n"
          "  - Uses ps_protocol; run on the Pi side while Amiga is halted.\n"
          "  - Disk select/motor signals are active low on classic drives.\n"
          "  - Use Ctrl-C to stop polling loops.\n",
          prog);
}

static void ensure_output(uint32_t ddr_addr, uint8_t mask) {
  uint8_t ddr = (uint8_t)ps_read_8(ddr_addr);
  if ((ddr & mask) != mask) {
    ddr |= mask;
    ps_write_8(ddr_addr, ddr);
  }
}

static void set_disk_led(int on) {
  // LED lives on CIAA port A bit 1 (active low).
  uint32_t ddra = CIAA_BASE + CIADDRA;
  uint32_t pra = CIAA_BASE + CIAPRA;
  ensure_output(ddra, CIAA_LED);
  uint8_t val = (uint8_t)ps_read_8(pra);
  if (on) val &= (uint8_t)~CIAA_LED;
  else val |= CIAA_LED;
  ps_write_8(pra, val);
  printf("disk LED %s (PRA=0x%02X)\n", on ? "on" : "off", val);
}

static void poll_joy(int count, int delay_ms) {
  printf("Polling JOY0/JOY1 + fire buttons (Ctrl-C to stop)...\n");
  for (int i = 0; count == 0 || i < count; i++) {
    uint16_t j0 = (uint16_t)ps_read_16(JOY0DAT);
    uint16_t j1 = (uint16_t)ps_read_16(JOY1DAT);
    uint8_t pra = (uint8_t)ps_read_8(CIAAPRA);
    printf("JOY0=0x%04X JOY1=0x%04X FIRE0=%d FIRE1=%d\n",
           j0, j1,
           (pra & CIAA_GAMEPORT0) ? 0 : 1,
           (pra & CIAA_GAMEPORT1) ? 0 : 1);
    fflush(stdout);
    if (delay_ms) usleep((useconds_t)delay_ms * 1000u);
    if (stop_poll) break;
  }
}

static void poll_pot(int count, int delay_ms) {
  printf("Polling POT0/1 (Ctrl-C to stop)...\n");
  for (int i = 0; count == 0 || i < count; i++) {
    uint16_t p0 = (uint16_t)ps_read_16(POT0DAT);
    uint16_t p1 = (uint16_t)ps_read_16(POT1DAT);
    printf("POT0=0x%04X POT1=0x%04X\n", p0, p1);
    fflush(stdout);
    if (delay_ms) usleep((useconds_t)delay_ms * 1000u);
    if (stop_poll) break;
  }
}

static void poll_keyboard_serial(int count, int delay_ms) {
  printf("Polling CIAA keyboard serial (CIAASDR), Ctrl-C to stop...\n");
  for (int i = 0; count == 0 || i < count; i++) {
    uint8_t key = (uint8_t)ps_read_8(CIAASDR);
    uint8_t icr = (uint8_t)ps_read_8(CIAAICR);  // reading clears serial flag
    printf("KBD: 0x%02X (ICR=0x%02X)\n", key, icr);
    fflush(stdout);
    if (delay_ms) usleep((useconds_t)delay_ms * 1000u);
    if (stop_poll) break;
  }
}

static void set_disk_motor(int on) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKMOTOR);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  if (on) prb |= CIAB_DSKMOTOR;
  else prb &= (uint8_t)~CIAB_DSKMOTOR;
  ps_write_8(CIABPRB, prb);
  printf("disk motor %s (PRB=0x%02X)\n", on ? "on" : "off", prb);
}

static void set_disk_select(int drive) {
  if (drive < 0 || drive > 3) {
    fprintf(stderr, "drive must be 0-3\n");
    exit(1);
  }
  uint8_t mask = CIAB_DSKSEL0 | CIAB_DSKSEL1 | CIAB_DSKSEL2 | CIAB_DSKSEL3;
  uint8_t sel_bit = (uint8_t)(CIAB_DSKSEL0 << drive);  // 0->SEL0, 1->SEL1, etc.
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, mask);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  prb |= mask;        // deassert all (active low)
  prb &= (uint8_t)~sel_bit;  // assert chosen drive
  ps_write_8(CIABPRB, prb);
  printf("disk select drive %d (PRB=0x%02X)\n", drive, prb);
}

static void set_disk_side(int side) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKSIDE);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  if (side) prb |= CIAB_DSKSIDE;
  else prb &= (uint8_t)~CIAB_DSKSIDE;
  ps_write_8(CIABPRB, prb);
  printf("disk side %d (PRB=0x%02X)\n", side, prb);
}

static void disk_step(int outwards) {
  // Direction: 0 = inward, 1 = outward (matches CIAB_DSKDIREC naming).
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKDIREC | CIAB_DSKSTEP);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  if (outwards) prb |= CIAB_DSKDIREC;
  else prb &= (uint8_t)~CIAB_DSKDIREC;
  ps_write_8(CIABPRB, prb);
  // Pulse STEP low->high.
  prb |= CIAB_DSKSTEP;
  ps_write_8(CIABPRB, prb);
  usleep(5000);  // 5ms pulse
  prb &= (uint8_t)~CIAB_DSKSTEP;
  ps_write_8(CIABPRB, prb);
  printf("disk step %s (PRB=0x%02X)\n", outwards ? "out" : "in", prb);
}

static void serial_set(int dtr, int rts) {
  uint32_t ddra = CIAB_BASE + CIADDRA;
  ensure_output(ddra, CIAB_COMDTR | CIAB_COMRTS);
  uint8_t pra = (uint8_t)ps_read_8(CIABPRA);
  if (dtr) pra |= CIAB_COMDTR; else pra &= (uint8_t)~CIAB_COMDTR;
  if (rts) pra |= CIAB_COMRTS; else pra &= (uint8_t)~CIAB_COMRTS;
  ps_write_8(CIABPRA, pra);
  printf("serial set DTR=%d RTS=%d (PRA=0x%02X)\n", dtr, rts, pra);
}

static void serial_read(void) {
  uint8_t pra = (uint8_t)ps_read_8(CIABPRA);
  printf("serial: CTS=%d DSR=%d CD=%d PRA=0x%02X\n",
         (pra & CIAB_COMCTS) ? 1 : 0,
         (pra & CIAB_COMDSR) ? 1 : 0,
         (pra & CIAB_COMCD) ? 1 : 0,
         pra);
}

static void parallel_read(void) {
  uint8_t pra = (uint8_t)ps_read_8(CIABPRA);
  printf("parallel: BUSY=%d POUT=%d SELECT=%d PRA=0x%02X\n",
         (pra & CIAB_PRTRBUSY) ? 1 : 0,
         (pra & CIAB_PRTRPOUT) ? 1 : 0,
         (pra & CIAB_PRTRSEL) ? 1 : 0,
         pra);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  signal(SIGINT, on_sigint);
  ps_setup_protocol();

  const char *cmd = argv[1];

  if (!strcmp(cmd, "--disk-led") && argc >= 3) {
    set_disk_led(!strcasecmp(argv[2], "on"));
    return 0;
  }
  if (!strcmp(cmd, "--poll-joy")) {
    int count = (argc >= 3) ? (int)parse_u32(argv[2]) : 0;
    int delay_ms = (argc >= 4) ? (int)parse_u32(argv[3]) : 250;
    poll_joy(count, delay_ms);
    return 0;
  }
  if (!strcmp(cmd, "--poll-pot")) {
    int count = (argc >= 3) ? (int)parse_u32(argv[2]) : 0;
    int delay_ms = (argc >= 4) ? (int)parse_u32(argv[3]) : 250;
    poll_pot(count, delay_ms);
    return 0;
  }
  if (!strcmp(cmd, "--kbd-poll")) {
    int count = (argc >= 3) ? (int)parse_u32(argv[2]) : 0;
    int delay_ms = (argc >= 4) ? (int)parse_u32(argv[3]) : 250;
    poll_keyboard_serial(count, delay_ms);
    return 0;
  }
  if (!strcmp(cmd, "--disk-motor") && argc >= 3) {
    set_disk_motor(!strcasecmp(argv[2], "on"));
    return 0;
  }
  if (!strcmp(cmd, "--disk-select") && argc >= 3) {
    set_disk_select((int)parse_u32(argv[2]));
    return 0;
  }
  if (!strcmp(cmd, "--disk-side") && argc >= 3) {
    set_disk_side((int)parse_u32(argv[2]));
    return 0;
  }
  if (!strcmp(cmd, "--disk-step") && argc >= 3) {
    const char *dir = argv[2];
    int outwards = (!strcasecmp(dir, "out") || !strcasecmp(dir, "outwards"));
    disk_step(outwards);
    return 0;
  }
  if (!strcmp(cmd, "--serial-set") && argc >= 4) {
    serial_set((int)parse_u32(argv[2]), (int)parse_u32(argv[3]));
    return 0;
  }
  if (!strcmp(cmd, "--serial-read")) {
    serial_read();
    return 0;
  }
  if (!strcmp(cmd, "--parallel-read")) {
    parallel_read();
    return 0;
  }

  usage(argv[0]);
  return 1;
}
