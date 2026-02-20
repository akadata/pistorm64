// src/gpio/ps_protocol_kmod.c

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

// Include our UAPI header
#include "ps_protocol.h"
#include <linux/pistorm.h>
#include "src/musashi/m68k.h"
#include "log.h"

#define STATUS_MASK_IPL  0xe000u
#define STATUS_SHIFT_IPL 13

// Compile-time toggle for batching - default to disabled to ensure stability
#ifndef PISTORM_ENABLE_BATCH
#define PISTORM_ENABLE_BATCH 1
#endif

#ifndef PISTORM_ENABLE_QUEUE
#define PISTORM_ENABLE_QUEUE 1
#endif

#if PISTORM_ENABLE_BATCH
// pick a sane chunk size; tune later
#ifndef PISTORM_BATCH_MAX
#define PISTORM_BATCH_MAX 256
#endif

/* Make sure nanosleep is declared even in strict C modes */
int nanosleep(const struct timespec *req, struct timespec *rem);


static unsigned int ps_batch_max_ops = PISTORM_BATCH_MAX;
static uint8_t ps_batch_last_fc = 0xff;

static void ps_batch_init(void) {
    const char *ops_env = getenv("PISTORM_BATCH_OPS");
    if(ops_env && *ops_env) {
        char *end = NULL;
        unsigned long ops = strtoul(ops_env, &end, 10);
        if(end && *end == '\0' && ops > 0) {
            if(ops > PISTORM_BATCH_MAX) ops = PISTORM_BATCH_MAX;
            ps_batch_max_ops = (unsigned int)ops;
            return;
        }
    }

    const char *bits_env = getenv("PISTORM_BATCH_BITS");
    if(bits_env && *bits_env) {
        char *end = NULL;
        unsigned long bits = strtoul(bits_env, &end, 10);
        if(end && *end == '\0' && bits >= 64) {
            if(bits > 2560) {
                bits = 2560;
            }
            unsigned long ops = bits / 32;
            if(ops == 0) {
                ops = 1;
            }
            if(ops > PISTORM_BATCH_MAX) {
                ops = PISTORM_BATCH_MAX;
            }
            ps_batch_max_ops = (unsigned int)ops;
        }
    }
}

struct pistorm_busop_batch {
    uint32_t count;
    uint64_t ptr;   // userspace pointer to ops[]
};

