// Simple test to verify kernel module functionality
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>

// Include the UAPI header
#include "include/uapi/linux/pistorm.h"

int main() {
    int fd;
    struct pistorm_busop op;
    
    printf("Testing PiStorm kernel module...\n");
    
    // Open the device
    fd = open("/dev/pistorm0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/pistorm0");
        return 1;
    }
    
    printf("Successfully opened /dev/pistorm0\n");
    
    // Try setup operation
    if (ioctl(fd, PISTORM_IOC_SETUP) < 0) {
        perror("PISTORM_IOC_SETUP failed");
        close(fd);
        return 1;
    }
    printf("Setup operation completed\n");
    
    // Try pulse reset
    if (ioctl(fd, PISTORM_IOC_PULSE_RESET) < 0) {
        perror("PISTORM_IOC_PULSE_RESET failed");
        close(fd);
        return 1;
    }
    printf("Pulse reset completed\n");
    
    // Try a simple bus operation
    op.addr = 0xDFF01E;  // INTREQR register
    op.value = 0;
    op.width = PISTORM_W16;
    op.is_read = 1;
    op.flags = 0;
    
    if (ioctl(fd, PISTORM_IOC_BUSOP, &op) < 0) {
        perror("PISTORM_IOC_BUSOP failed");
        close(fd);
        return 1;
    }
    
    printf("Bus operation completed, read value: 0x%04X\n", op.value);
    
    close(fd);
    printf("Test completed successfully!\n");
    return 0;
}