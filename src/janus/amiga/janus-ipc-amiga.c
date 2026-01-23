// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include "janus-ipc.h"

// For Amiga compatibility, we'll use a simplified approach
// In a real Amiga environment, this would use proper Amiga libraries

// External references to pistorm-dev functions (these would be defined elsewhere in the Amiga build)
#ifdef __amigaos__
void handle_pistorm_dev_write(uint32_t addr, uint32_t val, uint8_t type);
uint32_t handle_pistorm_dev_read(uint32_t addr, uint8_t type);
#endif

// Ring buffer structure for Janus IPC
struct janus_ring {
    volatile uint16_t write_idx;
    volatile uint16_t read_idx;
    uint16_t size;
    uint16_t flags;
    uint8_t data[1];
};

// Global variables for Janus IPC
static uintptr_t janus_ring_ptr = 0;
static uint16_t janus_ring_size = 0;
static uint16_t janus_ring_flags = 0;
static bool janus_initialized = false;

// Helper functions for big-endian operations
static uint16_t read_be16(const uint8_t* ptr) {
    return (uint16_t)((ptr[0] << 8) | ptr[1]);
}

static uint32_t read_be32(const uint8_t* ptr) {
    return ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | 
           ((uint32_t)ptr[2] << 8) | ((uint32_t)ptr[3]);
}

static void write_be16(uint8_t* ptr, uint16_t val) {
    ptr[0] = (uint8_t)(val >> 8);
    ptr[1] = (uint8_t)(val & 0xFF);
}

static void write_be32(uint8_t* ptr, uint32_t val) {
    ptr[0] = (uint8_t)(val >> 24);
    ptr[1] = (uint8_t)((val >> 16) & 0xFF);
    ptr[2] = (uint8_t)((val >> 8) & 0xFF);
    ptr[3] = (uint8_t)(val & 0xFF);
}

// Calculate used space in ring buffer
static uint16_t janus_ring_used(uint16_t write_idx, uint16_t read_idx, uint16_t ring_size) {
    if (write_idx >= read_idx) {
        return (uint16_t)(write_idx - read_idx);
    }
    return (uint16_t)(ring_size - (read_idx - write_idx));
}

// Calculate free space in ring buffer
static uint16_t janus_ring_free(uint16_t write_idx, uint16_t read_idx, uint16_t ring_size) {
    return (uint16_t)(ring_size - janus_ring_used(write_idx, read_idx, ring_size) - 1);
}

// Read data from ring buffer
static void janus_ring_read(const uint8_t* data, uint16_t ring_size, uint16_t read_idx,
                            uint8_t* dst, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        dst[i] = data[(read_idx + i) % ring_size];
    }
}

// Write data to ring buffer
static uint16_t janus_ring_write(struct janus_ring* ring, const uint8_t* src, uint16_t len) {
    uint16_t free_space = janus_ring_free(ring->write_idx, ring->read_idx, ring->size);
    uint16_t write_idx = ring->write_idx;
    uint16_t first;

    if (len == 0 || len > free_space) {
        return 0;
    }

    first = (uint16_t)(ring->size - write_idx);
    if (first > len) {
        first = len;
    }

    // Copy first part
    memcpy((void*)(ring->data + write_idx), (void*)src, first);
    
    // Copy remaining part if needed (wraparound)
    if (len > first) {
        memcpy((void*)ring->data, (void*)(src + first), (uint16_t)(len - first));
    }

    ring->write_idx = (uint16_t)((write_idx + len) % ring->size);
    return len;
}

// Initialize the Janus IPC system
bool janus_ipc_init(uintptr_t base_addr, uint16_t size, uint16_t flags) {
    if (size < 64) {  // Minimum reasonable size
        return false;
    }

    // Store the ring buffer parameters
    janus_ring_ptr = base_addr;
    janus_ring_size = size;
    janus_ring_flags = flags;
    
    // Initialize the ring buffer structure in memory
    uint8_t* base = (uint8_t*)base_addr;
    if (base != NULL) {
        write_be16((uint8_t*)&base[0], 0);      // write_idx = 0
        write_be16((uint8_t*)&base[2], 0);      // read_idx = 0
        write_be16((uint8_t*)&base[4], size);   // size
        write_be16((uint8_t*)&base[6], flags);  // flags
    }
    
    janus_initialized = true;
    return true;
}

