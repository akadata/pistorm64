// SPDX-License-Identifier: MIT
// Minimal stubs for builds that exclude /opt/vc host support.

#include <stdint.h>
#include "platforms/amiga/pistorm-dev/pistorm-dev.h"

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

char *get_pistorm_devcfg_filename() { return 0; }

void set_pistorm_devcfg_filename(char *filename) { (void)filename; }
