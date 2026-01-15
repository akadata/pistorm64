#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

// Define our UAPI directly since we can't include the header properly in this context
#define _LINUX_IOCTL_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define PISTORM_IOC_MAGIC 'p'

enum pistorm_width {
    PISTORM_W8  = 1,
    PISTORM_W16 = 2,
    PISTORM_W32 = 4,
};

struct pistorm_busop {
    __u32 addr;
    __u32 value;   /* for write: input; for read: output */
    __u8  width;   /* 1/2/4 */
    __u8  is_read; /* 1=read, 0=write */
    __u16 flags;   /* future */
};

#define PISTORM_IOC_SETUP          _IO(PISTORM_IOC_MAGIC, 0x00)
#define PISTORM_IOC_RESET_SM       _IO(PISTORM_IOC_MAGIC, 0x01)
#define PISTORM_IOC_PULSE_RESET    _IO(PISTORM_IOC_MAGIC, 0x02)
#define PISTORM_IOC_BUSOP          _IOWR(PISTORM_IOC_MAGIC, 0x10, struct pistorm_busop)

int main() {
    int fd;
    struct pistorm_busop busop;
    
    printf("PiStorm Smoke Test\n");
    
    // Open the device
    fd = open("/dev/pistorm0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/pistorm0");
        return 1;
    }
    
    printf("Successfully opened /dev/pistorm0\n");
    
    // Try setup
    if (ioctl(fd, PISTORM_IOC_SETUP) < 0) {
        perror("PISTORM_IOC_SETUP failed");
        close(fd);
        return 1;
    }
    printf("Setup completed\n");
    
    // Try pulse reset
    if (ioctl(fd, PISTORM_IOC_PULSE_RESET) < 0) {
        perror("PISTORM_IOC_PULSE_RESET failed");
        close(fd);
        return 1;
    }
    printf("Reset pulse completed\n");
    
    // Try a bus operation (read from INTREQR register)
    busop.addr = 0xDFF01E;  // INTREQR register
    busop.width = PISTORM_W16;
    busop.is_read = 1;
    busop.value = 0;
    
    if (ioctl(fd, PISTORM_IOC_BUSOP, &busop) < 0) {
        perror("PISTORM_IOC_BUSOP read failed");
        close(fd);
        return 1;
    }
    
    printf("Read from 0xDFF01E: 0x%04x\n", busop.value);
    
    // Try a write operation
    busop.addr = 0xDFF01E;  // INTREQR register
    busop.width = PISTORM_W16;
    busop.is_read = 0;
    busop.value = 0x0000;  // Clear all interrupts
    
    if (ioctl(fd, PISTORM_IOC_BUSOP, &busop) < 0) {
        perror("PISTORM_IOC_BUSOP write failed");
        close(fd);
        return 1;
    }
    
    printf("Write to 0xDFF01E completed\n");
    
    close(fd);
    printf("Smoke test completed successfully!\n");
    return 0;
}