static inline int ps_busop_batch(int ps_fd, struct pistorm_busop *ops, uint32_t count) {
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

static inline int ps_busopq_flush(int ps_fd) {
    if(!g_opsq_n) {
        return 0;
    }
    int rc = ps_busop_batch(ps_fd, g_opsq, g_opsq_n);
    g_opsq_n = 0;
    return rc;
}

static inline int ps_busopq_push(int ps_fd, const struct pistorm_busop *op) {
    g_opsq[g_opsq_n++] = *op;
    if(g_opsq_n >= ps_batch_max_ops) {
        return ps_busopq_flush(ps_fd);
    }
    return 0;
}
#endif // PISTORM_ENABLE_BATCH

static int ps_fd = -1;
static int backend_logged;
static volatile unsigned int gpio_shadow[32];
volatile unsigned int *gpio = gpio_shadow; /* legacy pointer */
static bool ps_queue_enabled = (PISTORM_ENABLE_QUEUE != 0);
static bool ps_queue_error_logged;
static uint64_t ps_queue_full_events;
static uint64_t ps_queue_full_fallbacks;

#ifndef PS_QUEUE_BACKOFF_NS
#define PS_QUEUE_BACKOFF_NS 200000
#endif

#ifndef PS_QUEUE_MAX_RETRIES
#define PS_QUEUE_MAX_RETRIES 200
#endif

static int ps_busop(int is_read, int width, unsigned addr, unsigned *val, unsigned short flags);

static int ps_open_dev(void) {
    if(ps_fd >= 0) {
        return 0;
    }
    ps_fd = open("/dev/pistorm", O_RDWR | O_CLOEXEC);
    if(ps_fd < 0) {
        if(!backend_logged) {
            fprintf(stderr, "[ps_protocol] kmod backend selected but /dev/pistorm missing (%s)\n",
                    strerror(errno));
            backend_logged = 1;
        }
        return -1;
    }
    if(!backend_logged) {
        printf("[ps_protocol] backend=kmod (/dev/pistorm)\n");
        backend_logged = 1;
    }
#if PISTORM_ENABLE_BATCH
    ps_batch_init();
#endif
    return 0;
}

void ps_setup_protocol(void) {
    if(ps_open_dev() < 0) {
        return;
    }
    if(ioctl(ps_fd, PISTORM_IOC_SETUP) < 0) {
        perror("PISTORM_IOC_SETUP");
    }
}

void ps_reset_state_machine(void) {
    if(ps_open_dev() < 0) {
        return;
    }
    if(ioctl(ps_fd, PISTORM_IOC_RESET_SM) < 0) {
        perror("PISTORM_IOC_RESET_SM");
    }
}

void ps_pulse_reset(void) {
    if(ps_open_dev() < 0) {
        return;
    }
    if(ioctl(ps_fd, PISTORM_IOC_PULSE_RESET) < 0) {
        perror("PISTORM_IOC_PULSE_RESET");
    }
}

void ps_protocol_dump_stats(void) {
    struct pistorm_queue_stats stats;
    if(!ps_queue_enabled) {
        fprintf(stderr, "[PS_PROTO] queue disabled (PISTORM_ENABLE_QUEUE=0)\n");
        return;
    }
    if(ps_open_dev() < 0) {
        fprintf(stderr, "[PS_PROTO] queue stats unavailable (device offline)\n");
        return;
    }

    if(ioctl(ps_fd, PISTORM_IOC_QUEUE_STATS, &stats) < 0) {
        fprintf(stderr, "[PS_PROTO] queue stats unavailable (%s)\n", strerror(errno));
        return;
    }

    fprintf(stderr, "[PS_PROTO] queue: enqueued=%llu drained=%llu depth=%u max=%u"
                    " full_events=%" PRIu64 " fallbacks=%" PRIu64 "\n",
            (unsigned long long)stats.enqueued,
            (unsigned long long)stats.drained,
            stats.current_depth, stats.max_depth,
            ps_queue_full_events, ps_queue_full_fallbacks);
}

void ps_fc_write(uint8_t fc) {
    if(ps_open_dev() < 0) {
        return;
    }
#if PISTORM_ENABLE_BATCH
    if(g_opsq_n && ps_batch_last_fc != fc) {
        ps_busopq_flush(ps_fd);
    }
    ps_batch_last_fc = fc;
#endif
    if(log_get_level() >= LOG_LEVEL_VERBOSE) {
        LOG_VERBOSE("[FC] cpld stub (fc=%u)\n", fc);
    }
    /* TODO: implement CPLD FC signaling when kernel/CPLD support is wired up. */
}

static void ps_queue_disable(const char *reason, int err) {
    if(!ps_queue_enabled){
        return;
    }
    ps_queue_enabled = false;
    if(!ps_queue_error_logged) {
        fprintf(stderr, "[ps_protocol] queue disabled (%s: %s)\n", reason,
                strerror(err));
        ps_queue_error_logged = true;
    }
}

static void ps_queue_log_full_event(void) {
    if(ps_queue_full_events != 1 && (ps_queue_full_events & 0xff) != 0) {
        return;
    }

    struct pistorm_queue_stats stats;
    if(ioctl(ps_fd, PISTORM_IOC_QUEUE_STATS, &stats) == 0) {
        LOG_VERBOSE("[PS_QUEUE] full_events=%" PRIu64 " depth=%u max=%u\n",
                    ps_queue_full_events, stats.current_depth, stats.max_depth);
        return;
    }

    LOG_VERBOSE("[PS_QUEUE] full_events=%" PRIu64 " (stats unavailable: %s)\n",
                ps_queue_full_events, strerror(errno));
}

static void ps_flush_queue_before_read(void) {
    if(!ps_queue_enabled) {
        return;
    }
    if(ps_open_dev() < 0) {
        return;
    }
    if(ioctl(ps_fd, PISTORM_IOC_QUEUE_FLUSH) < 0) {
        ps_queue_disable("queue flush", errno);
    }
}

static int ps_queue_enqueue_backpressure(const struct pistorm_busop *op) {
    int tries = 0;

    for (;;) {
        if(ioctl(ps_fd, PISTORM_IOC_QUEUE_ENQUEUE, op) == 0) {
            return 0;
        }

        if(errno != ENOSPC) {
            ps_queue_disable("queue enqueue", errno);
            return -1;
        }

        ps_queue_full_events++;
        ps_queue_log_full_event();

        if(ioctl(ps_fd, PISTORM_IOC_QUEUE_FLUSH) < 0) {
            ps_queue_disable("queue flush", errno);
            return -1;
        }

        if(++tries >= PS_QUEUE_MAX_RETRIES) {
            return -2;
        }

        struct timespec ts = {
            .tv_sec = 0,
            .tv_nsec = PS_QUEUE_BACKOFF_NS,
        };
        nanosleep(&ts, NULL);
    }
}

static void ps_queue_write(uint32_t addr, unsigned width, uint32_t value) {
    uint32_t temp = value;
    if(!ps_queue_enabled || ps_open_dev() < 0) {
        ps_busop(0, (int)width, addr, &temp, 0);
        return;
    }

    struct pistorm_busop op = {
        .addr = addr,
        .value = value,
        .width = (unsigned char)width,
        .is_read = 0,
        .flags = 0,
    };

    int rc = ps_queue_enqueue_backpressure(&op);
    if(rc == 0) {
        return;
    }
    if(rc == -2) {
        ps_queue_full_fallbacks++;
        LOG_VERBOSE("[PS_QUEUE] fallback sync write (full_events=%" PRIu64
                    " fallbacks=%" PRIu64 ")\n",
                    ps_queue_full_events, ps_queue_full_fallbacks);
    }
    ps_busop(0, (int)width, addr, &temp, 0);
}


static int ps_busop(int is_read, int width, unsigned addr, unsigned *val, unsigned short flags) {
    if(ps_open_dev() < 0) {
        return -1;
    }

#if PISTORM_ENABLE_BATCH
    // For read operations, flush any pending writes first to maintain ordering
    if(is_read && g_opsq_n > 0) {
        ps_busopq_flush(ps_fd);
    }

    // For write operations, use batching to reduce ioctl calls
    if(!is_read) {
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
    if(rc == 0) {
        if(is_read && val) {
            *val = op.value;
        }
        if(op.status & PISTORM_BUSOP_ST_BERR) {
            LOG_VERBOSE("[BERR] bus error observed addr=0x%08x\n", addr);
        }
    }
    return rc;
}

uint8_t ps_read_8(uint32_t addr)  {
    uint32_t v = 0;
    ps_flush_queue_before_read();
    ps_busop(1, PISTORM_W8, addr, &v, 0);
    return (uint8_t)(v & 0xff);
}

uint16_t ps_read_16(uint32_t addr) {
    uint32_t v = 0;
    ps_flush_queue_before_read();
    ps_busop(1, PISTORM_W16, addr, &v, 0);
    return (uint16_t)(v & 0xffff);
}

uint32_t ps_read_32(uint32_t addr) {
    uint32_t v = 0;
    ps_flush_queue_before_read();
    ps_busop(1, PISTORM_W32, addr, &v, 0);
    return v;
}

void ps_write_8(uint32_t addr, uint8_t v)  {
    ps_queue_write(addr, PISTORM_W8, (uint32_t)v);
}

void ps_write_16(uint32_t addr, uint16_t v) {
    ps_queue_write(addr, PISTORM_W16, (uint32_t)v);
}

void ps_write_32(uint32_t addr, uint32_t v) {
    ps_queue_write(addr, PISTORM_W32, v);
}

// Additional functions that might be needed
uint16_t ps_read_status_reg(void) {
    ps_flush_queue_before_read();
    struct pistorm_busop op = {
        .addr = 0,
        .value = 0,
        .width = PISTORM_W16,
        .is_read = 1,
        .flags = PISTORM_BUSOP_F_STATUS,
    };

    if(ps_busop(op.is_read, op.width, op.addr, &op.value, op.flags) == 0) {
        return (uint16_t)(op.value & 0xffffu);
    }
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

    if(ps_open_dev() < 0) {
        return gpio_shadow[13];
    }
    if(ioctl(ps_fd, PISTORM_IOC_GET_PINS, &pins) == 0) {
        gpio_shadow[13] = pins.gplev0;
        gpio_shadow[14] = pins.gplev1;
    }
    return gpio_shadow[13];
}

// Public API to flush the batch queue
int ps_flush_batch_queue(void) {
    if(ps_fd < 0) {
        return -1;
    }
#if PISTORM_ENABLE_BATCH
    return ps_busopq_flush(ps_fd);
#else
    return 0;  // No-op when batching is disabled
#endif
}

static void __attribute__((unused)) ps_update_irq(void) {
    unsigned int ipl = 0;

    if(!ps_get_ipl_zero()) {
        unsigned int status = ps_read_status_reg();
        ipl = (status & STATUS_MASK_IPL) >> STATUS_SHIFT_IPL;
    }

    m68k_set_irq(ipl);
}
