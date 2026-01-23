/*
 * janus-network-handler.c
 *
 * Network handler for Janus IPC System
 * Runs on Pi side to handle network communication for Amiga
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>

// Include Janus IPC headers
#include "janus-ipc.h"

// Define command codes
#define JANUS_CMD_MIPS_BENCH 0x0003

// Structure for MIPS benchmark request
struct mips_bench_request {
    char ip_addr[16];      // IP address string
    uint16_t port;         // Port number
    uint32_t iterations;   // Number of iterations
};

// Structure for MIPS benchmark response
struct mips_bench_response {
    uint16_t cmd;
    uint16_t len;
    uint32_t ops_per_second;  // MIPS score
    uint32_t iterations;      // Number of iterations
    uint32_t status;          // Status code
};

// Network handler for MIPS benchmark requests
int handle_mips_benchmark_request(const char* server_ip, int port, int iterations,
                                 struct mips_bench_response* response) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    char request[256];

    printf("Connecting to MIPS benchmark server at %s:%d for %d iterations\n",
           server_ip, port, iterations);

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation error\n");
        if (response) {
            response->status = 1; // Error status
        }
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 address from text to binary
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("Invalid address or address not supported: %s\n", server_ip);
        close(sock);
        if (response) {
            response->status = 2; // Invalid address status
        }
        return -1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection Failed to %s:%d\n", server_ip, port);
        close(sock);
        if (response) {
            response->status = 3; // Connection failed status
        }
        return -1;
    }

    // Prepare request
    snprintf(request, sizeof(request), "MIPS_BENCH:%d", iterations);

    // Send request
    int send_result = send(sock, request, strlen(request), 0);
    if (send_result < 0) {
        printf("Failed to send request: %s\n", request);
        close(sock);
        if (response) {
            response->status = 4; // Send failed status
        }
        return -1;
    }
    printf("Request sent: %s\n", request);

    // Receive response
    int valread = read(sock, buffer, sizeof(buffer) - 1);
    if (valread > 0) {
        buffer[valread] = '\0';
        printf("Response received: %s\n", buffer);

        // Parse response - expecting format: "MIPS_RESULT:<ops_per_sec>:iterations:<count>"
        if (strncmp(buffer, "MIPS_RESULT:", 12) == 0) {
            char* token = strtok(buffer + 12, ":");
            if (token != NULL) {
                float ops_per_sec = atof(token);
                if (response) {
                    response->cmd = JANUS_CMD_MIPS_BENCH;
                    response->len = sizeof(struct mips_bench_response) - 4;
                    response->ops_per_second = (uint32_t)ops_per_sec;
                    response->iterations = iterations;
                    response->status = 0; // Success
                }
                printf("Parsed result: %.2f MIPS (operations per second)\n", ops_per_sec);
            }
        }

        close(sock);
        return 0;
    } else {
        printf("Failed to receive response from server\n");
        close(sock);
        if (response) {
            response->status = 5; // Receive failed status
        }
        return -1;
    }
}

// Function to process network commands from the Amiga via Janus IPC
void process_network_command_from_amiga(const uint8_t* payload, uint16_t payload_len) {
    // This function would be called from the main janusd daemon
    // when a network command is received from the Amiga side

    if (payload_len < sizeof(struct mips_bench_request)) {
        printf("Invalid payload length: %d (expected at least %zu)\n",
               payload_len, sizeof(struct mips_bench_request));
        return;
    }

    // Parse the request
    struct mips_bench_request req;
    memcpy(&req, payload, sizeof(req));

    printf("Processing MIPS benchmark request:\n");
    printf("  IP: %s\n", req.ip_addr);
    printf("  Port: %d\n", req.port);
    printf("  Iterations: %u\n", req.iterations);

    // Handle the MIPS benchmark request
    struct mips_bench_response response;
    int result = handle_mips_benchmark_request(req.ip_addr, req.port, req.iterations, &response);

    if (result == 0) {
        printf("MIPS benchmark request completed successfully\n");
        printf("Result: %u ops/sec for %u iterations\n",
               response.ops_per_second, response.iterations);

        // Send the result back to the Amiga via the Janus IPC ring buffer mechanism
        // uint16_t send_result = janus_send_message(response.cmd,
        //                                         (const uint8_t*)&response + 4,
        //                                         sizeof(response) - 4);
    } else {
        printf("MIPS benchmark request failed with status: %d\n", response.status);
    }
}