// Send a message to the Pi side
uint16_t janus_send_message(uint16_t cmd, const uint8_t* payload, uint16_t payload_len) {
    if (!janus_initialized || payload_len > (JANUS_MSG_HEADER_LEN + 256)) {
        return 0;
    }

    // Prepare the message with header
    uint8_t message[JANUS_MSG_HEADER_LEN + 256];
    write_be16(&message[JANUS_OFF_CMD], cmd);
    write_be16(&message[JANUS_OFF_LEN], payload_len);
    
    if (payload && payload_len > 0) {
        memcpy(&message[JANUS_MSG_HEADER_LEN], payload, payload_len);
    }

    // Get the ring buffer base
    uint8_t* base = (uint8_t*)janus_ring_ptr;
    struct janus_ring* ring = (struct janus_ring*)base;
    
    // Write the message to the ring buffer
    uint16_t total_len = JANUS_MSG_HEADER_LEN + payload_len;
    uint16_t written = janus_ring_write(ring, message, total_len);
    
    if (written == total_len) {
        // Kick the Pi side to process the message
#ifdef __amigaos__
        handle_pistorm_dev_write(PI_DBG_VAL1, 1, 0);  // Use debug register as doorbell
#else
        // For non-pistorm builds, just print a message
        printf("Message sent to ring buffer, would kick Pi side in real implementation\n");
#endif
    }
    
    return written;
}

// Process incoming messages from the Pi side
void janus_process_messages(void) {
    if (!janus_initialized || janus_ring_ptr == 0) {
        return;
    }

    uint8_t* base = (uint8_t*)janus_ring_ptr;
    uint16_t write_idx = read_be16(base + 0);
    uint16_t read_idx = read_be16(base + 2);
    uint16_t size = read_be16(base + 4);
    uint16_t ring_size = janus_ring_size ? janus_ring_size : size;

    if (ring_size == 0) {
        return;
    }

    uint16_t used = janus_ring_used(write_idx, read_idx, ring_size);

    if (used < JANUS_MSG_HEADER_LEN) {
        return;  // No complete message available
    }

    const uint8_t* data = base + 8;  // Skip header fields

    while (used >= JANUS_MSG_HEADER_LEN) {
        uint8_t header[JANUS_MSG_HEADER_LEN];
        janus_ring_read(data, ring_size, read_idx, header, JANUS_MSG_HEADER_LEN);

        uint16_t cmd = read_be16(header + JANUS_OFF_CMD);
        uint16_t len = read_be16(header + JANUS_OFF_LEN);
        uint16_t total = (uint16_t)(JANUS_MSG_HEADER_LEN + len);

        if (len == 0 || total > ring_size) {
            break;  // Invalid message
        }

        if (used < total) {
            break;  // Not enough data for complete message
        }

        // Process the message based on command type
        if (cmd == JANUS_CMD_FRACTAL && len == JANUS_FRACTAL_PAYLOAD_LEN) {
            // Handle fractal command - this would typically be processed by the Amiga side
            // For now, just acknowledge receipt
            uint8_t msg[JANUS_FRACTAL_MSG_LEN];
            janus_ring_read(data, ring_size, read_idx, msg, total);
            
            // Extract fractal parameters
            uint16_t width = read_be16(msg + JANUS_OFF_WIDTH);
            uint16_t height = read_be16(msg + JANUS_OFF_HEIGHT);
            uint16_t stride = read_be16(msg + JANUS_OFF_STRIDE);
            uint16_t max_iter = read_be16(msg + JANUS_OFF_MAX_ITER);
            uint32_t dst_ptr = read_be32(msg + JANUS_OFF_DST_PTR);
            uint32_t status_ptr = read_be32(msg + JANUS_OFF_STATUS_PTR);
            int32_t cx = (int32_t)read_be32(msg + JANUS_OFF_CX);
            int32_t cy = (int32_t)read_be32(msg + JANUS_OFF_CY);
            uint32_t scale = read_be32(msg + JANUS_OFF_SCALE);
            
            // For now, just log the received fractal command
            printf("JANUS: Received fractal cmd - w:%u h:%u stride:%u iter:%u dst:$%X\n", 
                   width, height, stride, max_iter, dst_ptr);
        } else if (cmd == JANUS_CMD_TEXT) {
            // Handle text command
            uint8_t text_payload[len + 1];
            janus_ring_read(data, ring_size, read_idx + JANUS_MSG_HEADER_LEN, text_payload, len);
            text_payload[len] = '\0';  // Null terminate
            
            printf("JANUS: Received text: %s\n", text_payload);
        } else {
            printf("JANUS: Unknown command %u, len %u\n", cmd, len);
        }

        // Move read index past this message
        read_idx = (uint16_t)((read_idx + total) % ring_size);
        used = janus_ring_used(write_idx, read_idx, ring_size);
    }

    // Update the read index in the ring buffer
    write_be16(base + 2, read_idx);
}

