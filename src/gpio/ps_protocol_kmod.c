// src/gpio/ps_protocol_kmod.c
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Include our UAPI header
#include "ps_protocol.h"
#include <linux/pistorm.h>
#include "src/musashi/m68k.h"

static int ps_fd = -1;
static int backend_logged;
static volatile unsigned int gpio_shadow[32];
volatile unsigned int *gpio = gpio_shadow; /* legacy pointer */

static int ps_open_dev(void) {
    if (ps_fd >= 0) return 0;
    ps_fd = open("/dev/pistorm", O_RDWR | O_CLOEXEC);
    if (ps_fd < 0) {
        if (!backend_logged) {
            fprintf(stderr, "[ps_protocol] kmod backend selected but /dev/pistorm missing (%s)\n",
                    strerror(errno));
            backend_logged = 1;
        }
        return -1;
    }
    if (!backend_logged) {
        printf("[ps_protocol] backend=kmod (/dev/pistorm)\n");
        backend_logged = 1;
    }
    return 0;
}

void ps_setup_protocol(void) {
    if (ps_open_dev() < 0) return;
    if (ioctl(ps_fd, PISTORM_IOC_SETUP) < 0)
        perror("PISTORM_IOC_SETUP");
}

void ps_reset_state_machine(void) {
    if (ps_open_dev() < 0) return;
    if (ioctl(ps_fd, PISTORM_IOC_RESET_SM) < 0)
        perror("PISTORM_IOC_RESET_SM");
}

void ps_pulse_reset(void) {
    if (ps_open_dev() < 0) return;
    if (ioctl(ps_fd, PISTORM_IOC_PULSE_RESET) < 0)
        perror("PISTORM_IOC_PULSE_RESET");
}

static int ps_busop(int is_read, int width, unsigned addr, unsigned *val, unsigned short flags) {
    if (ps_open_dev() < 0) return -1;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = val ? *val : 0,
        .width  = (unsigned char)width,
        .is_read= (unsigned char)is_read,
        .flags  = flags,
    };
    int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
    if (rc == 0 && is_read && val) *val = op.value;
    return rc;
}

unsigned ps_read_8(unsigned addr)  { 
    unsigned v = 0; 
    ps_busop(1, PISTORM_W8, addr, &v, 0); 
    return v & 0xff; 
}

unsigned ps_read_16(unsigned addr) { 
    unsigned v = 0; 
    ps_busop(1, PISTORM_W16, addr, &v, 0); 
    return v & 0xffff; 
}

unsigned ps_read_32(unsigned addr) { 
    unsigned v = 0; 
    ps_busop(1, PISTORM_W32, addr, &v, 0); 
    return v; 
}

void ps_write_8(unsigned addr, unsigned v)  { 
    ps_busop(0, PISTORM_W8, addr, &v, 0); 
}

void ps_write_16(unsigned addr, unsigned v) { 
    ps_busop(0, PISTORM_W16, addr, &v, 0); 
}

void ps_write_32(unsigned addr, unsigned v) { 
    ps_busop(0, PISTORM_W32, addr, &v, 0); 
}

// Additional functions that might be needed
unsigned ps_read_status_reg(void) {
    struct pistorm_busop op = {
        .addr = 0,
        .value = 0,
        .width = PISTORM_W16,
        .is_read = 1,
        .flags = PISTORM_BUSOP_F_STATUS,
    };

    if (ps_busop(op.is_read, op.width, op.addr, &op.value, op.flags) == 0)
        return op.value & 0xffffu;
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
    (void)ps_busop(op.is_read, op.width, op.addr, &op.value, op.flags);
}

unsigned ps_get_ipl_zero(void) {
    unsigned int level = ps_gpio_lev();
    return level & (1u << PIN_IPL_ZERO);
}

unsigned int ps_gpio_lev(void) {
    struct pistorm_pins pins;

    if (ps_open_dev() < 0)
        return gpio_shadow[13];
    if (ioctl(ps_fd, PISTORM_IOC_GET_PINS, &pins) == 0) {
        gpio_shadow[13] = pins.gplev0;
        gpio_shadow[14] = pins.gplev1;
    }
    return gpio_shadow[13];
}

void ps_update_irq() {
    unsigned int ipl = 0;

    if (!ps_get_ipl_zero()) {
        unsigned int status = ps_read_status_reg();
        ipl = (status & STATUS_MASK_IPL) >> STATUS_SHIFT_IPL;
    }

    m68k_set_irq(ipl);
}
