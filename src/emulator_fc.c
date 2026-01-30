#include "emulator_fc.h"
#include "log.h"
#ifdef PISTORM_KMOD
#include "gpio/ps_protocol.h"
#endif

uint32_t current_fc = 0;
static enum fc_mode fc_mode_state = FC_MODE_OFF;

void fc_set_mode(enum fc_mode mode) {
  fc_mode_state = mode;
  switch (mode) {
  case FC_MODE_OFF:
    LOG_INFO("[FC] disabled\n");
    break;
  case FC_MODE_STUB:
    LOG_INFO("[FC] enabled (stub)\n");
    break;
  case FC_MODE_CPLD:
    LOG_INFO("[FC] enabled (cpld)\n");
    break;
  default:
    LOG_WARN("[FC] unknown mode %d, forcing off\n", mode);
    fc_mode_state = FC_MODE_OFF;
    break;
  }
}

enum fc_mode fc_get_mode(void) {
  return fc_mode_state;
}

void cpu_set_fc(uint32_t fc) {
  if (fc == current_fc) {
    return;
  }
  current_fc = fc;
  if (fc_mode_state == FC_MODE_OFF) {
    return;
  }
  if (fc_mode_state == FC_MODE_CPLD) {
#ifdef PISTORM_KMOD
    ps_fc_write((uint8_t)(fc & 0x7u));
#endif
  }
  LOG_DEBUG("[FC] line=%u (S=%u D=%u P=%u)\n",
            fc,
            (fc >> 2) & 1u,
            (fc >> 1) & 1u,
            fc & 1u);
}
