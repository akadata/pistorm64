/*
 * Standalone GPCLK0 200MHz test for PiStorm on Raspberry Pi5
 * Direct register access to RP1 clock manager using device tree approach
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

// BCM2708 register addresses (from ps_protocol.h)
#define BCM2708_PERI_BASE 0x3F000000  // pi3 (legacy, but addresses are different on Pi5)
#define BCM2708_PERI_SIZE 0x01000000
#define GPIO_ADDR 0x200000 /* GPIO controller */
#define GPCLK_ADDR 0x101000
#define CLK_PASSWD 0x5a000000
#define CLK_GP0_CTL 0x070
#define CLK_GP0_DIV 0x074

// IO Bank 0 base for RP1 (for GPIO control)
#define RP1_IO_BANK0_BASE_PHYS 0x1f000d0000ULL
#define RP1_IO_BANK0_SIZE      0xc000

// GPIO control register offsets in IO Bank 0
#define GPIO_CTRL_OFFSET(gpio) (0x004 + (gpio) * 8)
#define GPIO_STATUS_OFFSET(gpio) (0x000 + (gpio) * 8)

// Function to read device tree string
static int read_dt_string(const char *path, char *buf, size_t buf_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    ssize_t r = read(fd, buf, buf_len - 1);
    close(fd);
    if (r <= 0) {
        return -1;
    }
    buf[r] = '\0';
    return 0;
}

// Function to read file bytes
static int read_file_bytes(const char *path, uint8_t *buf, size_t len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    ssize_t r = read(fd, buf, len);
    close(fd);
    return (r == (ssize_t)len) ? 0 : -1;
}

// Function to convert big-endian bytes to uint32_t
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | ((uint32_t)p[3] << 0);
}