// Get the current status of the Janus IPC system
bool janus_get_status(void) {
    return janus_initialized;
}

// Get free space in the ring buffer
uint16_t janus_get_free_space(void) {
    if (!janus_initialized) {
        return 0;
    }
    
    uint8_t* base = (uint8_t*)janus_ring_ptr;
    uint16_t write_idx = read_be16(base + 0);
    uint16_t read_idx = read_be16(base + 2);
    uint16_t size = read_be16(base + 4);
    uint16_t ring_size = janus_ring_size ? janus_ring_size : size;
    
    return janus_ring_free(write_idx, read_idx, ring_size);
}

// Send a simple text message
uint16_t janus_send_text(const char* text) {
    uint16_t len = strlen(text);
    if (len > 255) len = 255;  // Limit text length
    
    return janus_send_message(JANUS_CMD_TEXT, (const uint8_t*)text, len);
}

// Send a fractal command
uint16_t janus_send_fractal(uint16_t width, uint16_t height, uint16_t stride, 
                           uint16_t max_iter, uintptr_t dst_ptr, uintptr_t status_ptr,
                           int32_t cx, int32_t cy, uint32_t scale) {
    uint8_t payload[JANUS_FRACTAL_PAYLOAD_LEN];
    
    write_be16(&payload[JANUS_OFF_WIDTH], width);
    write_be16(&payload[JANUS_OFF_HEIGHT], height);
    write_be16(&payload[JANUS_OFF_STRIDE], stride);
    write_be16(&payload[JANUS_OFF_MAX_ITER], max_iter);
    write_be32(&payload[JANUS_OFF_DST_PTR], (uint32_t)dst_ptr);
    write_be32(&payload[JANUS_OFF_STATUS_PTR], (uint32_t)status_ptr);
    write_be32(&payload[JANUS_OFF_CX], (uint32_t)cx);
    write_be32(&payload[JANUS_OFF_CY], (uint32_t)cy);
    write_be32(&payload[JANUS_OFF_SCALE], scale);
    
    return janus_send_message(JANUS_CMD_FRACTAL, payload, JANUS_FRACTAL_PAYLOAD_LEN);
}

// Enhanced Amiga-style message handling functions
// These functions allow integration with Amiga's Exec message system

// Create a message port for Janus IPC
struct MsgPort *janus_create_msg_port(const char *name, int16_t pri) {
    // This would create a proper Amiga message port
    // For now, we'll simulate the functionality
    printf("Creating Janus message port: %s with priority %d\n", name, pri);
    return NULL; // Placeholder
}

// Send a message to a remote processor (could be on Pi, network, etc.)
uint16_t janus_send_remote_message(uint16_t cmd, const uint8_t* payload, uint16_t payload_len, 
                                   uintptr_t remote_addr) {
    // This would send a message to a remote processor
    // Could be implemented over TCP/IP, UDP, or other protocols
    printf("Sending remote message to $%lX, cmd: %u, len: %u\n", (unsigned long)remote_addr, cmd, payload_len);
    
    // For now, just send to the local Pi side
    return janus_send_message(cmd, payload, payload_len);
}

// Register a callback for when messages arrive
void janus_set_message_callback(janus_msg_callback_t callback) {
    // This would register a callback function to handle incoming messages
    (void)callback; // Suppress unused parameter warning
    printf("Setting message callback\n");
}