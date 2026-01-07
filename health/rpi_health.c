// rpi_health.c
#define _GNU_SOURCE
#include "rpi_health.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef _IOWR
#include <linux/ioctl.h>
#endif

#define IOCTL_MBOX_PROPERTY _IOWR(100, 0, char *)

// Property tags
#define TAG_GET_THROTTLED     0x00030046
#define TAG_GET_TEMPERATURE   0x00030006
#define TAG_GET_CLOCK_RATE    0x00030002
#define TAG_GET_VOLTAGE       0x00030003

// IDs
#define TEMP_ID_SOC           0x00000000
#define CLOCK_ID_ARM          0x00000003
#define VOLT_ID_CORE          0x00000001

static int mbox_call(uint32_t *buf, size_t bytes) {
    int fd = open("/dev/vcio", O_RDONLY);
    if (fd < 0) return -1;
    buf[0] = (uint32_t)bytes;
    buf[1] = 0;
    int rc = ioctl(fd, IOCTL_MBOX_PROPERTY, buf);
    close(fd);
    return (rc < 0) ? -2 : 0;
}

static int get_throttled(uint32_t *out) {
    uint32_t buf[7] = {0};
    buf[2] = TAG_GET_THROTTLED;
    buf[3] = 4;
    buf[4] = 0;
    buf[5] = 0;
    buf[6] = 0;
    if (mbox_call(buf, sizeof(buf)) != 0) return -1;
    *out = buf[5];
    return 0;
}

static int get_temperature_mC(uint32_t *out_mC) {
    // returns millidegrees C
    uint32_t buf[8] = {0};
    buf[2] = TAG_GET_TEMPERATURE;
    buf[3] = 8;      // value buffer size
    buf[4] = 0;
    buf[5] = TEMP_ID_SOC;
    buf[6] = 0;      // temp (mC)
    buf[7] = 0;
    if (mbox_call(buf, sizeof(buf)) != 0) return -1;
    *out_mC = buf[6];
    return 0;
}

static int get_clock_rate_hz(uint32_t clock_id, uint32_t *out_hz) {
    uint32_t buf[8] = {0};
    buf[2] = TAG_GET_CLOCK_RATE;
    buf[3] = 8;
    buf[4] = 0;
    buf[5] = clock_id;
    buf[6] = 0; // rate
    buf[7] = 0;
    if (mbox_call(buf, sizeof(buf)) != 0) return -1;
    *out_hz = buf[6];
    return 0;
}

static int get_voltage_uv(uint32_t volt_id, uint32_t *out_uv) {
    // returns microvolts (uV)
    uint32_t buf[8] = {0};
    buf[2] = TAG_GET_VOLTAGE;
    buf[3] = 8;
    buf[4] = 0;
    buf[5] = volt_id;
    buf[6] = 0; // voltage
    buf[7] = 0;
    if (mbox_call(buf, sizeof(buf)) != 0) return -1;
    *out_uv = buf[6];
    return 0;
}

// same decoder as before
const char *rpi_throttled_flags_to_string(uint32_t f) {
    static char s[256];
    s[0] = 0;

    #define APPEND(msg) do { if (s[0]) strncat(s," | ",sizeof(s)-strlen(s)-1); strncat(s,msg,sizeof(s)-strlen(s)-1);} while(0)
    if (f == 0) { snprintf(s, sizeof(s), "OK"); return s; }

    if (f & (1u<<0))  APPEND("UNDER_VOLTAGE_NOW");
    if (f & (1u<<1))  APPEND("FREQ_CAPPED_NOW");
    if (f & (1u<<2))  APPEND("THROTTLED_NOW");
    if (f & (1u<<3))  APPEND("SOFT_TEMP_LIMIT_NOW");
    if (f & (1u<<16)) APPEND("UNDER_VOLTAGE_OCCURRED");
    if (f & (1u<<17)) APPEND("FREQ_CAPPED_OCCURRED");
    if (f & (1u<<18)) APPEND("THROTTLING_OCCURRED");
    if (f & (1u<<19)) APPEND("SOFT_TEMP_LIMIT_OCCURRED");

    char raw[64];
    snprintf(raw, sizeof(raw), "RAW=0x%08x", f);
    APPEND(raw);
    return s;
}

int rpi_read_health(rpi_health_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    uint32_t t;
    if (get_throttled(&out->throttled) != 0) return -2;

    if (get_temperature_mC(&t) == 0) out->temp_c = (float)t / 1000.0f;
    if (get_clock_rate_hz(CLOCK_ID_ARM, &out->arm_hz) == 0) { /* ok */ }
    if (get_voltage_uv(VOLT_ID_CORE, &out->core_uv) == 0) { /* ok */ }

    return 0;
}
