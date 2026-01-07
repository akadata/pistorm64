#include "health.h"
#include "rpi_health.h"
#include <stdio.h>
#include <math.h>

static int health_inited = 0;
static rpi_health_t last_h;

static void log_health_event(const rpi_health_t *h, const char *why) {
    fprintf(stderr,
        "[HEALTH] %s | throttled=0x%08x (%s) | temp=%.1fC | arm=%u Hz | core=%u uV\n",
        why,
        h->throttled, rpi_throttled_flags_to_string(h->throttled),
        h->temp_c, h->arm_hz, h->core_uv
    );
}

void health_init(void) {
    health_inited = 0;
}

void health_poll_once(void) {
    rpi_health_t h;
    if (rpi_read_health(&h) != 0) return;

    if (!health_inited) {
        last_h = h;
        health_inited = 1;
        log_health_event(&h, "init");
        return;
    }

    const float TEMP_WARN = 75.0f;
    const float TEMP_HYST = 1.0f;

    int changed = 0;
    if (h.throttled != last_h.throttled) changed = 1;
    if (fabsf(h.temp_c - last_h.temp_c) >= 2.0f) changed = 1;
    if (h.arm_hz != last_h.arm_hz) changed = 1;
    if (h.core_uv != last_h.core_uv) changed = 1;

    if (h.throttled != 0) {
        log_health_event(&h, "THROTTLED!=0");
    } else if (h.temp_c >= TEMP_WARN && last_h.temp_c < (TEMP_WARN - TEMP_HYST)) {
        log_health_event(&h, "TEMP_HIGH");
    } else if (changed) {
        log_health_event(&h, "change");
    }

    last_h = h;
}
