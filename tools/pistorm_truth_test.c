// Minimal PiStorm kmod validation
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "include/uapi/linux/pistorm.h"

static int ps_read16(int fd, uint32_t addr, uint16_t *out) {
    struct pistorm_busop op = {
        .addr = addr,
        .value = 0,
        .width = PISTORM_W16,
        .is_read = 1,
        .flags = 0,
    };
    if (ioctl(fd, PISTORM_IOC_BUSOP, &op) < 0)
        return -1;
    *out = (uint16_t)(op.value & 0xffffu);
    return 0;
}

static bool seen(const uint16_t *vals, size_t count, uint16_t v) {
    for (size_t i = 0; i < count; i++)
        if (vals[i] == v)
            return true;
    return false;
}

int main(int argc, char **argv) {
    int fd, rc = 0;
    int samples = 1000;
    uint16_t vmin = 0xffff, vmax = 0x0000;
    uint16_t distinct[6000];
    size_t distinct_cnt = 0;

    if (argc > 1) {
        samples = atoi(argv[1]);
        if (samples < 1) samples = 500;
        if (samples > 5000) samples = 5000;
    }

    fd = open("/dev/pistorm", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open(/dev/pistorm)");
        return 1;
    }

    if (ioctl(fd, PISTORM_IOC_SETUP) < 0) {
        perror("PISTORM_IOC_SETUP");
        close(fd);
        return 1;
    }

    for (int i = 0; i < samples; i++) {
        uint16_t v = 0;
        if (ps_read16(fd, 0xdff006, &v) < 0) {
            perror("VHPOSR");
            rc = 1;
            break;
        }
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        if (!seen(distinct, distinct_cnt, v) && distinct_cnt < 6000)
            distinct[distinct_cnt++] = v;
    }

    printf("VHPOSR samples=%d distinct=%zu min=0x%04x max=0x%04x\n",
           samples, distinct_cnt, vmin, vmax);

    if (distinct_cnt <= 1) {
        fprintf(stderr, "VHPOSR did not change; bus may be floating/stalled.\n");
        rc = 1;
    }

    uint16_t regs[3] = {0};
    const uint32_t addrs[3] = {0xdff002, 0xdff01e, 0xbfe001};
    const char *names[3] = {"DMACONR", "INTREQR", "CIAA_PRA"};

    for (size_t i = 0; i < 3; i++) {
        if (ps_read16(fd, addrs[i], &regs[i]) < 0) {
            fprintf(stderr, "read %s failed: %s\n", names[i], strerror(errno));
            rc = 1;
        }
    }

    if ((regs[0] == 0x00ff && regs[1] == 0x00ff && regs[2] == 0x00ff) ||
        (regs[0] == 0 && regs[1] == 0 && regs[2] == 0)) {
        fprintf(stderr, "Sampled registers look floating or zeroed.\n");
        rc = 1;
    }

    printf("DMACONR=0x%04x INTREQR=0x%04x CIAA_PRA=0x%04x\n",
           regs[0], regs[1], regs[2]);

    close(fd);
    return rc;
}
