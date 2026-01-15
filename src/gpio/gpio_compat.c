/*
 * GPIO compatibility layer implementation for kernel module backend
 * This simulates direct GPIO register access but routes operations through the kernel module
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pistorm.h"  // UAPI header
#include "gpio_compat.h"

// Global gpio pointer - this is what the emulator expects
volatile unsigned int* gpio = (volatile unsigned int*)0xDEADBEEF;  // Fake address to satisfy compiler

static int ps_fd = -1;

static int ps_open_dev(void) {
    if (ps_fd >= 0) return 0;
    ps_fd = open("/dev/pistorm0", O_RDWR | O_CLOEXEC);
    return (ps_fd >= 0) ? 0 : -1;
}

int init_gpio_compat_layer(void) {
    return ps_open_dev();
}

// This function simulates *(gpio + offset) for reading
unsigned int gpio_read_reg(int reg_offset) {
    if (ps_open_dev() < 0) return 0;
    
    // This is a simplified implementation - in reality, we'd need to map
    // the register offset to the appropriate operation
    // For now, we'll handle the specific case of reading GPIO status
    
    // The emulator typically reads from *(gpio + 13) which corresponds to GPLEV0
    // This gives the current state of GPIO pins
    if (reg_offset == 13) {  // GPLEV0 register access
        // For now, return a default value that indicates pins are in expected state
        // In a real implementation, we'd need to query the actual pin states
        // through the kernel module
        
        // This is a placeholder - we need to implement a proper way to get pin states
        struct pistorm_busop op = {
            .addr   = 0xDEADBEEF,  // Special address to indicate pin state query
            .value  = 0,
            .width  = PISTORM_W32,
            .is_read= 1,
            .flags  = 0,
        };
        
        // For now, return a value that indicates no transaction in progress and IPL zero
        // This is a simplified placeholder implementation
        unsigned int result = 0;
        
        // In a real implementation, we'd need to have the kernel module
        // provide a way to read the current state of the GPIO pins
        // For now, let's return a value that makes sense for the emulator
        result = 0xFFFFEC;  // Default value that would indicate pins are in expected state
        
        return result;
    }
    
    // For other registers, we'd need specific handling
    return 0xFFFFFFFF;  // Return all bits high as default
}

// This function simulates *(gpio + offset) = value for writing
void gpio_write_reg(int reg_offset, unsigned int value) {
    if (ps_open_dev() < 0) return;
    
    // This is a simplified implementation - in reality, we'd need to map
    // the register offset and value to the appropriate operation
    // For now, this is a placeholder
    
    struct pistorm_busop op = {
        .addr   = 0xDEADBEEF + reg_offset,  // Special address to indicate register write
        .value  = value,
        .width  = PISTORM_W32,
        .is_read= 0,
        .flags  = 0,
    };
    
    // In a real implementation, we'd need to handle different register writes appropriately
    // For now, this is just a placeholder
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
}

// Additional helper functions for the emulator
unsigned int ps_read_status_reg(void) {
    if (ps_open_dev() < 0) return 0;
    struct pistorm_busop op = {
        .addr   = 0x00BFE003,  // STATUS register
        .value  = 0,
        .width  = PISTORM_W16,
        .is_read= 1,
        .flags  = 0,
    };
    int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
    return (rc == 0) ? op.value : 0;
}

void ps_write_status_reg(unsigned int value) {
    if (ps_open_dev() < 0) return;
    struct pistorm_busop op = {
        .addr   = 0x00BFE003,  // STATUS register
        .value  = value,
        .width  = PISTORM_W16,
        .is_read= 0,
        .flags  = 0,
    };
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
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

unsigned ps_read_8(unsigned addr) {
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

unsigned ps_read_16(unsigned addr) {
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

unsigned ps_read_32(unsigned addr) {
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

void ps_write_8(unsigned addr, unsigned value) {
    if (ps_open_dev() < 0) return;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = value,
        .width  = PISTORM_W8,
        .is_read= 0,
        .flags  = 0,
    };
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
}

void ps_write_16(unsigned addr, unsigned value) {
    if (ps_open_dev() < 0) return;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = value,
        .width  = PISTORM_W16,
        .is_read= 0,
        .flags  = 0,
    };
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
}

void ps_write_32(unsigned addr, unsigned value) {
    if (ps_open_dev() < 0) return;
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = value,
        .width  = PISTORM_W32,
        .is_read= 0,
        .flags  = 0,
    };
    ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
}

// Function to simulate the specific access pattern used by emulator
// This handles the *(gpio + 13) access pattern
unsigned int ps_get_gpio_register(int offset) {
    if (offset == 13) {
        // This is the GPLEV0 access pattern used by emulator
        // It checks for TXN_IN_PROGRESS and IPL_ZERO
        // We need to return a value that represents the current pin states
        
        // For now, let's implement a more realistic version
        // In a real implementation, the kernel module would need to provide
        // a way to query the current state of the GPIO pins
        struct pistorm_busop op = {
            .addr   = 0xBADBAD00,  // Special address to indicate pin state query
            .value  = 0,
            .width  = PISTORM_W32,
            .is_read= 1,
            .flags  = 0x01,  // Special flag to indicate pin state query
        };
        
        int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
        if (rc == 0) {
            return op.value;
        } else {
            // Default: no transaction in progress, IPL not zero
            return 0xFFFFEC;  // This would indicate TXN_IN_PROGRESS = 0 and other pins as expected
        }
    }
    return 0xFFFFFFFF;  // Default for other registers
}