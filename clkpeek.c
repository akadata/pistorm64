// clkpeek.c
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
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

// Detect peripheral base from /proc/device-tree/soc/ranges.
// On Raspberry Pi this property typically contains 3 big-endian u32 cells:
// bus_addr (0x7e000000), cpu_addr (PERI_BASE), size (0x01000000)
static uint32_t detect_peri_base(void) {
    uint8_t *buf = NULL;
    size_t len = 0;

    if (read_file("/proc/device-tree/soc/ranges", &buf, &len) == 0 && len >= 12) {
        uint32_t bus  = be32(buf + 0);
        uint32_t cpu  = be32(buf + 4);
        uint32_t size = be32(buf + 8);
        free(buf);

        // Sanity: bus is often 0x7e000000, size often 0x01000000
        if (cpu == 0x20000000u || cpu == 0x3F000000u || cpu == 0xFE000000u) {
            (void)bus; (void)size;
            return cpu;
        }
        // If it’s something else but non-zero, still return it.
        if (cpu != 0) return cpu;
    } else {
        free(buf);
    }

    // Fallback: try model-compatible defaults
    // Many Zero/2/3 use 0x3F..., Pi4/400 use 0xFE...
    return 0x3F000000u;
}

static const char *src_name(uint32_t src) {
    switch (src) {
        case 0: return "GND";
        case 1: return "OSC (19.2MHz)";
        case 2: return "TESTDEBUG0";
        case 3: return "TESTDEBUG1";
        case 4: return "PLLA";
        case 5: return "PLLC";
        case 6: return "PLLD (500MHz)";
        case 7: return "HDMI";
        default: return "UNKNOWN";
    }
}

static double src_hz_guess(uint32_t src) {
    // For this debugging task, OSC and PLLD are the useful ones.
    switch (src) {
        case 1: return 19.2e6;
        case 6: return 500e6;
        default: return 0.0;
    }
}

static void ts_now(char *out, size_t outlen) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    snprintf(out, outlen, "%02d:%02d:%02d.%03ld",
             tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
}

int main(int argc, char **argv) {
    int watch = 0;
    int always = 0;
    int interval_ms = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w")) watch = 1;
        else if (!strcmp(argv[i], "-a")) always = 1;
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) interval_ms = atoi(argv[++i]);
        else {
            fprintf(stderr, "Usage: %s [-w] [-a] [-i ms]\n", argv[0]);
            fprintf(stderr, "  -w      watch mode\n");
            fprintf(stderr, "  -a      print every interval (otherwise only on change)\n");
            fprintf(stderr, "  -i ms   interval in ms (default 100)\n");
            return 2;
        }
    }

    uint32_t peri = detect_peri_base();
    uint32_t gpio_base = peri + 0x00200000u;
    uint32_t cm_base   = peri + 0x00101000u;

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    // Map one page for GPIO and one for CM
    size_t map_len = 0x1000;

    off_t gpio_off = (off_t)(gpio_base & ~0xFFFu);
    volatile uint32_t *gpio = mmap(NULL, map_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpio_off);
    if (gpio == MAP_FAILED) { perror("mmap gpio"); close(fd); return 1; }

    off_t cm_off = (off_t)(cm_base & ~0xFFFu);
    volatile uint32_t *cm = mmap(NULL, map_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, cm_off);
    if (cm == MAP_FAILED) { perror("mmap cm"); munmap((void*)gpio, map_len); close(fd); return 1; }

    // Offsets within their pages:
    uint32_t cm_page_off   = cm_base & 0xFFFu;
    uint32_t gpio_page_off = gpio_base & 0xFFFu;

    // Clock manager offsets for GPCLK0
    const uint32_t GP0CTL = 0x70u;
    const uint32_t GP0DIV = 0x74u;

    // GPIO function select registers: GPFSEL0 @ 0x00
    const uint32_t GPFSEL0 = 0x00u;

    uint32_t last_ctl = 0xFFFFFFFFu, last_div = 0xFFFFFFFFu, last_fsel0 = 0xFFFFFFFFu;

    do {
        uint32_t fsel0 = gpio[(gpio_page_off + GPFSEL0) / 4];
        uint32_t ctl   = cm[(cm_page_off + GP0CTL) / 4];
        uint32_t div   = cm[(cm_page_off + GP0DIV) / 4];

        int changed = (ctl != last_ctl) || (div != last_div) || (fsel0 != last_fsel0);

        if (!watch || always || changed) {
            char tbuf[32];
            ts_now(tbuf, sizeof tbuf);

            // Decode GPIO4 FSEL (bits 12..14 of GPFSEL0)
            uint32_t gpio4_f = (fsel0 >> 12) & 0x7;
            const char *gpio4_mode =
                (gpio4_f == 0) ? "INPUT" :
                (gpio4_f == 1) ? "OUTPUT" :
                (gpio4_f == 4) ? "ALT0 (GPCLK0)" :
                (gpio4_f == 5) ? "ALT1" :
                (gpio4_f == 6) ? "ALT2" :
                (gpio4_f == 7) ? "ALT3" :
                (gpio4_f == 3) ? "ALT4" :
                                 "ALT5";

	    // uint32_t src  = (ctl >> 4) & 0xF;
	    uint32_t src  = (ctl >> 4) & 0x7;   // 3-bit SRC

            uint32_t divi = (div >> 12) & 0xFFF;
            uint32_t divf = div & 0xFFF;

            double d = (double)divi + ((double)divf / 4096.0);
            double hz = 0.0;
            double shz = src_hz_guess(src);
            if (shz > 0.0 && d > 0.0) hz = shz / d;

            printf("[%s] PERI=0x%08x GPIO4=%s | GP0CTL=0x%08x (SRC=%u %s EN=%u) GP0DIV=0x%08x (DIVI=%u DIVF=%u d=%.6f) => %.3f MHz\n",
                   tbuf,
                   peri,
                   gpio4_mode,
                   ctl,
                   src, src_name(src),
                   (ctl >> 7) & 1,
                   div,
                   divi, divf, d,
                   hz / 1e6);

            last_ctl = ctl;
            last_div = div;
            last_fsel0 = fsel0;
        }

        if (!watch) break;
        usleep((useconds_t)interval_ms * 1000);
    } while (1);

    munmap((void*)cm, map_len);
    munmap((void*)gpio, map_len);
    close(fd);
    return 0;
}

