// clkpeek.c - minimal PiStorm bus peek tool using the standard gpio/ps_protocol implementation
//
// Goal:
//   Replace any older / ad-hoc clkpeek with the same protocol path used by fb2ami/buptest:
//     gpio/ps_protocol.c + gpio/rpi_peri.c
//
// Build (from repo root):
//   gcc -O2 -Wall -Wextra -I. clkpeek.c gpio/ps_protocol.c gpio/rpi_peri.c -o clkpeek
//
// Examples:
//   sudo ./clkpeek                     # default: peek 0xDFF000 (custom chips) 64 words
//   sudo ./clkpeek 0x00DFF000 128      # 128 16-bit reads
//   sudo ./clkpeek -32 0x00F80000 32   # 32-bit reads from kickstart mapping window
//   sudo ./clkpeek -p 1000 0x00DFF000  # poll every 1000ms
//   sudo ./clkpeek -w 0x00DFF180 0x7FFF # write16 then readback
//
// Notes:
//   - This tool assumes your ps_protocol layer exposes:
//       int  ps_setup_protocol(void);
//       void ps_reset_state_machine(void);
//       void ps_pulse_reset(void);
//       uint16_t ps_read_16(uint32_t addr);
//       uint32_t ps_read_32(uint32_t addr);
//       void ps_write_8(uint32_t addr, uint8_t v);
//       void ps_write_16(uint32_t addr, uint16_t v);
//       void ps_write_32(uint32_t addr, uint32_t v);
//       void ps_cleanup_protocol(void);   (if present)
//     If your header uses slightly different names, adjust the wrappers below.
//
//   - Run as root because /dev/mem (or clock/gpio) access is typically required.

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "src/gpio/ps_protocol.h"

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

static uint32_t parse_u32(const char *s) {
    if (!s || !*s) return 0;
    char *end = NULL;
    errno = 0;
    uint64_t v = strtoull(s, &end, 0);
    if (errno || end == s || *end != '\0' || v > 0xFFFFFFFFULL) {
        fprintf(stderr, "Invalid number: %s\n", s);
        exit(2);
    }
    return (uint32_t)v;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage:\n"
        "  %s [options] [addr] [count]\n\n"
        "Options:\n"
        "  -16              Read 16-bit words (default)\n"
        "  -32              Read 32-bit longs\n"
        "  -p <ms>          Poll loop; repeat dump every <ms> milliseconds\n"
        "  -r               Pulse reset before first read\n"
        "  -w <addr> <val>  Write16 <val> to <addr> before peeking\n"
        "  -q               Quiet: only print values (no header)\n"
        "  -h               Help\n\n"
        "Args:\n"
        "  addr             Base address to peek (default 0x00DFF000)\n"
        "  count            Number of units to read (default 64)\n\n",
        argv0);
}

static void hexdump16(uint32_t base, uint32_t count, bool quiet) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t addr = base + (i * 2);
        uint16_t v = ps_read_16(addr);
        if (quiet) {
            printf("%04x\n", v);
            continue;
        }
        if ((i % 8) == 0) printf("%08" PRIx32 ": ", addr);
        printf("%04x ", v);
        if ((i % 8) == 7) printf("\n");
    }
    if (!quiet && (count % 8) != 0) printf("\n");
}

static void hexdump32(uint32_t base, uint32_t count, bool quiet) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t addr = base + (i * 4);
        uint32_t v = ps_read_32(addr);
        if (quiet) {
            printf("%08" PRIx32 "\n", v);
            continue;
        }
        if ((i % 4) == 0) printf("%08" PRIx32 ": ", addr);
        printf("%08" PRIx32 " ", v);
        if ((i % 4) == 3) printf("\n");
    }
    if (!quiet && (count % 4) != 0) printf("\n");
}

static void sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000UL);
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    bool use32 = false;
    bool do_reset = false;
    bool quiet = false;
    uint32_t poll_ms = 0;

    bool do_write16 = false;
    uint32_t w_addr = 0;
    uint16_t w_val = 0;

    // Defaults: custom chips base, 64 words
    uint32_t base = 0x00DFF000;
    uint32_t count = 64;

    // Basic arg parsing (kept simple on purpose)
    int i = 1;
    while (i < argc) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(a, "-16")) {
            use32 = false;
            i++;
        } else if (!strcmp(a, "-32")) {
            use32 = true;
            i++;
        } else if (!strcmp(a, "-r")) {
            do_reset = true;
            i++;
        } else if (!strcmp(a, "-q")) {
            quiet = true;
            i++;
        } else if (!strcmp(a, "-p")) {
            if (i + 1 >= argc) { usage(argv[0]); return 2; }
            poll_ms = parse_u32(argv[i + 1]);
            i += 2;
        } else if (!strcmp(a, "-w")) {
            if (i + 2 >= argc) { usage(argv[0]); return 2; }
            do_write16 = true;
            w_addr = parse_u32(argv[i + 1]);
            w_val = (uint16_t)parse_u32(argv[i + 2]);
            i += 3;
        } else if (a[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", a);
            usage(argv[0]);
            return 2;
        } else {
            // positional: addr [count]
            base = parse_u32(argv[i]);
            if (i + 1 < argc) count = parse_u32(argv[i + 1]);
            break;
        }
    }

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    ps_setup_protocol();

    // Always start from a clean state machine.
    ps_reset_state_machine();

    if (do_reset) {
        ps_pulse_reset();
        // Give the bus a moment to settle.
        usleep(5000);
    }

    if (do_write16) {
        if (!quiet) {
            fprintf(stderr, "[clkpeek] write16 %08" PRIx32 " <= %04x\n", w_addr, w_val);
        }
        ps_write_16(w_addr, w_val);
        usleep(1000);
    }

    do {
        if (!quiet) {
            fprintf(stderr,
                "[clkpeek] %s base=%08" PRIx32 " count=%" PRIu32 "%s\n",
                use32 ? "32-bit" : "16-bit", base, count,
                poll_ms ? " (polling)" : "");
        }

        if (use32) hexdump32(base, count, quiet);
        else       hexdump16(base, count, quiet);

        if (poll_ms && !g_stop) sleep_ms(poll_ms);

    } while (poll_ms && !g_stop);

    // If your ps_protocol layer has cleanup, call it.
    // Some versions omit this; leaving it optional keeps the tool compatible.
#ifdef PS_HAS_CLEANUP_PROTOCOL
    ps_cleanup_protocol();
#endif

    return 0;
}

