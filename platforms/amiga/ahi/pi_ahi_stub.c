// SPDX-License-Identifier: MIT

// Minimal AHI backend stub for builds without ALSA development headers/libraries.
// Keeps link compatibility; returns "disabled" at runtime.

#include <stdint.h>
#include <stdio.h>

#include "platforms/amiga/ahi/pi_ahi.h"

uint32_t pi_ahi_init(char *dev) {
  (void)dev;
  printf("[AHI] Disabled (built without ALSA).\n");
  return 0;
}

void pi_ahi_shutdown() {}

void handle_pi_ahi_write(uint32_t addr_, uint32_t val, uint8_t type) {
  (void)addr_;
  (void)val;
  (void)type;
}

uint32_t handle_pi_ahi_read(uint32_t addr_, uint8_t type) {
  (void)addr_;
  (void)type;
  return 0;
}

int get_ahi_sample_size(uint16_t type) {
  (void)type;
  return 0;
}

int get_ahi_channels(uint16_t type) {
  (void)type;
  return 0;
}

void pi_ahi_set_playback_rate(uint32_t rate) { (void)rate; }
