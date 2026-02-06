// SPDX-License-Identifier: MIT

/*
 * Userspace GPIO implementation of PiStorm protocol
 * Used when PISTORM_KMOD=0 to provide compatibility without kernel module
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "ps_protocol.h"
#include "../log.h"

// For userspace implementation, we'll simulate the PiStorm bus using memory mapping
// This is a simplified implementation that doesn't actually interface with hardware

// Simulated registers
static uint32_t simulated_regs[4] = {0}; // DATA, ADDR_LO, ADDR_HI, STATUS
static uint32_t simulated_bus_data = 0;

// For userspace implementation, we'll use a dummy approach
// In a real userspace implementation, we'd need to interface with the Pi's GPIO directly
// This is a placeholder that simulates basic functionality

uint8_t ps_read_8(uint32_t addr) {
    // Simulate reading from the PiStorm bus
    // In a real implementation, this would interface with GPIO
    (void)addr; // Suppress unused parameter warning
    LOG_DEBUG("[GPIO-USERSPACE] read8 at 0x%08X\n", addr);
    return (uint8_t)(simulated_bus_data & 0xFF);
}

uint16_t ps_read_16(uint32_t addr) {
    // Simulate reading from the PiStorm bus
    (void)addr; // Suppress unused parameter warning
    LOG_DEBUG("[GPIO-USERSPACE] read16 at 0x%08X\n", addr);
    return (uint16_t)(simulated_bus_data & 0xFFFF);
}

uint32_t ps_read_32(uint32_t addr) {
    // Simulate reading from the PiStorm bus
    (void)addr; // Suppress unused parameter warning
    LOG_DEBUG("[GPIO-USERSPACE] read32 at 0x%08X\n", addr);
    return simulated_bus_data;
}

void ps_write_8(uint32_t addr, uint8_t v) {
    // Simulate writing to the PiStorm bus
    (void)addr; (void)v; // Suppress unused parameter warnings
    LOG_DEBUG("[GPIO-USERSPACE] write8 at 0x%08X = 0x%02X\n", addr, v);
    simulated_bus_data = (simulated_bus_data & ~0xFF) | v;
}

void ps_write_16(uint32_t addr, uint16_t v) {
    // Simulate writing to the PiStorm bus
    (void)addr; (void)v; // Suppress unused parameter warnings
    LOG_DEBUG("[GPIO-USERSPACE] write16 at 0x%08X = 0x%04X\n", addr, v);
    simulated_bus_data = (simulated_bus_data & ~0xFFFF) | v;
}

void ps_write_32(uint32_t addr, uint32_t v) {
    // Simulate writing to the PiStorm bus
    (void)addr; (void)v; // Suppress unused parameter warnings
    LOG_DEBUG("[GPIO-USERSPACE] write32 at 0x%08X = 0x%08X\n", addr, v);
    simulated_bus_data = v;
}

uint16_t ps_read_status_reg(void) {
    LOG_DEBUG("[GPIO-USERSPACE] read status reg\n");
    return (uint16_t)(simulated_regs[REG_STATUS] & 0xFFFF);
}

void ps_write_status_reg(uint16_t value) {
    LOG_DEBUG("[GPIO-USERSPACE] write status reg = 0x%04X\n", value);
    simulated_regs[REG_STATUS] = (uint32_t)value;
}

void ps_setup_protocol(void) {
    LOG_INFO("[GPIO-USERSPACE] Protocol setup (userspace simulation)\n");
    // Initialize simulated registers
    memset(simulated_regs, 0, sizeof(simulated_regs));
    simulated_bus_data = 0;
}

void ps_reset_state_machine(void) {
    LOG_DEBUG("[GPIO-USERSPACE] Reset state machine\n");
    // Reset simulated state
    simulated_regs[REG_STATUS] = 0;
}

void ps_pulse_reset(void) {
    LOG_DEBUG("[GPIO-USERSPACE] Pulse reset\n");
    // Simulate reset pulse
    simulated_regs[REG_STATUS] |= (1 << PIN_RESET);
    usleep(1000); // 1ms delay
    simulated_regs[REG_STATUS] &= ~(1 << PIN_RESET);
}

void ps_protocol_dump_stats(void) {
    LOG_INFO("[GPIO-USERSPACE] Protocol stats dump (simulated)\n");
    LOG_INFO("[GPIO-USERSPACE] Simulated bus operations: data=0x%08X\n", simulated_bus_data);
}

int ps_flush_batch_queue(void) {
    LOG_DEBUG("[GPIO-USERSPACE] Flush batch queue (no-op in userspace)\n");
    return 0; // No-op in userspace implementation
}

unsigned int ps_get_ipl_zero(void) {
    // Simulate IPL zero status - return 1 if interrupt level is 0
    LOG_DEBUG("[GPIO-USERSPACE] Get IPL zero\n");
    return 1; // Simulate no interrupts pending
}

unsigned int ps_gpio_lev(void) {
    // Simulate GPIO level - return a default value
    LOG_DEBUG("[GPIO-USERSPACE] Get GPIO level\n");
    return simulated_regs[REG_STATUS]; // Return current status register value
}