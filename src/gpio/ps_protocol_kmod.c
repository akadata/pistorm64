// src/gpio/ps_protocol_kmod.c
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Include our UAPI header
#include "ps_protocol.h"
#include <linux/pistorm.h>
#include "src/musashi/m68k.h"

// Compile-time toggle for batching - default to disabled to ensure stability
#ifndef PISTORM_ENABLE_BATCH
#define PISTORM_ENABLE_BATCH 0
#endif

#if PISTORM_ENABLE_BATCH
// pick a sane chunk size; tune later
#ifndef PISTORM_BATCH_MAX
#define PISTORM_BATCH_MAX 256
#endif

struct pistorm_busop_batch {
    uint32_t count;
    uint64_t ptr;   // userspace pointer to ops[]
};

static inline int ps_busop_batch(int ps_fd, struct pistorm_busop *ops, uint32_t count)
{
    struct pistorm_batch b = {
        .ops_count = count,
        .ops_ptr   = (uint64_t)(uintptr_t)ops,
        .reserved  = 0,
    };
    return ioctl(ps_fd, PISTORM_IOC_BATCH, &b);
}

// Optional: small queue to accumulate ops and flush in one ioctl
static struct pistorm_busop g_opsq[PISTORM_BATCH_MAX];
static uint32_t g_opsq_n = 0;

static inline int ps_busopq_flush(int ps_fd)
{
    if (!g_opsq_n) return 0;
    int rc = ps_busop_batch(ps_fd, g_opsq, g_opsq_n);
    g_opsq_n = 0;
    return rc;
}

static inline int ps_busopq_push(int ps_fd, const struct pistorm_busop *op)
{
    g_opsq[g_opsq_n++] = *op;
    if (g_opsq_n >= PISTORM_BATCH_MAX) return ps_busopq_flush(ps_fd);
    return 0;
}
#endif // PISTORM_ENABLE_BATCH

static int ps_fd = -1;
static int backend_logged;
static volatile unsigned int gpio_shadow[32];
volatile unsigned int *gpio = gpio_shadow; /* legacy pointer */

static int ps_open_dev(void) {
    if (ps_fd >= 0) return 0;
    ps_fd = open("/dev/pistorm", O_RDWR | O_CLOEXEC);
    if (ps_fd < 0) {
        if (!backend_logged) {
            fprintf(stderr, "[ps_protocol] kmod backend selected but /dev/pistorm missing (%s)\n",
                    strerror(errno));
            backend_logged = 1;
        }
        return -1;
    }
    if (!backend_logged) {
        printf("[ps_protocol] backend=kmod (/dev/pistorm)\n");
        backend_logged = 1;
    }
    return 0;
}

void ps_setup_protocol(void) {
    if (ps_open_dev() < 0) return;
    if (ioctl(ps_fd, PISTORM_IOC_SETUP) < 0)
        perror("PISTORM_IOC_SETUP");
}

void ps_reset_state_machine(void) {
    if (ps_open_dev() < 0) return;
    if (ioctl(ps_fd, PISTORM_IOC_RESET_SM) < 0)
        perror("PISTORM_IOC_RESET_SM");
}

void ps_pulse_reset(void) {
    if (ps_open_dev() < 0) return;
    if (ioctl(ps_fd, PISTORM_IOC_PULSE_RESET) < 0)
        perror("PISTORM_IOC_PULSE_RESET");
}

void ps_protocol_dump_stats(void) {
    // Kernel backend doesn't collect user-space queue stats.
    fprintf(stderr, "[PS_PROTO] kmod backend: no stats available\n");
}


