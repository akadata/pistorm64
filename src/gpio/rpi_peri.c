#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "gpio/rpi_peri.h"

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static int read_file(const char *path, uint8_t **buf_out, size_t *len_out) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz < 0) { close(fd); return -1; }
    if (lseek(fd, 0, SEEK_SET) < 0) { close(fd); return -1; }

    uint8_t *buf = calloc(1, (size_t)sz);
    if (!buf) { close(fd); return -1; }

    ssize_t rd = read(fd, buf, (size_t)sz);
    close(fd);

    if (rd != sz) { free(buf); return -1; }
    *buf_out = buf;
    *len_out = (size_t)sz;
    return 0;
}

// Returns PERI_BASE for BCM283x-style SoCs (0x20000000, 0x3F000000, 0xFE000000)
// Returns 0 on failure (caller can choose fallback or bail).
uint32_t rpi_detect_peri_base(void) {
    uint8_t *buf = NULL;
    size_t len = 0;

    // Most Raspberry Pi kernels expose this
    if (read_file("/proc/device-tree/soc/ranges", &buf, &len) == 0 && len >= 12) {
        uint32_t cpu  = be32(buf + 4);
        free(buf);

        // Known BCM283x bases
        if (cpu == 0x20000000u || cpu == 0x3F000000u || cpu == 0xFE000000u) {
            return cpu;
        }
        // If non-zero and sane-looking, still allow it
        if (cpu != 0) return cpu;
    } else {
        free(buf);
    }

    return 0;
}

int rpi_open_devmem(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    return fd;
}

// Map a PERI sub-block; returns pointer or MAP_FAILED
volatile uint32_t *rpi_map_block(int mem_fd, uint32_t phys_addr, size_t len, uint32_t *page_off_out) {
    uint32_t page_off = phys_addr & 0xFFFu;
    off_t page_base = (off_t)(phys_addr & ~0xFFFu);

    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, page_base);
    if (p == MAP_FAILED) return (volatile uint32_t *)MAP_FAILED;

    if (page_off_out) *page_off_out = page_off;
    return (volatile uint32_t *)p;
}
