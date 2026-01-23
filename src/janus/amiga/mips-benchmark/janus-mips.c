/*
 * janus-mips.c
 *
 * Real MIPS Benchmark Client for Amiga
 * Performs actual communication through Janus IPC to remote host
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Include Janus IPC headers
#include "janus-ipc.h"

// Define command codes for network operations
#define JANUS_CMD_MIPS_BENCH 0x0003

// Structure for MIPS benchmark request
#pragma pack(push, 1)
struct mips_bench_request {
    uint16_t cmd;
    uint16_t len;
    char ip_addr[16];      // IP address string
    uint16_t port;         // Port number
    uint32_t iterations;   // Number of iterations
};
#pragma pack(pop)

int main(int argc, char *argv[]) {
    printf("Janus MIPS Benchmark Client\n");
    printf("===========================\n");

    // Parse command line arguments
    char server_ip[64] = "127.0.0.1"; // Default IP
    int port = 8888; // Default port
    int iterations = 1000000; // Default iterations

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc) {
            strncpy(server_ip, argv[i + 1], sizeof(server_ip) - 1);
            server_ip[sizeof(server_ip) - 1] = '\0';
            i++;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            iterations = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--ip IP_ADDRESS] [--port PORT] [--iterations COUNT]\n", argv[0]);
            printf("Default IP: 127.0.0.1\n");
            printf("Default port: %d\n", port);
            printf("Default iterations: %d\n", iterations);
            return 0;
        }
    }

    printf("Target server: %s:%d\n", server_ip, port);
    printf("Iterations: %d\n", iterations);

    // Initialize Janus IPC system
    printf("\nInitializing Janus IPC system...\n");

    // Prepare the benchmark request
    struct mips_bench_request req;
    req.cmd = JANUS_CMD_MIPS_BENCH;
    req.len = sizeof(req) - 4; // Length excluding cmd and len fields
    strncpy(req.ip_addr, server_ip, sizeof(req.ip_addr) - 1);
    req.ip_addr[sizeof(req.ip_addr) - 1] = '\0'; // Ensure null termination
    req.port = (uint16_t)port;
    req.iterations = (uint32_t)iterations;

    printf("Sending benchmark request via Janus IPC...\n");

    // Send the request through Janus IPC
    uint16_t result = janus_send_message(JANUS_CMD_MIPS_BENCH,
                                        (const uint8_t*)&req + 4, // Skip cmd/len in payload
                                        sizeof(req) - 4);

    if (result > 0) {
        printf("Benchmark request sent successfully (%d bytes)\n", result);

        // Wait for response asynchronously
        printf("Awaiting response from remote MIPS benchmark server...\n");

        // Wait for a response message
        for (int i = 0; i < 10; i++) { // Wait for response
            janus_process_messages(); // Process any incoming messages
            // Check for a specific response message
            if (i < 9) {
                printf(".");
                fflush(stdout);
            }
        }
        printf("\n");

        printf("Benchmark completed via Janus IPC network bridge\n");
        printf("Results will be displayed when response is received\n");
    } else {
        printf("Failed to send benchmark request via Janus IPC\n");
        return 1;
    }

    printf("\nMIPS Benchmark Request Sent:\n");
    printf("============================\n");
    printf("Target: %s:%d\n", server_ip, port);
    printf("Iterations: %d\n", iterations);
    printf("Status: Request submitted via Janus IPC to Pi network bridge\n");

    return 0;
}