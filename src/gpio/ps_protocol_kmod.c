// SPDX-License-Identifier: MIT
//
// Userspace shim that routes all PiStorm bus access through /dev/pistorm0

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ps_protocol.h"
#include <linux/pistorm.h>
#include "src/musashi/m68k.h"

static int ps_fd = -1;
static bool backend_logged;
static volatile unsigned int gpio_shadow[32];
volatile unsigned int *gpio = gpio_shadow; /* legacy pointer used by emulator */

static int ps_open_dev(void) {
    if (ps_fd >= 0) {
        return 0;
    }

    ps_fd = open("/dev/pistorm0", O_RDWR | O_CLOEXEC);
    if (ps_fd < 0) {
        if (!backend_logged) {
            fprintf(stderr,
                    "[ps_protocol] kmod backend selected but /dev/pistorm0 missing (%s)\n",
                    strerror(errno));
            backend_logged = true;
        }
        return -1;
    }

    if (!backend_logged) {
        printf("[ps_protocol] backend=kmod (/dev/pistorm0)\n");
        backend_logged = true;
    }
    return 0;
}

static int ps_simple_ioctl(unsigned long cmd) {
    if (ps_open_dev() < 0) {
        return -1;
    }
    if (ioctl(ps_fd, cmd) < 0) {
        perror("pistorm ioctl");
        return -1;
    }
    return 0;
}

static int ps_bus_ioctl(struct pistorm_busop *op) {
    if (ps_open_dev() < 0) {
        return -1;
    }
    if (ioctl(ps_fd, PISTORM_IOC_BUSOP, op) < 0) {
        perror("PISTORM_IOC_BUSOP");
        return -1;
    }
    return 0;
}

static unsigned int ps_fetch_pins(void) {
    struct pistorm_pins pins;

    if (ps_open_dev() < 0) {
        return 0;
    }
    if (ioctl(ps_fd, PISTORM_IOC_GET_PINS, &pins) == 0) {
        gpio_shadow[13] = pins.gplev0;
        gpio_shadow[14] = pins.gplev1;
        return pins.gplev0;
    }
    return gpio_shadow[13];
}

static unsigned int ps_bus_read(unsigned int addr, unsigned char width) {
    struct pistorm_busop op = {
        .addr = addr,
        .value = 0,
        .width = width,
        .is_read = 1,
        .flags = 0,
    };

    if (ps_bus_ioctl(&op) == 0) {
        return op.value;
    }
    return 0;
}

static void ps_bus_write(unsigned int addr, unsigned int value, unsigned char width) {
    struct pistorm_busop op = {
        .addr = addr,
        .value = value,
        .width = width,
        .is_read = 0,
        .flags = 0,
    };

    (void)ps_bus_ioctl(&op);
}

void ps_setup_protocol(void) {
    ps_simple_ioctl(PISTORM_IOC_SETUP);
}

void ps_reset_state_machine(void) {
    ps_simple_ioctl(PISTORM_IOC_RESET_SM);
}

void ps_pulse_reset(void) {
    ps_simple_ioctl(PISTORM_IOC_PULSE_RESET);
}

unsigned int ps_read_8(unsigned int addr) {
    return ps_bus_read(addr, PISTORM_W8) & 0xffu;
}

unsigned int ps_read_16(unsigned int addr) {
    return ps_bus_read(addr, PISTORM_W16) & 0xffffu;
}

unsigned int ps_read_32(unsigned int addr) {
    return ps_bus_read(addr, PISTORM_W32);
}

void ps_write_8(unsigned int addr, unsigned int data) {
    ps_bus_write(addr, data, PISTORM_W8);
}

void ps_write_16(unsigned int addr, unsigned int data) {
    ps_bus_write(addr, data, PISTORM_W16);
}

void ps_write_32(unsigned int addr, unsigned int data) {
    ps_bus_write(addr, data, PISTORM_W32);
}

unsigned int ps_read_status_reg(void) {
    struct pistorm_busop op = {
        .addr = 0,
        .value = 0,
        .width = PISTORM_W16,
        .is_read = 1,
        .flags = PISTORM_BUSOP_F_STATUS,
    };

    if (ps_bus_ioctl(&op) == 0) {
        return op.value & 0xffffu;
    }
    return 0;
}

void ps_write_status_reg(unsigned int value) {
    struct pistorm_busop op = {
        .addr = 0,
        .value = value,
        .width = PISTORM_W16,
        .is_read = 0,
        .flags = PISTORM_BUSOP_F_STATUS,
    };

    (void)ps_bus_ioctl(&op);
}

unsigned int ps_get_ipl_zero(void) {
    unsigned int level = ps_fetch_pins();
    return level & (1u << PIN_IPL_ZERO);
}

unsigned int ps_gpio_lev(void) {
    return ps_fetch_pins();
}

void ps_update_irq() {
    unsigned int ipl = 0;

    if (!ps_get_ipl_zero()) {
        unsigned int status = ps_read_status_reg();
        ipl = (status & STATUS_MASK_IPL) >> STATUS_SHIFT_IPL;
    }

    m68k_set_irq(ipl);
}
