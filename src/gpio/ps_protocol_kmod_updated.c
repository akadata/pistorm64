// src/gpio/ps_protocol_kmod.c (updated version)
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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

// Function to read the status register (equivalent to *(gpio + 13))
unsigned int ps_read_status_reg(void) {
    if (ps_open_dev() < 0) return 0;
    struct pistorm_busop op = {
        .addr   = 0xBFE003,  // CIAA PRA (contains IPL_ZERO and other status)
        .value  = 0,
        .width  = PISTORM_W8,
        .is_read= 1,
        .flags  = 0,
    };
    int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
    if (rc == 0) return op.value;
    return 0;
}

// Function to get IPL_ZERO state (extracted from status register)
unsigned int ps_get_ipl_zero(void) {
    unsigned int status = ps_read_status_reg();
    // IPL_ZERO is active low, so return 1 if bit is clear
    return (status & (1 << 1)) ? 0 : 1;  // Assuming IPL_ZERO is bit 1 in CIAA PRA
}

// Function to check if transaction is in progress (TXN_IN_PROGRESS)
// This would need to be implemented based on how TXN_IN_PROGRESS is monitored
int ps_is_txn_in_progress(void) {
    // This is a complex case - TXN_IN_PROGRESS is likely a GPIO input
    // that indicates when the Amiga bus transaction is in progress
    // We'd need to implement this in the kernel module
    unsigned int status = ps_read_status_reg();
    // For now, assuming it's available through a special register or status check
    // In a real implementation, this would be handled in the kernel module
    return 0; // Placeholder - needs proper implementation
}

static int ps_kmod_busop(int is_read, int width, unsigned addr, unsigned *val) {
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
    ps_kmod_busop(1, PISTORM_W8, addr, &v); 
    return v & 0xff; 
}

unsigned ps_read_16(unsigned addr) { 
    unsigned v = 0; 
    ps_kmod_busop(1, PISTORM_W16, addr, &v); 
    return v & 0xffff; 
}

unsigned ps_read_32(unsigned addr) { 
    unsigned v = 0; 
    ps_kmod_busop(1, PISTORM_W32, addr, &v); 
    return v; 
}

void ps_write_8(unsigned addr, unsigned v)  { 
    ps_kmod_busop(0, PISTORM_W8, addr, &v); 
}

void ps_write_16(unsigned addr, unsigned v) { 
    ps_kmod_busop(0, PISTORM_W16, addr, &v); 
}

void ps_write_32(unsigned addr, unsigned v) { 
    ps_kmod_busop(0, PISTORM_W32, addr, &v); 
}

// Additional functions that might be needed
unsigned ps_read_status_reg_impl(void) {
    unsigned v = 0;
    ps_kmod_busop(1, PISTORM_W16, 0x00BFE003, &v);  // STATUS register address
    return v;
}

void ps_write_status_reg_impl(unsigned int value) {
    ps_kmod_busop(0, PISTORM_W16, 0x00BFE003, &value);  // STATUS register address
}