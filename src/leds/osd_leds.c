// SPDX-License-Identifier: MIT

#include "leds/osd_leds.h"

#include <stdatomic.h>
#include <time.h>

#define OSD_LED_HOLD_MS 150u

static atomic_int osd_leds_enabled = 0;
static _Atomic uint32_t host_busy_until_ms = 0;
static _Atomic uint32_t unit_read_until_ms[OSD_LEDS_UNIT_COUNT];
static _Atomic uint32_t unit_write_until_ms[OSD_LEDS_UNIT_COUNT];
static _Atomic uint32_t unit_present_mask = 0;

static uint32_t osd_led_now_ms(void) {
  struct timespec ts;
  uint64_t now_ns;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  now_ns = ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
  return (uint32_t)(now_ns / 1000000ull);
}

static uint32_t osd_led_ns_to_ms(uint64_t now_ns) {
  return (uint32_t)(now_ns / 1000000ull);
}

static int osd_led_expiry_active(uint32_t expiry_ms, uint32_t now_ms) {
  return (int32_t)(expiry_ms - now_ms) > 0;
}

static void osd_led_pulse_host_ms(uint32_t now_ms) {
  atomic_store_explicit(&host_busy_until_ms, now_ms + OSD_LED_HOLD_MS, memory_order_relaxed);
}

void osd_leds_set_enabled(int enabled) {
  atomic_store_explicit(&osd_leds_enabled, enabled ? 1 : 0, memory_order_relaxed);
}

int osd_leds_is_enabled(void) {
  return atomic_load_explicit(&osd_leds_enabled, memory_order_relaxed) != 0;
}

void osd_led_piscsi_host_pulse(void) {
  if (!osd_leds_is_enabled()) {
    return;
  }
  osd_led_pulse_host_ms(osd_led_now_ms());
}

void osd_led_piscsi_unit_pulse_read(int unit) {
  uint32_t now_ms;
  if (!osd_leds_is_enabled()) {
    return;
  }
  now_ms = osd_led_now_ms();
  osd_led_pulse_host_ms(now_ms);
  if (unit < 0 || unit >= OSD_LEDS_UNIT_COUNT) {
    return;
  }
  atomic_store_explicit(&unit_read_until_ms[unit], now_ms + OSD_LED_HOLD_MS, memory_order_relaxed);
}

void osd_led_piscsi_unit_pulse_write(int unit) {
  uint32_t now_ms;
  if (!osd_leds_is_enabled()) {
    return;
  }
  now_ms = osd_led_now_ms();
  osd_led_pulse_host_ms(now_ms);
  if (unit < 0 || unit >= OSD_LEDS_UNIT_COUNT) {
    return;
  }
  atomic_store_explicit(&unit_write_until_ms[unit], now_ms + OSD_LED_HOLD_MS, memory_order_relaxed);
}

void osd_led_piscsi_set_unit_present(int unit, int present) {
  uint32_t bit;
  if (unit < 0 || unit >= OSD_LEDS_UNIT_COUNT) {
    return;
  }
  bit = (uint32_t)(1u << (uint32_t)unit);
  if (present) {
    atomic_fetch_or_explicit(&unit_present_mask, bit, memory_order_relaxed);
  } else {
    atomic_fetch_and_explicit(&unit_present_mask, ~bit, memory_order_relaxed);
  }
}

uint32_t osd_led_piscsi_get_unit_present_mask(void) {
  return atomic_load_explicit(&unit_present_mask, memory_order_relaxed);
}

void osd_leds_get_states(uint64_t now_ns, enum osd_led_state out_states[OSD_LEDS_TOTAL_COUNT]) {
  uint32_t now_ms;
  if (!out_states) {
    return;
  }

  if (!osd_leds_is_enabled()) {
    for (int i = 0; i < OSD_LEDS_TOTAL_COUNT; i++) {
      out_states[i] = LED_IDLE;
    }
    return;
  }

  if (now_ns == 0) {
    now_ms = osd_led_now_ms();
  } else {
    now_ms = osd_led_ns_to_ms(now_ns);
  }

  uint32_t host_until = atomic_load_explicit(&host_busy_until_ms, memory_order_relaxed);
  out_states[0] = osd_led_expiry_active(host_until, now_ms) ? LED_BUSY : LED_IDLE;

  for (int i = 0; i < OSD_LEDS_UNIT_COUNT; i++) {
    uint32_t read_until = atomic_load_explicit(&unit_read_until_ms[i], memory_order_relaxed);
    uint32_t write_until = atomic_load_explicit(&unit_write_until_ms[i], memory_order_relaxed);
    int read_active = osd_led_expiry_active(read_until, now_ms);
    int write_active = osd_led_expiry_active(write_until, now_ms);
    if (write_active) {
      out_states[i + 1] = LED_WRITE;
    } else if (read_active) {
      out_states[i + 1] = LED_READ;
    } else {
      out_states[i + 1] = LED_IDLE;
    }
  }
}
