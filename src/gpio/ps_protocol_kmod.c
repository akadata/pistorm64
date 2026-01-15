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
#include "pistorm.h"   // This should be the copy from our repo

static int ps_fd = -1;

static int ps_open_dev(void) {
    if (ps_fd >= 0) return 0;
    ps_fd = open("/dev/pistorm0", O_RDWR | O_CLOEXEC);
    return (ps_fd >= 0) ? 0 : -1;
}

int ps_setup_protocol(void) {
    if (ps_open_dev() < 0) return -1;
    return ioctl(ps_fd, PISTORM_IOC_SETUP);
}

int ps_reset_state_machine(void) {
    if (ps_open_dev() < 0) return -1;
    return ioctl(ps_fd, PISTORM_IOC_RESET_SM);
}

int ps_pulse_reset(void) {
    if (ps_open_dev() < 0) return -1;
    return ioctl(ps_fd, PISTORM_IOC_PULSE_RESET);
}

static int ps_busop(int is_read, int width, unsigned addr, unsigned *val) {
    if (ps_open_dev() < 0) return -1;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = val ? *val : 0,
        .width  = (unsigned char)width,
        .is_read= (unsigned char)is_read,
        .flags  = 0,
    };
    int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
    if (rc == 0 && is_read && val) *val = op.value;
    return rc;
}

unsigned ps_read_8(unsigned addr)  { 
    unsigned v = 0; 
    ps_busop(1, PISTORM_W8, addr, &v); 
    return v & 0xff; 
}

unsigned ps_read_16(unsigned addr) { 
    unsigned v = 0; 
    ps_busop(1, PISTORM_W16, addr, &v); 
    return v & 0xffff; 
}

unsigned ps_read_32(unsigned addr) { 
    unsigned v = 0; 
    ps_busop(1, PISTORM_W32, addr, &v); 
    return v; 
}

void ps_write_8(unsigned addr, unsigned v)  { 
    ps_busop(0, PISTORM_W8, addr, &v); 
}

void ps_write_16(unsigned addr, unsigned v) { 
    ps_busop(0, PISTORM_W16, addr, &v); 
}

void ps_write_32(unsigned addr, unsigned v) { 
    ps_busop(0, PISTORM_W32, addr, &v); 
}

// Additional functions that might be needed
unsigned ps_read_status_reg(void) {
    unsigned v = 0;
    ps_busop(1, PISTORM_W16, 0x00BFE003, &v);  // STATUS register address
    return v;
}

void ps_write_status_reg(unsigned int value) {
    ps_busop(0, PISTORM_W16, 0x00BFE003, &value);  // STATUS register address
}

unsigned ps_get_ipl_zero(void) {
    // This would read the IPL_ZERO pin state
    // Implementation depends on how IPL_ZERO is mapped
    unsigned v = 0;
    ps_busop(1, PISTORM_W8, 0x00BFE001, &v);  // CIAA PRA
    return (v & 0x02) ? 0 : 1;  // Active low
}