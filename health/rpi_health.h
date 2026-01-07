// rpi_health.h
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t throttled;   // flags from get_throttled
    float    temp_c;      // SoC temperature
    uint32_t arm_hz;      // ARM clock
    uint32_t core_uv;     // core voltage in microvolts (optional)
} rpi_health_t;

int rpi_read_health(rpi_health_t *out);
const char *rpi_throttled_flags_to_string(uint32_t flags);