static int ps_busop(int is_read, int width, unsigned addr, unsigned *val, unsigned short flags) {
    if (ps_open_dev() < 0) return -1;

#if PISTORM_ENABLE_BATCH
    // For read operations, flush any pending writes first to maintain ordering
    if (is_read && g_opsq_n > 0) {
        ps_busopq_flush(ps_fd);
    }

    // For write operations, use batching to reduce ioctl calls
    if (!is_read) {
        struct pistorm_busop op = {
            .addr   = addr,
            .value  = val ? *val : 0,
            .width  = (unsigned char)width,
            .is_read= (unsigned char)is_read,
            .flags  = flags,
        };
        return ps_busopq_push(ps_fd, &op);
    }
#endif

    // For read operations, we need immediate results, so use direct ioctl
    // Also use direct ioctl when batching is disabled
    struct pistorm_busop op = {
        .addr   = addr,
        .value  = val ? *val : 0,
        .width  = (unsigned char)width,
        .is_read= (unsigned char)is_read,
        .flags  = flags,
    };
    int rc = ioctl(ps_fd, PISTORM_IOC_BUSOP, &op);
    if (rc == 0 && is_read && val) *val = op.value;
    return rc;
}

uint8_t ps_read_8(uint32_t addr)  {
    uint32_t v = 0;
    ps_busop(1, PISTORM_W8, addr, &v, 0);
    return (uint8_t)(v & 0xff);
}

uint16_t ps_read_16(uint32_t addr) {
    uint32_t v = 0;
    ps_busop(1, PISTORM_W16, addr, &v, 0);
    return (uint16_t)(v & 0xffff);
}

uint32_t ps_read_32(uint32_t addr) {
    uint32_t v = 0;
    ps_busop(1, PISTORM_W32, addr, &v, 0);
    return v;
}

void ps_write_8(uint32_t addr, uint8_t v)  {
    uint32_t temp_v = v;
    ps_busop(0, PISTORM_W8, addr, &temp_v, 0);
}

void ps_write_16(uint32_t addr, uint16_t v) {
    uint32_t temp_v = v;
    ps_busop(0, PISTORM_W16, addr, &temp_v, 0);
}

void ps_write_32(uint32_t addr, uint32_t v) {
    uint32_t temp_v = v;
    ps_busop(0, PISTORM_W32, addr, &temp_v, 0);
}

// Additional functions that might be needed
uint16_t ps_read_status_reg(void) {
    struct pistorm_busop op = {
        .addr = 0,
        .value = 0,
        .width = PISTORM_W16,
        .is_read = 1,
        .flags = PISTORM_BUSOP_F_STATUS,
    };

    if (ps_busop(op.is_read, op.width, op.addr, &op.value, op.flags) == 0)
        return (uint16_t)(op.value & 0xffffu);
    return 0;
}

void ps_write_status_reg(uint16_t value) {
    struct pistorm_busop op = {
        .addr = 0,
        .value = (unsigned int)value,
        .width = PISTORM_W16,
        .is_read = 0,
        .flags = PISTORM_BUSOP_F_STATUS,
    };
    (void)ps_busop(op.is_read, op.width, op.addr, &op.value, op.flags);
}

unsigned ps_get_ipl_zero(void) {
    unsigned int level = ps_gpio_lev();
    return level & (1u << PIN_IPL_ZERO);
}

unsigned int ps_gpio_lev(void) {
    struct pistorm_pins pins;

    if (ps_open_dev() < 0)
        return gpio_shadow[13];
    if (ioctl(ps_fd, PISTORM_IOC_GET_PINS, &pins) == 0) {
        gpio_shadow[13] = pins.gplev0;
        gpio_shadow[14] = pins.gplev1;
    }
    return gpio_shadow[13];
}

// Public API to flush the batch queue
int ps_flush_batch_queue(void) {
    if (ps_fd < 0) return -1;
#if PISTORM_ENABLE_BATCH
    return ps_busopq_flush(ps_fd);
#else
    return 0;  // No-op when batching is disabled
#endif
}

static void __attribute__((unused)) ps_update_irq(void) {
    unsigned int ipl = 0;

    if (!ps_get_ipl_zero()) {
        unsigned int status = ps_read_status_reg();
        ipl = (status & STATUS_MASK_IPL) >> STATUS_SHIFT_IPL;
    }

    m68k_set_irq(ipl);
}
