// src/gpio/ps_protocol_kmod.c - Kernel module backend implementation
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Include our UAPI header
#include "pistorm.h"

static int ps_fd = -1;

static int ps_open_dev(void) {
    if (ps_fd >= 0) return 0;
    ps_fd = open("/dev/pistorm0", O_RDWR | O_CLOEXEC);
    return (ps_fd >= 0) ? 0 : -1;
}

// Define the global gpio pointer as expected by the emulator
// This will be a dummy pointer that we intercept with our functions
volatile unsigned int* gpio = (volatile unsigned int*)0xDEADBEEF;  // Dummy address

// Helper functions for GPIO operations (when using kernel module backend)
void ps_gpio_set_input(int gpio_pin) {
    // In kernel module backend, this would send an ioctl to configure pin as input
    if (ps_open_dev() < 0) return;
    struct pistorm_busop op = {
        .addr   = 0xDEADBEEF,  // Special address for GPIO setup
        .value  = (gpio_pin << 16) | 0x0001,  // Pin number and input flag
        .width  = PISTORM_W32,
        .is_read= 0,
        .flags  = 0x01,  // Special flag for GPIO setup
    };
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
}

void ps_gpio_set_output(int gpio_pin) {
    // In kernel module backend, this would send an ioctl to configure pin as output
    if (ps_open_dev() < 0) return;
    struct pistorm_busop op = {
        .addr   = 0xDEADBEEF,  // Special address for GPIO setup
        .value  = (gpio_pin << 16) | 0x0002,  // Pin number and output flag
        .width  = PISTORM_W32,
        .is_read= 0,
        .flags  = 0x01,  // Special flag for GPIO setup
    };
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
}

void ps_gpio_set_alt(int gpio_pin, int alt_func) {
    // In kernel module backend, this would send an ioctl to configure pin as alt function
    if (ps_open_dev() < 0) return;
    struct pistorm_busop op = {
        .addr   = 0xDEADBEEF,  // Special address for GPIO setup
        .value  = (gpio_pin << 16) | (alt_func & 0xFF),  // Pin number and alt function
        .width  = PISTORM_W32,
        .is_read= 0,
        .flags  = 0x02,  // Special flag for alt function setup
    };
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
}

unsigned int ps_gpio_pull_read(void) {
    // In kernel module backend, this would send an ioctl to read pull state
    if (ps_open_dev() < 0) return 0;
    struct pistorm_busop op = {
        .addr   = 0xDEADBEEF,  // Special address for GPIO setup
        .value  = 0x03,  // Pull read command
        .width  = PISTORM_W32,
        .is_read= 1,
        .flags  = 0x03,  // Special flag for pull read
    };
    int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
    return (rc == 0) ? op.value : 0;
}

unsigned int ps_gpio_pullclk0_read(void) {
    // In kernel module backend, this would send an ioctl to read pull clock state
    if (ps_open_dev() < 0) return 0;
    struct pistorm_busop op = {
        .addr   = 0xDEADBEEF,  // Special address for GPIO setup
        .value  = 0x04,  // Pull clock read command
        .width  = PISTORM_W32,
        .is_read= 1,
        .flags  = 0x04,  // Special flag for pull clock read
    };
    int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
    return (rc == 0) ? op.value : 0;
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

unsigned int ps_read_8(unsigned int addr) {
    if (ps_open_dev() < 0) return 0;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = 0,
        .width  = PISTORM_W8,
        .is_read= 1,
        .flags  = 0,
    };
    int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
    return (rc == 0) ? (op.value & 0xFF) : 0;
}

unsigned int ps_read_16(unsigned int addr) {
    if (ps_open_dev() < 0) return 0;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = 0,
        .width  = PISTORM_W16,
        .is_read= 1,
        .flags  = 0,
    };
    int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
    return (rc == 0) ? (op.value & 0xFFFF) : 0;
}

unsigned int ps_read_32(unsigned int addr) {
    if (ps_open_dev() < 0) return 0;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = 0,
        .width  = PISTORM_W32,
        .is_read= 1,
        .flags  = 0,
    };
    int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
    return (rc == 0) ? op.value : 0;
}

void ps_write_8(unsigned int addr, unsigned int data) {
    if (ps_open_dev() < 0) return;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = data,
        .width  = PISTORM_W8,
        .is_read= 0,
        .flags  = 0,
    };
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
}

void ps_write_16(unsigned int addr, unsigned int data) {
    if (ps_open_dev() < 0) return;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = data,
        .width  = PISTORM_W16,
        .is_read= 0,
        .flags  = 0,
    };
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
}

void ps_write_32(unsigned int addr, unsigned int data) {
    if (ps_open_dev() < 0) return;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = data,
        .width  = PISTORM_W32,
        .is_read= 0,
        .flags  = 0,
    };
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
}

unsigned int ps_read_status_reg(void) {
    return ps_read_16(0x00BFE003);  // STATUS register address
}

void ps_write_status_reg(unsigned int value) {
    ps_write_16(0x00BFE003, value);  // STATUS register address
}

unsigned int ps_get_ipl_zero(void) {
    // Read the IPL_ZERO state - this would typically be from CIAA PRA
    unsigned int v = ps_read_8(0x00BFE001);  // CIAA PRA
    // IPL_ZERO is active low, so return 1 if bit is clear
    return (v & 0x02) ? 0 : 1;  // Assuming IPL_ZERO is bit 1 in CIAA PRA
}