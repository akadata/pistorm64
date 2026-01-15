// SPDX-License-Identifier: MIT
// Simple motor control test to verify motor line activation

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

static volatile sig_atomic_t stop_requested = 0;

static void on_sigint(int signo) {
  (void)signo;
  stop_requested = 1;
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
  uint8_t pra_shadow = (uint8_t)ps_read_8(pra);
  pra_shadow &= (uint8_t)~CIAA_OVERLAY;
  ps_write_8(pra, pra_shadow);
}

static void init_disk_port(void) {
  // Disk control is all of CIAB port B: motor, select, side, direction, step.
  uint8_t ddrb_shadow = 0xFF;  // all outputs
  ps_write_8(CIABDDRB, ddrb_shadow);
  // Clear CIAB control registers to plain I/O mode.
  ps_write_8(CIABCRA, 0x00);
  ps_write_8(CIABCRB, 0x00);
  uint8_t prb_shadow = (uint8_t)ps_read_8(CIABPRB);
  // Default to drive 0 selected, motor on, side 0 (active low), STEP idle high.
  prb_shadow = (uint8_t)(CIAB_DSKSEL1 | CIAB_DSKSEL2 | CIAB_DSKSEL3);  // high
  prb_shadow &= (uint8_t)~(CIAB_DSKSEL0 | CIAB_DSKMOTOR | CIAB_DSKSIDE | CIAB_DSKDIREC);
  prb_shadow |= CIAB_DSKSTEP;  // idle high (active low)
  ps_write_8(CIABPRB, prb_shadow);
  usleep(1000);
}

static void log_status(const char *label) {
  uint8_t pra = (uint8_t)ps_read_8(CIAAPRA);
  uint8_t prb = (uint8_t)ps_read_8(CIABPRB);
  uint8_t ddrb = (uint8_t)ps_read_8(CIABDDRB);
  uint16_t intreq = (uint16_t)ps_read_16(INTREQR);
  uint16_t intena = (uint16_t)ps_read_16(INTENAR);
  uint16_t dmaconr = (uint16_t)ps_read_16(DMACONR);

  printf("%s: CIAAPRA=0x%02X (RDY=%d TRK0=%d PROT=%d CHG=%d) CIABPRB=0x%02X CIABDDRB=0x%02X "
         "INTREQR=0x%04X INTENAR=0x%04X DMACONR=0x%04X\n",
         label,
         pra,
         (pra & CIAA_DSKRDY) ? 0 : 1,       // active low
         (pra & CIAA_DSKTRACK0) ? 0 : 1,    // active low
         (pra & CIAA_DSKPROT) ? 1 : 0,      // 1 = write-protected
         (pra & CIAA_DSKCHANGE) ? 1 : 0,    // 1 = disk change detected
         prb, ddrb, intreq, intena, dmaconr);
}

static void motor_on_simple(void) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKMOTOR);
  uint8_t current_prb = (uint8_t)ps_read_8(CIABPRB);
  current_prb &= (uint8_t)~CIAB_DSKMOTOR;  // active low on Amiga drives
  ps_write_8(CIABPRB, current_prb);
  printf("Motor ON: CIABPRB=0x%02X (bit 7 cleared)\n", current_prb);
}

static void motor_off_simple(void) {
  uint32_t ddrb = CIAB_BASE + CIADDRB;
  ensure_output(ddrb, CIAB_DSKMOTOR);
  uint8_t current_prb = (uint8_t)ps_read_8(CIABPRB);
  current_prb |= CIAB_DSKMOTOR;  // turn motor off (active low)
  ps_write_8(CIABPRB, current_prb);
  printf("Motor OFF: CIABPRB=0x%02X (bit 7 set)\n", current_prb);
}

int main(int argc, char **argv) {
  signal(SIGINT, on_sigint);
  ps_setup_protocol();

  overlay_off();
  init_disk_port();
  
  printf("Motor control test program\n");
  printf("This program will toggle the motor control line and monitor status.\n");
  printf("Press Ctrl+C to exit.\n\n");

  while (!stop_requested) {
    printf("\n--- Testing Motor ON ---\n");
    motor_on_simple();
    usleep(1000000); // 1 second
    log_status("After motor ON");
    
    // Check if drive becomes ready
    for (int i = 0; i < 10 && !stop_requested; i++) {
        uint8_t pra = (uint8_t)ps_read_8(CIAAPRA);
        if ((pra & CIAA_DSKRDY) == 0) {
            printf("Drive READY detected after %d seconds!\n", i+1);
            break;
        }
        sleep(1);
        if (i == 9) {
            printf("Drive still NOT READY after 10 seconds\n");
        }
    }
    
    printf("\n--- Testing Motor OFF ---\n");
    motor_off_simple();
    usleep(1000000); // 1 second
    log_status("After motor OFF");
    
    sleep(5); // Wait 5 seconds between cycles
  }

  // Ensure motor is off on exit
  motor_off_simple();
  printf("\nMotor test completed.\n");
  return 0;
}