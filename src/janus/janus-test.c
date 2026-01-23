// SPDX-License-Identifier: MIT
//
// Simple test program to demonstrate the Janus IPC module usage
// This would typically run on the Amiga side to test communication with the Pi

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "janus-ipc.h"

int main() {
    printf("Janus IPC Module Test\n");
    printf("=====================\n");

    // Initialize the Janus IPC system with a mock ring buffer
    // In a real Amiga environment, this would be actual memory accessible by both sides
    uint8_t mock_ring_buffer[1024];
    uintptr_t ring_addr = (uintptr_t)&mock_ring_buffer;

    printf("Initializing Janus IPC...\n");
    bool init_result = janus_ipc_init(ring_addr, 1024, 0);

    if (!init_result) {
        printf("Failed to initialize Janus IPC!\n");
        return 1;
    }

    printf("Janus IPC initialized successfully.\n");
    printf("Status: %s\n", janus_get_status() ? "active" : "inactive");
    printf("Free space: %d bytes\n", janus_get_free_space());

    // Test sending a text message
    printf("\nTesting text message...\n");
    uint16_t text_result = janus_send_text("Hello from Amiga!");
    printf("Text message sent, result: %d\n", text_result);

    // Test sending a fractal command
    printf("\nTesting fractal command...\n");
    uint16_t fractal_result = janus_send_fractal(
        320,           // width
        240,           // height
        320,           // stride
        100,           // max iterations
        (uintptr_t)0x100000,      // destination pointer
        (uintptr_t)0x200000,      // status pointer
        0x80000000,    // center X (fixed point)
        0x80000000,    // center Y (fixed point)
        0x100000       // scale (fixed point)
    );
    printf("Fractal command sent, result: %d\n", fractal_result);

    // Process any incoming messages
    printf("\nProcessing incoming messages...\n");
    janus_process_messages();

    printf("\nTest completed successfully!\n");
    return 0;
}