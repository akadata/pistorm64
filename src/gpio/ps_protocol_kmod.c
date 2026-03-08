// SPDX-License-Identifier: MIT

#include "gpio/ps_protocol.h"

#include <stdio.h>
#include <string.h>

#include "pistorm/backend.h"
#include "log.h"

volatile unsigned int gpio_shadow[32];
volatile unsigned int* gpio = gpio_shadow; /* legacy pointer for old tools/tests */
static int ps_protocol_ready = 0;

static void ps_refresh_gpio_ptr(void) {
  volatile uint32_t* regs = ps_backend_gpio_regs();
  if (regs) {
    gpio = regs;
  } else {
    gpio = gpio_shadow;
  }
}

int ps_select_backend(const char* name) {
  return ps_backend_select(name, 0);
}

const char* ps_get_backend(void) {
  return ps_backend_selected_name();
}

int ps_set_userspace_gpclk_src(uint32_t src) {
  return ps_backend_userspace_set_gpclk_src(src);
}

int ps_set_userspace_gpclk_div(uint32_t div) {
  return ps_backend_userspace_set_gpclk_div(div);
}

int ps_set_userspace_wr_stretch(uint32_t count) {
  return ps_backend_userspace_set_wr_stretch(count);
}

int ps_set_userspace_rd_stretch(uint32_t count) {
  return ps_backend_userspace_set_rd_stretch(count);
}

void ps_setup_protocol(void) {
  if (ps_backend_setup_protocol() < 0) {
    ps_protocol_ready = 0;
    fprintf(stderr, "[ps_protocol] setup failed (backend=%s)\n", ps_backend_selected_name());
    return;
  }
  ps_protocol_ready = 1;
  ps_refresh_gpio_ptr();
  printf("[ps_protocol] backend=%s\n", ps_backend_active_name());
}

void ps_cleanup_protocol(void) {
  ps_protocol_ready = 0;
  ps_backend_shutdown();
  gpio = gpio_shadow;
}

int ps_protocol_is_ready(void) {
  return ps_protocol_ready;
}

void ps_reset_state_machine(void) {
  (void)ps_backend_reset_sm();
}

void ps_pulse_reset(void) {
  (void)ps_backend_pulse_reset();
}

void ps_protocol_dump_stats(void) {
  ps_backend_dump_stats();
}

#ifdef PISTORM_KMOD
void ps_fc_write(uint8_t fc) {
  if (log_get_level() >= LOG_LEVEL_VERBOSE) {
    LOG_VERBOSE("[FC] backend stub (fc=%u)\n", fc);
  }
  (void)fc;
}
#endif

uint8_t ps_read_8(uint32_t addr) {
  uint8_t v = 0;
  if (ps_backend_read8(addr, 0, &v) < 0) {
    return 0;
  }
  return v;
}

uint16_t ps_read_16(uint32_t addr) {
  uint16_t v = 0;
  if (ps_backend_read16(addr, 0, &v) < 0) {
    return 0;
  }
  return v;
}

uint32_t ps_read_32(uint32_t addr) {
  uint32_t v = 0;
  if (ps_backend_read32(addr, 0, &v) < 0) {
    return 0;
  }
  return v;
}

void ps_write_8(uint32_t addr, uint8_t v) {
  (void)ps_backend_write8(addr, v, 0);
}

void ps_write_16(uint32_t addr, uint16_t v) {
  (void)ps_backend_write16(addr, v, 0);
}

void ps_write_32(uint32_t addr, uint32_t v) {
  (void)ps_backend_write32(addr, v, 0);
}

uint16_t ps_read_status_reg(void) {
  return ps_backend_read_status();
}

void ps_write_status_reg(uint16_t value) {
  (void)ps_backend_write_status(value);
}

unsigned int ps_get_ipl_zero(void) {
  unsigned int level = ps_gpio_lev();
  return level & (1u << PIN_IPL_ZERO);
}

unsigned int ps_gpio_lev(void) {
  struct pistorm_pins pins = {0};

  if (ps_backend_get_pins(&pins) == 0) {
    gpio_shadow[13] = pins.gplev0;
    gpio_shadow[14] = pins.gplev1;
  }

  ps_refresh_gpio_ptr();
  return gpio_shadow[13];
}

int ps_flush_batch_queue(void) {
  return ps_backend_flush();
}
