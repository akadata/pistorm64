#include "emulator_fc.h"

uint32_t current_fc = 0;

void cpu_set_fc(uint32_t fc) {
    current_fc = fc;
}