int main() {
    printf("PiStorm GPCLK0 200MHz Direct Register Test\n");
    printf("=========================================\n\n");

    // First, get the clock manager base address from device tree
    char dvp_path[256];
    uint64_t cm_phys_base = 0;

    printf("Reading clock manager address from device tree...\n");
    if (read_dt_string("/sys/firmware/devicetree/base/__symbols__/dvp", dvp_path, sizeof(dvp_path)) == 0) {
        printf("  Found dvp symbol: %s\n", dvp_path);

        char reg_path[512];
        snprintf(reg_path, sizeof(reg_path), "/sys/firmware/devicetree/base%s/reg", dvp_path);

        uint8_t reg[8];
        if (read_file_bytes(reg_path, reg, sizeof(reg)) == 0) {
            uint32_t cm_child = be32(&reg[0]);
            printf("  Clock manager child address: 0x%08x\n", cm_child);

            // Derive SoC peripheral physical base from DT bus ranges:
            // `/sys/firmware/devicetree/base/soc@107c000000/ranges` maps child 0x00000000 -> parent 0x1000000000.
            uint8_t ranges[16];
            if (read_file_bytes("/sys/firmware/devicetree/base/soc@107c000000/ranges", ranges, sizeof(ranges)) == 0) {
                uint64_t soc_phys_base = ((uint64_t)be32(&ranges[4]) << 32) | be32(&ranges[0]);  // parent address (2 cells)
                printf("  SoC physical base: 0x%016lx\n", soc_phys_base);

                cm_phys_base = soc_phys_base + (uint64_t)cm_child;
                printf("  Calculated clock manager physical base: 0x%016lx\n", cm_phys_base);
            } else {
                printf("  Could not read soc ranges, using fallback\n");
                cm_phys_base = 0x107c700000ULL + (uint64_t)cm_child;  // Fallback
            }
        } else {
            printf("  Could not read reg property\n");
        }
    } else {
        printf("  Could not find dvp symbol in device tree\n");
        printf("  Using fallback address\n");
        cm_phys_base = 0x107c700000ULL + GPCLK_ADDR;  // Fallback
    }

    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("open /dev/mem");
        printf("Error: This program must be run as root (use sudo)\n");
        return 1;
    }

    // Map RP1 IO Bank 0 (for GPIO control)
    void *io_bank0_map = mmap(
        NULL,
        RP1_IO_BANK0_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd,
        RP1_IO_BANK0_BASE_PHYS
    );

    if (io_bank0_map == MAP_FAILED) {
        perror("mmap IO_BANK0");
        close(mem_fd);
        return 1;
    }

    volatile uint32_t *io_bank0 = (volatile uint32_t *)io_bank0_map;

    // Map the clock manager using the calculated address
    const uint64_t map_base = cm_phys_base & ~0xfffull;
    const size_t map_len = 0x1000;
    const size_t page_off = (size_t)(cm_phys_base - map_base);

    printf("Attempting to map clock manager at 0x%016lx (page offset 0x%zx)...\n", map_base, page_off);

    void *cm_map = mmap(
        NULL,
        map_len,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd,
        (off_t)map_base
    );

    if (cm_map == MAP_FAILED) {
        perror("mmap clock manager");
        printf("This may be due to kernel security restrictions (CONFIG_STRICT_DEVMEM)\n");
        munmap(io_bank0_map, RP1_IO_BANK0_SIZE);
        close(mem_fd);
        return 1;
    }

    volatile uint32_t *cm = (volatile uint32_t *)((volatile uint8_t *)cm_map + page_off);

    printf("Successfully mapped RP1 registers:\n");
    printf("  IO_BANK0 at 0x%08lx\n", (unsigned long)RP1_IO_BANK0_BASE_PHYS);
    printf("  Clock Manager at 0x%016lx (mapped at 0x%016lx with offset 0x%zx)\n",
           (unsigned long long)cm_phys_base, (unsigned long long)map_base, page_off);

    // Configure GPIO4 to ALT0 (GPCLK0) using IO_BANK0 registers
    printf("\nConfiguring GPIO4 to GPCLK0 (ALT0)...\n");
    uint32_t gpio4_ctrl_reg = GPIO_CTRL_OFFSET(4);
    uint32_t current_val = io_bank0[gpio4_ctrl_reg / 4];
    printf("  Current GPIO4 control: 0x%08x\n", current_val);

    // Set function select to ALT0 (bits 0-4)
    uint32_t new_val = (current_val & ~0x1F) | 0x0;  // ALT0 = 0
    io_bank0[gpio4_ctrl_reg / 4] = new_val;
    printf("  New GPIO4 control: 0x%08x\n", io_bank0[gpio4_ctrl_reg / 4]);

    // Read back to verify
    uint32_t verify_val = io_bank0[gpio4_ctrl_reg / 4];
    printf("  Verified GPIO4 control: 0x%08x\n", verify_val);

    // Try to set GPCLK0 to 200MHz directly
    // Source: PLL_SYS (5 = 2000MHz), Divider: 10 (2000MHz/10 = 200MHz)
    printf("\nAttempting to configure GPCLK0 for 200MHz...\n");

    volatile uint32_t *gp0_ctl = &cm[CLK_GP0_CTL / 4];
    volatile uint32_t *gp0_div = &cm[CLK_GP0_DIV / 4];

    printf("  Current GP0DIV: 0x%08x\n", *gp0_div);
    printf("  Current GP0CTL: 0x%08x\n", *gp0_ctl);

    // Stop the clock first
    printf("  Stopping GPCLK0...\n");
    *gp0_ctl = CLK_PASSWD | (1 << 5);  // KILL bit
    usleep(100);

    // Wait for BUSY to clear
    int timeout = 1000;
    while ((*gp0_ctl & (1 << 7)) && timeout-- > 0) {
        usleep(10);
    }
    if (timeout <= 0) {
        printf("  Warning: BUSY did not clear after KILL\n");
    } else {
        printf("  BUSY cleared after KILL\n");
    }

    // Set divider for 200MHz (2000MHz / 10 = 200MHz)
    // Divider is in format: bits 23:12 = integer part, bits 11:0 = fractional part
    uint32_t div_value = 10;  // For 200MHz from 2000MHz PLL_SYS
    *gp0_div = CLK_PASSWD | (div_value << 12);
    printf("  Set GP0DIV to: 0x%08x\n", *gp0_div);

    // Enable with source PLL_SYS (5) and MASH=1 (for stability)
    *gp0_ctl = CLK_PASSWD | 5 | (1u << 4);  // Source 5 = PLL_SYS, MASH=1
    usleep(100);

    // Wait for BUSY to set
    timeout = 1000;
    while (!(*gp0_ctl & (1 << 7)) && timeout-- > 0) {
        usleep(10);
    }
    if (timeout <= 0) {
        printf("  Warning: BUSY did not set after enable\n");
    } else {
        printf("  BUSY set after enable\n");
    }

    printf("  Final GP0DIV: 0x%08x\n", *gp0_div);
    printf("  Final GP0CTL: 0x%08x\n", *gp0_ctl);

    // Read GPIO status to see if clock is running
    uint32_t gpio4_status_reg = GPIO_STATUS_OFFSET(4);
    printf("\nGPIO4 Status: 0x%08x\n", io_bank0[gpio4_status_reg / 4]);

    // Try reading some GPIO state to see if we can communicate with CPLD
    // This would be the SYNC_IN register in the RIO block
    printf("\nAttempting to read GPIO states (may require RIO mapping)...\n");

    // Clean up
    munmap(cm_map, map_len);
    munmap(io_bank0_map, RP1_IO_BANK0_SIZE);
    close(mem_fd);

    printf("\nTest completed. Check if clock is running with:\n");
    printf("  sudo pinctrl get 4\n");
    printf("  sudo cat /sys/kernel/debug/clk/clk_summary | grep clk_gp0\n");

    return 0;
}