#include "emulator_fc.h"
#include "log.h"

uint32_t current_fc = 0;

void cpu_set_fc(uint32_t fc) {
    if (fc == current_fc) {
        return;
    }
    current_fc = fc;
    LOG_DEBUG("[FC] line=%u (S=%u D=%u P=%u)\n",
              fc,
              (fc >> 2) & 1u,
              (fc >> 1) & 1u,
              fc & 1u);
}
