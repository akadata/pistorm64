// SPDX-License-Identifier: MIT

#ifndef OSD_LEDS_H
#define OSD_LEDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OSD_LEDS_UNIT_COUNT 7
#define OSD_LEDS_TOTAL_COUNT (1 + OSD_LEDS_UNIT_COUNT)

enum osd_led_state {
  LED_IDLE = 0,
  LED_READ = 1,
  LED_WRITE = 2,
  LED_BUSY = 3
};

void osd_leds_set_enabled(int enabled);
int osd_leds_is_enabled(void);

void osd_led_piscsi_host_pulse(void);
void osd_led_piscsi_unit_pulse_read(int unit);
void osd_led_piscsi_unit_pulse_write(int unit);
void osd_led_piscsi_set_unit_present(int unit, int present);
uint32_t osd_led_piscsi_get_unit_present_mask(void);

void osd_leds_get_states(uint64_t now_ns, enum osd_led_state out_states[OSD_LEDS_TOTAL_COUNT]);

#ifdef __cplusplus
}
#endif

#endif
