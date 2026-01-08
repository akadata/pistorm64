// SPDX-License-Identifier: MIT
// Minimal stubs for builds that exclude /opt/vc host support.

#include <stdint.h>
#include <string.h>
#include "platforms/amiga/pistorm-dev/pistorm-dev.h"

// Mirror the real implementation so CLI config files still work without /opt/vc.
static char cfg_filename[256] = "default.cfg";

uint32_t handle_pistorm_dev_read(uint32_t addr, uint8_t type) {
  (void)addr;
  (void)type;
  return 0xFFFFFFFF;
}

void handle_pistorm_dev_write(uint32_t addr, uint32_t val, uint8_t type) {
  (void)addr;
  (void)val;
  (void)type;
}

char* get_pistorm_devcfg_filename(void) {
  return cfg_filename;
}

void set_pistorm_devcfg_filename(char* filename) {
  if (!filename) {
    return;
  }
  strncpy(cfg_filename, filename, sizeof(cfg_filename) - 1);
  cfg_filename[sizeof(cfg_filename) - 1] = '\0';
}
