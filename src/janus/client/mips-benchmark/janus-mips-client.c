/*
 * janus-mips-client.c
 * 
 * MIPS Benchmark Client for Janus IPC System
 * Runs on Homer (ArchLinux) to handle MIPS benchmark requests from Amiga
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_PORT 8888
#define BUFFER_SIZE 1024

// Global variables
static volatile sig_atomic_t running = 1;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down gracefully...\n", sig);
    running = 0;
}

// Simulate MIPS benchmark - performs a computational task
double run_mips_benchmark(int iterations) {
    struct timeval start, end;
    double elapsed_time;
    volatile double result = 0.0; // volatile to prevent optimization
    
    gettimeofday(&start, NULL);
    
    // Perform a simple computational task for MIPS measurement
    for (int i = 0; i < iterations; i++) {
        // Simple mathematical operations to simulate MIPS workload
        result += (double)i * 1.5;
        result -= (double)i * 0.3;
        result *= 1.001;
        result /= 1.001;
        
        // Additional operations to consume CPU cycles
        if (i % 1000 == 0) {
            result = result * result + 1.0;
        }
    }
    
    gettimeofday(&end, NULL);
    
    elapsed_time = (end.tv_sec - start.tv_sec) + 
                   (end.tv_usec - start.tv_usec) / 1000000.0;
    
    if (elapsed_time > 0) {
        return (double)iterations / elapsed_time; // Operations per second
    }
    
    return 0.0;
}

int main(int argc, char *argv[]) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    
    // Parse command line arguments
    int port = DEFAULT_PORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--port PORT]\n", argv[0]);
            printf("Default port: %d\n", DEFAULT_PORT);
            return 0;
        }
    }
    
    printf("Janus MIPS Benchmark Client starting on port %d\n", port);
    
    // Register signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Configure address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    address.sin_port = htons(port);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Start listening
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Janus MIPS Client listening on port %d\n", port);
    printf("Waiting for connections...\n");
    
    while (running) {
        // Accept incoming connection
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, 
                                (socklen_t*)&addrlen)) < 0) {
            if (running) { // Only report error if not shutting down
                perror("accept failed");
            }
            break;
        }
        
        printf("Connection accepted from %s:%d\n", 
               inet_ntoa(address.sin_addr), ntohs(address.sin_port));
        
        // Receive benchmark request
        int valread = read(new_socket, buffer, BUFFER_SIZE - 1);
        if (valread > 0) {
            buffer[valread] = '\0';
            
            // Parse the request - expects format: "MIPS_BENCH:<iterations>"
            if (strncmp(buffer, "MIPS_BENCH:", 11) == 0) {
                int iterations = atoi(buffer + 11);
                
                if (iterations <= 0) {
                    iterations = 1000000; // Default to 1 million iterations
                }
                
                printf("Running MIPS benchmark with %d iterations\n", iterations);
                
                // Run the benchmark
                double mips_score = run_mips_benchmark(iterations);
                
                // Format response
                char response[256];
                snprintf(response, sizeof(response), 
                        "MIPS_RESULT:%.2f:iterations:%d", mips_score, iterations);
                
                // Send result back
                send(new_socket, response, strlen(response), 0);
                printf("Sent result: %s\n", response);
            } else {
                // Unknown command
                const char* error_response = "ERROR:UNKNOWN_COMMAND";
                send(new_socket, error_response, strlen(error_response), 0);
            }
        }
        
        // Close the connection
        close(new_socket);
        printf("Connection closed\n");
    }
    
    // Cleanup
    close(server_fd);
    printf("Janus MIPS Client shutting down\n");
    
    return 0;
}