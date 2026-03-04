/*
 * src/ppc/test_ppc_mailbox.c
 *
 * Stage 3 mailbox harness:
 * - loads qemu-uae.so with dlopen/dlsym loader
 * - maps PPC RAM + reset window
 * - runs PPC mailbox firmware from RAM
 * - executes host<->PPC mailbox commands in polling mode
 *
 * Build (from pistorm64 repo root):
 *   mkdir -p build/ppc
 *   cc -O2 -g -Wall -Wextra -Wpedantic -std=c11 -pthread \
 *      -Isrc/ppc -o build/ppc/test_ppc_mailbox \
 *      src/ppc/test_ppc_mailbox.c src/ppc/qemu_uae_loader.c -ldl
 *
 * Run:
 *   unset LD_LIBRARY_PATH
 *   export QEMU_UAE_SO=/usr/local/lib/qemu-uae.so
 *   export PPC_MODEL=603e
 *   export PPC_START_TIMEOUT_MS=2000
 *   export PPC_MAILBOX_CMD_TIMEOUT_MS=500
 *   export PPC_MAILBOX_LOOPS=100
 *   export PPC_VERBOSE=1
 *   export PPC_BENCH=0
 *   export PPC_BENCH_MODE=throughput
 *   export PPC_BENCH_ITERS=100000
 *   export PPC_BENCH_SAMPLES=100000
 *   export PPC_BENCH_HISTO=0
 *   export PPC_HOST_SERVICE=1
 *   export PPC_HOST_SERVICE_TEST=1
 *   export PPC_HOSTSVC_DOORBELL=0
 *   export PPC_HOST_SERVICE_CPU=-1
 *   export PPC_HOST_SERVICE_SCHED_FIFO=0
 *   export PPC_HOST_SERVICE_SCHED_PRIO=10
 *   ./build/ppc/test_ppc_mailbox
 */

#define _GNU_SOURCE

#include "ppc_mailbox.h"
#include "qemu_uae_loader.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const uint32_t PPC_RAM_BASE = 0x00000000U;
static const uint32_t PPC_RAM_BYTES_DEFAULT = 1024U * 1024U;
static const uint32_t PPC_RESET_WINDOW_BASE = 0xfff00000U;
static bool g_verbose = false;
static bool g_qemu_log_output = false;
static int g_poll_sleep_ms = 1;

typedef struct harness_debug_state {
    pthread_mutex_t lock;
    bool have_last_nip;
    uint32_t last_nip;
} harness_debug_state;

typedef struct mailbox_result {
    uint32_t ack_seq;
    uint32_t status;
    uint32_t result0;
    uint32_t result1;
} mailbox_result;

typedef struct wait_started_context {
    qemu_uae_loader *loader;
} wait_started_context;

typedef struct host_service_context {
    uint8_t *mailbox;
    uint8_t *ram;
    uint32_t ram_size;
    int idle_sleep_ms;
    int cpu_affinity;
    int sched_priority;
    bool use_sched_fifo;
    bool doorbell_enabled;
    bool verbose;
    qemu_uae_external_interrupt_function external_interrupt;
    volatile bool stop;
} host_service_context;

typedef enum bench_mode_kind {
    BENCH_MODE_THROUGHPUT = 0,
    BENCH_MODE_LATENCY = 1
} bench_mode_kind;

static harness_debug_state g_debug_state = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .have_last_nip = false,
    .last_nip = 0U
};

static void write_be32_at(uint8_t *buffer, uint32_t offset, uint32_t value)
{
    buffer[offset + 0U] = (uint8_t)((value >> 24U) & 0xffU);
    buffer[offset + 1U] = (uint8_t)((value >> 16U) & 0xffU);
    buffer[offset + 2U] = (uint8_t)((value >> 8U) & 0xffU);
    buffer[offset + 3U] = (uint8_t)(value & 0xffU);
}

static uint32_t read_be32_at(const uint8_t *buffer, uint32_t offset)
{
    return ((uint32_t)buffer[offset + 0U] << 24U)
           | ((uint32_t)buffer[offset + 1U] << 16U)
           | ((uint32_t)buffer[offset + 2U] << 8U)
           | ((uint32_t)buffer[offset + 3U]);
}

static uint32_t mailbox_read_u32(const uint8_t *mailbox, uint32_t offset)
{
    return read_be32_at(mailbox, offset);
}

static void mailbox_write_u32(uint8_t *mailbox, uint32_t offset, uint32_t value)
{
    write_be32_at(mailbox, offset, value);
}

static bool monotonic_ns(uint64_t *out_ns)
{
    struct timespec ts;

    if (out_ns == NULL) {
        return false;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return false;
    }
    *out_ns = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
    return true;
}

static uint64_t monotonic_ms(void)
{
    uint64_t ns;

    if (monotonic_ns(&ns) == false) {
        return 0U;
    }
    return ns / 1000000ULL;
}

static bool io_read32(uint32_t addr, uint32_t *data, int size)
{
    (void)size;
    if (data != NULL) {
        *data = 0xDEADBEEFU;
    }
    if (g_verbose == true) {
        fprintf(stderr, "[PPC-IO] read addr=0x%08" PRIx32 " size=%d -> 0x%08" PRIx32 "\n",
                addr, size, (data != NULL) ? *data : 0U);
    }
    return true;
}

static bool io_write32(uint32_t addr, uint32_t data, int size)
{
    if (g_verbose == true) {
        fprintf(stderr, "[PPC-IO] write addr=0x%08" PRIx32 " size=%d data=0x%08" PRIx32 "\n",
                addr, size, data);
    }
    return true;
}

static bool io_read64(uint32_t addr, uint64_t *data)
{
    uint64_t value;

    if (data != NULL) {
        *data = 0xDEADBEEFDEADBEEFULL;
    }
    value = (data != NULL) ? *data : UINT64_C(0);
    if (g_verbose == true) {
        fprintf(stderr, "[PPC-IO] read64 addr=0x%08" PRIx32 " -> 0x%016" PRIx64 "\n",
                addr, value);
    }
    return true;
}

static bool io_write64(uint32_t addr, uint64_t data)
{
    if (g_verbose == true) {
        fprintf(stderr, "[PPC-IO] write64 addr=0x%08" PRIx32 " data=0x%016" PRIx64 "\n",
                addr, data);
    }
    return true;
}

static void capture_nip_from_log_message(const char *message)
{
    const char *marker;
    char *end;
    unsigned long parsed;

    marker = strstr(message, "NIP ");
    if (marker == NULL) {
        return;
    }
    marker += 4;
    while (*marker == ' ') {
        marker++;
    }

    errno = 0;
    parsed = strtoul(marker, &end, 16);
    if ((errno != 0) || (end == marker) || (parsed > 0xffffffffUL)) {
        return;
    }

    (void)pthread_mutex_lock(&g_debug_state.lock);
    g_debug_state.have_last_nip = true;
    g_debug_state.last_nip = (uint32_t)parsed;
    (void)pthread_mutex_unlock(&g_debug_state.lock);
}

static void harness_log(const char *format, ...)
{
    va_list args;
    char message[1024];

    va_start(args, format);
    (void)vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    capture_nip_from_log_message(message);
    if (g_qemu_log_output == true) {
        fprintf(stderr, "[QEMU-UAE] %s", message);
    }
}

static int env_int_or_default(const char *name, int default_value)
{
    const char *value;
    char *end;
    long parsed;

    value = getenv(name);
    if ((value == NULL) || (value[0] == '\0')) {
        return default_value;
    }

    errno = 0;
    parsed = strtol(value, &end, 0);
    if ((errno != 0) || (end == value) || (*end != '\0')) {
        return default_value;
    }
    if ((parsed < 1L) || (parsed > 0x7fffffffL)) {
        return default_value;
    }
    return (int)parsed;
}

static int env_nonneg_int_or_default(const char *name, int default_value)
{
    const char *value;
    char *end;
    long parsed;

    value = getenv(name);
    if ((value == NULL) || (value[0] == '\0')) {
        return default_value;
    }

    errno = 0;
    parsed = strtol(value, &end, 0);
    if ((errno != 0) || (end == value) || (*end != '\0')) {
        return default_value;
    }
    if ((parsed < 0L) || (parsed > 0x7fffffffL)) {
        return default_value;
    }
    return (int)parsed;
}

static uint32_t env_u32_or_default(const char *name, uint32_t default_value)
{
    const char *value;
    char *end;
    unsigned long parsed;

    value = getenv(name);
    if ((value == NULL) || (value[0] == '\0')) {
        return default_value;
    }

    errno = 0;
    parsed = strtoul(value, &end, 0);
    if ((errno != 0) || (end == value) || (*end != '\0')) {
        return default_value;
    }
    if (parsed > 0xffffffffUL) {
        return default_value;
    }
    return (uint32_t)parsed;
}

static bool env_bool_or_default(const char *name, bool default_value)
{
    const char *value;

    value = getenv(name);
    if ((value == NULL) || (value[0] == '\0')) {
        return default_value;
    }

    if ((strcmp(value, "1") == 0) || (strcmp(value, "true") == 0)
        || (strcmp(value, "TRUE") == 0) || (strcmp(value, "yes") == 0)
        || (strcmp(value, "YES") == 0) || (strcmp(value, "on") == 0)
        || (strcmp(value, "ON") == 0)) {
        return true;
    }
    if ((strcmp(value, "0") == 0) || (strcmp(value, "false") == 0)
        || (strcmp(value, "FALSE") == 0) || (strcmp(value, "no") == 0)
        || (strcmp(value, "NO") == 0) || (strcmp(value, "off") == 0)
        || (strcmp(value, "OFF") == 0)) {
        return false;
    }
    return default_value;
}

static bench_mode_kind env_bench_mode_or_default(bench_mode_kind default_mode)
{
    const char *value;

    value = getenv("PPC_BENCH_MODE");
    if ((value == NULL) || (value[0] == '\0')) {
        return default_mode;
    }
    if ((strcmp(value, "throughput") == 0) || (strcmp(value, "tput") == 0)) {
        return BENCH_MODE_THROUGHPUT;
    }
    if ((strcmp(value, "lat") == 0) || (strcmp(value, "latency") == 0)) {
        return BENCH_MODE_LATENCY;
    }

    fprintf(stderr, "[PPC] unknown PPC_BENCH_MODE='%s', defaulting to throughput\n", value);
    return BENCH_MODE_THROUGHPUT;
}

static const char *bench_mode_name(bench_mode_kind mode)
{
    if (mode == BENCH_MODE_LATENCY) {
        return "lat";
    }
    return "throughput";
}

static int compare_u64_ascending(const void *lhs, const void *rhs)
{
    const uint64_t *a;
    const uint64_t *b;

    a = (const uint64_t *)lhs;
    b = (const uint64_t *)rhs;
    if (*a < *b) {
        return -1;
    }
    if (*a > *b) {
        return 1;
    }
    return 0;
}

static size_t percentile_index(size_t count, uint32_t percentile)
{
    size_t rank;
    uint64_t numerator;

    if (count == 0U) {
        return 0U;
    }
    if (percentile >= 100U) {
        return count - 1U;
    }

    numerator = (uint64_t)percentile * (uint64_t)count;
    rank = (size_t)((numerator + 99ULL) / 100ULL);
    if (rank < 1U) {
        rank = 1U;
    }
    if (rank > count) {
        rank = count;
    }
    return rank - 1U;
}

static void print_latency_histogram(const uint64_t *samples_ns, size_t count)
{
    static const uint32_t upper_bounds_us[] = {
        1U, 2U, 5U, 10U, 20U, 50U, 100U, 200U, 500U, 1000U, 2000U, 5000U, 10000U
    };
    uint64_t buckets[(sizeof(upper_bounds_us) / sizeof(upper_bounds_us[0])) + 1U];
    size_t i;
    size_t j;

    memset(buckets, 0, sizeof(buckets));
    for (i = 0U; i < count; i++) {
        uint64_t value_us;
        bool placed;

        value_us = (samples_ns[i] + 999ULL) / 1000ULL;
        placed = false;
        for (j = 0U; j < (sizeof(upper_bounds_us) / sizeof(upper_bounds_us[0])); j++) {
            if (value_us <= upper_bounds_us[j]) {
                buckets[j] += 1U;
                placed = true;
                break;
            }
        }
        if (placed == false) {
            buckets[sizeof(upper_bounds_us) / sizeof(upper_bounds_us[0])] += 1U;
        }
    }

    fprintf(stderr, "[PPC] bench-histo(us):\n");
    for (j = 0U; j < (sizeof(upper_bounds_us) / sizeof(upper_bounds_us[0])); j++) {
        fprintf(stderr, "[PPC]   <=%5" PRIu32 " : %" PRIu64 "\n", upper_bounds_us[j], buckets[j]);
    }
    fprintf(stderr, "[PPC]   >%5" PRIu32 " : %" PRIu64 "\n",
            upper_bounds_us[(sizeof(upper_bounds_us) / sizeof(upper_bounds_us[0])) - 1U],
            buckets[sizeof(upper_bounds_us) / sizeof(upper_bounds_us[0])]);
}

static void sleep_ms(int milliseconds)
{
    struct timespec request;
    struct timespec remainder;

    if (milliseconds <= 0) {
        return;
    }

    request.tv_sec = milliseconds / 1000;
    request.tv_nsec = (long)(milliseconds % 1000) * 1000000L;

    while (nanosleep(&request, &remainder) != 0) {
        if (errno != EINTR) {
            break;
        }
        request = remainder;
    }
}

static bool install_mailbox_firmware(uint8_t *ram, uint32_t ram_size, uint32_t entry_offset)
{
    /*
     * PPC mailbox firmware assembled with llvm-mc:
     * - poll seq/ack_seq
     * - cmd=PING: result0 = ~arg0
     * - cmd=MEMCPY32: copy payload[0:len] -> payload[256:256+len]
     * - cmd=HOST_TIME32 / MEM_CRC32: request host service via host_req_seq lane
     */
    static const uint32_t program_words[] = {
        0x3c600000U, 0x60632000U, 0x80830008U, 0x80a3000cU, 0x7c042800U, 0x4182fff4U,
        0x38c00001U, 0x90c30014U, 0x80e30010U, 0x2c070001U, 0x4182002cU, 0x2c070002U,
        0x41820040U, 0x2c070003U, 0x41820088U, 0x2c070004U, 0x418200e0U, 0x38c00003U,
        0x90c30014U, 0x9083000cU, 0x4bffffb8U, 0x81030018U, 0x7d0940f8U, 0x91230020U,
        0x39400002U, 0x91430014U, 0x9083000cU, 0x4bffff9cU, 0x81030018U, 0x39200040U,
        0x7c084800U, 0x40810008U, 0x7d284b78U, 0x39430040U, 0x39630140U, 0x2c080000U,
        0x4182001cU, 0x7d0903a6U, 0x818a0000U, 0x918b0000U, 0x394a0004U, 0x396b0004U,
        0x4200fff0U, 0x91030020U, 0x38c00002U, 0x90c30014U, 0x9083000cU, 0x4bffff4cU,
        0x82230240U, 0x3a310001U, 0x3a400001U, 0x92430248U, 0x3a600000U, 0x92630250U,
        0x92630254U, 0x9263024cU, 0x7c0004acU, 0x92230240U, 0x82830244U, 0x7c148800U,
        0x4082fff8U, 0x82a3024cU, 0x82c30258U, 0x82e3025cU, 0x92c30020U, 0x92a30024U,
        0x2c150002U, 0x40820078U, 0x38c00002U, 0x90c30014U, 0x9083000cU, 0x4bfffeecU,
        0x82230240U, 0x3a310001U, 0x3a400002U, 0x92430248U, 0x82630018U, 0x8283001cU,
        0x92630250U, 0x92830254U, 0x3aa00000U, 0x92a3024cU, 0x7c0004acU, 0x92230240U,
        0x82c30244U, 0x7c168800U, 0x4082fff8U, 0x82e3024cU, 0x83030258U, 0x93030020U,
        0x92e30024U, 0x2c170002U, 0x40820014U, 0x38c00002U, 0x90c30014U, 0x9083000cU,
        0x4bfffe88U, 0x38c00003U, 0x90c30014U, 0x9083000cU, 0x4bfffe78U
    };
    uint32_t word_count;
    uint32_t i;
    uint32_t program_size;

    word_count = (uint32_t)(sizeof(program_words) / sizeof(program_words[0]));
    program_size = word_count * 4U;
    if ((entry_offset + program_size) > ram_size) {
        return false;
    }

    for (i = 0U; i < word_count; i++) {
        write_be32_at(ram, entry_offset + (i * 4U), program_words[i]);
    }
    return true;
}

static bool fetch_instruction_from_backing(
    const uint8_t *ram,
    uint32_t ram_size,
    uint32_t nip,
    uint32_t *instruction)
{
    uint32_t offset;

    if (instruction == NULL) {
        return false;
    }

    if (nip < ram_size) {
        offset = nip;
    } else if ((nip >= PPC_RESET_WINDOW_BASE)
               && ((nip - PPC_RESET_WINDOW_BASE) < ram_size)) {
        offset = nip - PPC_RESET_WINDOW_BASE;
    } else {
        return false;
    }

    if ((offset + 4U) > ram_size) {
        return false;
    }

    *instruction = read_be32_at(ram, offset);
    return true;
}

static void *wait_started_thread_main(void *opaque)
{
    wait_started_context *context;

    context = (wait_started_context *)opaque;
    if (context == NULL) {
        return NULL;
    }

    if ((context->loader->qemu_uae_mutex_lock != NULL)
        && (context->loader->qemu_uae_mutex_unlock != NULL)) {
        context->loader->qemu_uae_mutex_lock();
        context->loader->qemu_uae_wait_until_started();
        context->loader->qemu_uae_mutex_unlock();
    } else {
        context->loader->qemu_uae_wait_until_started();
    }
    return NULL;
}

static bool wait_until_started_with_timeout(qemu_uae_loader *loader, int timeout_ms)
{
    wait_started_context context;
    pthread_t wait_thread;
    struct timespec deadline;
    int rc;

    if (timeout_ms <= 0) {
        timeout_ms = 2000;
    }

    context.loader = loader;
    rc = pthread_create(&wait_thread, NULL, wait_started_thread_main, &context);
    if (rc != 0) {
        fprintf(stderr, "[PPC] pthread_create(wait_until_started) failed: %s\n", strerror(rc));
        return false;
    }

    rc = clock_gettime(CLOCK_REALTIME, &deadline);
    if (rc != 0) {
        fprintf(stderr, "[PPC] clock_gettime failed while waiting for start: %s\n",
                strerror(errno));
        (void)pthread_cancel(wait_thread);
        (void)pthread_join(wait_thread, NULL);
        return false;
    }

    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    rc = pthread_timedjoin_np(wait_thread, NULL, &deadline);
    if (rc == 0) {
        return true;
    }
    if (rc == ETIMEDOUT) {
        fprintf(stderr, "[PPC] timeout waiting for qemu_uae_wait_until_started (%d ms)\n",
                timeout_ms);
        (void)pthread_cancel(wait_thread);
        (void)pthread_join(wait_thread, NULL);
        return false;
    }

    fprintf(stderr, "[PPC] pthread_timedjoin_np(wait_until_started) failed: %s\n", strerror(rc));
    (void)pthread_cancel(wait_thread);
    (void)pthread_join(wait_thread, NULL);
    return false;
}

static void mailbox_init_page(uint8_t *mailbox)
{
    memset(mailbox, 0, PPC_MAILBOX_SIZE);
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_MAGIC, PPC_MAILBOX_MAGIC);
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_ABI_VERSION, PPC_MAILBOX_VERSION);
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_STATUS, PPC_MAILBOX_STATUS_IDLE);
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_HOST_STATUS, PPC_MAILBOX_STATUS_IDLE);
}

static bool mailbox_send_and_wait(
    uint8_t *mailbox,
    uint32_t seq,
    uint32_t cmd,
    uint32_t arg0,
    uint32_t arg1,
    int timeout_ms,
    mailbox_result *out)
{
    uint64_t deadline;

    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_CMD, cmd);
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_ARG0, arg0);
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_ARG1, arg1);
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_STATUS, PPC_MAILBOX_STATUS_IDLE);
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_RESULT0, 0U);
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_RESULT1, 0U);

    /*
     * Publish payload/args before seq so PPC never sees a partially written
     * request for this sequence number.
     */
    PPC_MAILBOX_MEMORY_BARRIER();
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_SEQ, seq);

    if (g_verbose == true) {
        fprintf(stderr, "[PPC] mailbox: send seq=%" PRIu32 " cmd=%" PRIu32 "\n", seq, cmd);
    }

    deadline = monotonic_ms() + (uint64_t)timeout_ms;
    for (;;) {
        uint32_t ack_seq;
        uint32_t status;
        uint32_t result0;
        uint32_t result1;

        ack_seq = mailbox_read_u32(mailbox, PPC_MAILBOX_OFF_ACK_SEQ);
        status = mailbox_read_u32(mailbox, PPC_MAILBOX_OFF_STATUS);
        result0 = mailbox_read_u32(mailbox, PPC_MAILBOX_OFF_RESULT0);
        result1 = mailbox_read_u32(mailbox, PPC_MAILBOX_OFF_RESULT1);
        if ((ack_seq == seq)
            && ((status == PPC_MAILBOX_STATUS_DONE) || (status == PPC_MAILBOX_STATUS_ERR))) {
            if (out != NULL) {
                out->ack_seq = ack_seq;
                out->status = status;
                out->result0 = result0;
                out->result1 = result1;
            }
            if (g_verbose == true) {
                fprintf(stderr,
                        "[PPC] mailbox: done seq=%" PRIu32 " status=%" PRIu32
                        " result0=0x%08" PRIx32 "\n",
                        seq, status, result0);
            }
            return true;
        }

        if (monotonic_ms() > deadline) {
            fprintf(stderr,
                    "[PPC] mailbox: timeout seq=%" PRIu32 " ack_seq=%" PRIu32
                    " status=%" PRIu32 "\n",
                    seq, ack_seq, status);
            return false;
        }
        if (g_poll_sleep_ms > 0) {
            sleep_ms(g_poll_sleep_ms);
        }
    }
}

static uint32_t crc32_ieee(const uint8_t *data, uint32_t len)
{
    uint32_t crc;
    uint32_t i;
    uint32_t j;

    crc = 0xffffffffU;
    for (i = 0U; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (j = 0U; j < 8U; j++) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1U) ^ 0xedb88320U;
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc ^ 0xffffffffU;
}

static void host_service_configure_thread(host_service_context *ctx)
{
    if (ctx->cpu_affinity >= 0) {
        cpu_set_t cpu_set;
        int rc;

        if (ctx->cpu_affinity >= CPU_SETSIZE) {
            fprintf(stderr, "[PPC] hostsvc: invalid CPU affinity index %d (max %d)\n",
                    ctx->cpu_affinity, CPU_SETSIZE - 1);
        } else {
            CPU_ZERO(&cpu_set);
            CPU_SET(ctx->cpu_affinity, &cpu_set);
            rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
            if (rc != 0) {
                fprintf(stderr, "[PPC] hostsvc: pthread_setaffinity_np(cpu=%d) failed: %s\n",
                        ctx->cpu_affinity, strerror(rc));
            } else if (ctx->verbose == true) {
                fprintf(stderr, "[PPC] hostsvc: pinned thread to cpu=%d\n", ctx->cpu_affinity);
            }
        }
    }

    if (ctx->use_sched_fifo == true) {
        struct sched_param sched;
        int min_prio;
        int max_prio;
        int rc;

        min_prio = sched_get_priority_min(SCHED_FIFO);
        max_prio = sched_get_priority_max(SCHED_FIFO);
        if ((min_prio == -1) || (max_prio == -1)) {
            fprintf(stderr, "[PPC] hostsvc: failed to query SCHED_FIFO priority range: %s\n",
                    strerror(errno));
            return;
        }

        if (ctx->sched_priority < min_prio) {
            ctx->sched_priority = min_prio;
        }
        if (ctx->sched_priority > max_prio) {
            ctx->sched_priority = max_prio;
        }

        memset(&sched, 0, sizeof(sched));
        sched.sched_priority = ctx->sched_priority;
        rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sched);
        if (rc != 0) {
            fprintf(stderr,
                    "[PPC] hostsvc: pthread_setschedparam(SCHED_FIFO, prio=%d) failed: %s\n",
                    ctx->sched_priority, strerror(rc));
        } else if (ctx->verbose == true) {
            fprintf(stderr, "[PPC] hostsvc: enabled SCHED_FIFO prio=%d\n", ctx->sched_priority);
        }
    }
}

static void host_service_ring_doorbell(const host_service_context *ctx)
{
    if ((ctx->doorbell_enabled == false) || (ctx->external_interrupt == NULL)) {
        return;
    }

    /*
     * Pulse EXT interrupt line so PPC can observe host response promptly even
     * when the host service thread is configured with a longer idle sleep.
     */
    ctx->external_interrupt(true);
    ctx->external_interrupt(false);
}

static bool host_service_handle_request(host_service_context *ctx, uint32_t req_seq)
{
    uint32_t cmd;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t status;
    uint32_t result0;
    uint32_t result1;

    cmd = mailbox_read_u32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_CMD);
    arg0 = mailbox_read_u32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_ARG0);
    arg1 = mailbox_read_u32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_ARG1);

    status = PPC_MAILBOX_STATUS_ERR;
    result0 = 0U;
    result1 = 0U;

    if (cmd == PPC_MAILBOX_HOST_CMD_TIME32) {
        struct timespec ts;
        uint64_t ns;

        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
            ns = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
            result0 = (uint32_t)(ns & 0xffffffffU);
            status = PPC_MAILBOX_STATUS_DONE;
        } else {
            status = PPC_MAILBOX_STATUS_ERR;
        }
    } else if (cmd == PPC_MAILBOX_HOST_CMD_MEM_CRC32) {
        uint64_t end_addr;

        end_addr = (uint64_t)arg0 + (uint64_t)arg1;
        if ((arg0 >= ctx->ram_size) || (end_addr > ctx->ram_size)) {
            status = PPC_MAILBOX_STATUS_RANGE;
        } else {
            result0 = crc32_ieee(ctx->ram + arg0, arg1);
            status = PPC_MAILBOX_STATUS_DONE;
        }
    } else {
        status = PPC_MAILBOX_STATUS_ERR;
    }

    mailbox_write_u32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_RESULT0, result0);
    mailbox_write_u32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_RESULT1, result1);
    mailbox_write_u32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_STATUS, status);
    PPC_MAILBOX_MEMORY_BARRIER();
    mailbox_write_u32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_ACK_SEQ, req_seq);
    PPC_MAILBOX_MEMORY_BARRIER();
    host_service_ring_doorbell(ctx);

    if (ctx->verbose == true) {
        fprintf(stderr,
                "[PPC] hostsvc: req_seq=%" PRIu32 " cmd=%" PRIu32
                " status=%" PRIu32 " result0=0x%08" PRIx32 "\n",
                req_seq, cmd, status, result0);
    }

    return status == PPC_MAILBOX_STATUS_DONE;
}

static void *host_service_thread_main(void *opaque)
{
    host_service_context *ctx;

    ctx = (host_service_context *)opaque;
    if (ctx == NULL) {
        return NULL;
    }

    host_service_configure_thread(ctx);

    while (ctx->stop == false) {
        uint32_t req_seq;
        uint32_t ack_seq;

        req_seq = mailbox_read_u32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_REQ_SEQ);
        ack_seq = mailbox_read_u32(ctx->mailbox, PPC_MAILBOX_OFF_HOST_ACK_SEQ);
        if (req_seq == ack_seq) {
            if (ctx->idle_sleep_ms > 0) {
                sleep_ms(ctx->idle_sleep_ms);
            } else {
                sched_yield();
            }
            continue;
        }
        (void)host_service_handle_request(ctx, req_seq);
    }
    return NULL;
}

static bool run_host_service_proofs(
    uint8_t *mailbox,
    uint8_t *ram,
    uint32_t ram_size,
    int cmd_timeout_ms,
    uint32_t *seq_in_out)
{
    mailbox_result time_result_1;
    mailbox_result time_result_2;
    mailbox_result crc_result;
    uint32_t seq;
    uint32_t crc_addr;
    uint32_t crc_len;
    uint32_t expected_crc;
    uint32_t i;

    seq = *seq_in_out;

    seq += 1U;
    if (mailbox_send_and_wait(
            mailbox, seq, PPC_MAILBOX_CMD_HOST_TIME32, 0U, 0U, cmd_timeout_ms, &time_result_1)
        == false) {
        fprintf(stderr, "[PPC] host-time request #1 failed\n");
        return false;
    }
    if (time_result_1.status != PPC_MAILBOX_STATUS_DONE) {
        fprintf(stderr, "[PPC] host-time status #1 invalid: %" PRIu32 "\n", time_result_1.status);
        return false;
    }

    sleep_ms(1);

    seq += 1U;
    if (mailbox_send_and_wait(
            mailbox, seq, PPC_MAILBOX_CMD_HOST_TIME32, 0U, 0U, cmd_timeout_ms, &time_result_2)
        == false) {
        fprintf(stderr, "[PPC] host-time request #2 failed\n");
        return false;
    }
    if (time_result_2.status != PPC_MAILBOX_STATUS_DONE) {
        fprintf(stderr, "[PPC] host-time status #2 invalid: %" PRIu32 "\n", time_result_2.status);
        return false;
    }
    if (time_result_2.result0 == time_result_1.result0) {
        fprintf(stderr, "[PPC] host-time values did not change: 0x%08" PRIx32 "\n",
                time_result_2.result0);
        return false;
    }

    crc_addr = 0x00008000U;
    crc_len = 512U;
    if ((crc_addr + crc_len) > ram_size) {
        fprintf(stderr, "[PPC] CRC proof buffer does not fit mapped RAM\n");
        return false;
    }
    for (i = 0U; i < crc_len; i++) {
        ram[crc_addr + i] = (uint8_t)((i * 7U) ^ 0xa5U);
    }
    expected_crc = crc32_ieee(ram + crc_addr, crc_len);

    seq += 1U;
    if (mailbox_send_and_wait(
            mailbox, seq, PPC_MAILBOX_CMD_MEM_CRC32, crc_addr, crc_len, cmd_timeout_ms,
            &crc_result)
        == false) {
        fprintf(stderr, "[PPC] mem-crc32 request failed\n");
        return false;
    }
    if (crc_result.status != PPC_MAILBOX_STATUS_DONE) {
        fprintf(stderr, "[PPC] mem-crc32 status invalid: %" PRIu32 " host_status=0x%08" PRIx32
                "\n", crc_result.status, crc_result.result1);
        return false;
    }
    if (crc_result.result0 != expected_crc) {
        fprintf(stderr,
                "[PPC] mem-crc32 mismatch: expected=0x%08" PRIx32 " got=0x%08" PRIx32 "\n",
                expected_crc, crc_result.result0);
        return false;
    }

    fprintf(stderr,
            "[PPC] hostsvc proof: time32 0x%08" PRIx32 " -> 0x%08" PRIx32
            ", crc32=0x%08" PRIx32 "\n",
            time_result_1.result0, time_result_2.result0, crc_result.result0);

    *seq_in_out = seq;
    return true;
}

static bool run_mailbox_functional(
    uint8_t *mailbox,
    int loops,
    int cmd_timeout_ms,
    uint32_t *seq_in_out)
{
    uint32_t i;
    uint32_t seq;

    seq = *seq_in_out;
    for (i = 0U; i < (uint32_t)loops; i++) {
        mailbox_result ping_result;
        mailbox_result memcpy_result;
        uint32_t ping_arg0;
        uint32_t len_words;
        uint32_t j;

        ping_arg0 = 0x12345678U ^ i;
        seq += 1U;
        if (mailbox_send_and_wait(
                mailbox, seq, PPC_MAILBOX_CMD_PING, ping_arg0, 0U, cmd_timeout_ms, &ping_result)
            == false) {
            return false;
        }
        if (ping_result.status != PPC_MAILBOX_STATUS_DONE) {
            fprintf(stderr, "[PPC] mailbox ping status error: %" PRIu32 "\n", ping_result.status);
            return false;
        }
        if (ping_result.result0 != (ping_arg0 ^ 0xffffffffU)) {
            fprintf(stderr,
                    "[PPC] mailbox ping mismatch: arg0=0x%08" PRIx32 " result0=0x%08" PRIx32 "\n",
                    ping_arg0, ping_result.result0);
            return false;
        }

        len_words = (i % PPC_MAILBOX_MEMCPY32_MAX_WORDS) + 1U;
        for (j = 0U; j < len_words; j++) {
            uint32_t value;
            uint32_t in_off;
            uint32_t out_off;

            value = (i << 16U) | j;
            in_off = PPC_MAILBOX_OFF_PAYLOAD + PPC_MAILBOX_PAYLOAD_IN_OFFSET + (j * 4U);
            out_off = PPC_MAILBOX_OFF_PAYLOAD + PPC_MAILBOX_PAYLOAD_OUT_OFFSET + (j * 4U);
            mailbox_write_u32(mailbox, in_off, value);
            mailbox_write_u32(mailbox, out_off, 0U);
        }

        seq += 1U;
        if (mailbox_send_and_wait(
                mailbox, seq, PPC_MAILBOX_CMD_MEMCPY32, len_words, 0U,
                cmd_timeout_ms, &memcpy_result)
            == false) {
            return false;
        }
        if (memcpy_result.status != PPC_MAILBOX_STATUS_DONE) {
            fprintf(stderr, "[PPC] mailbox memcpy status error: %" PRIu32 "\n",
                    memcpy_result.status);
            return false;
        }
        if (memcpy_result.result0 != len_words) {
            fprintf(stderr, "[PPC] mailbox memcpy result length mismatch: expected=%" PRIu32
                    " got=%" PRIu32 "\n", len_words, memcpy_result.result0);
            return false;
        }

        for (j = 0U; j < len_words; j++) {
            uint32_t expected;
            uint32_t got;
            uint32_t out_off;

            expected = (i << 16U) | j;
            out_off = PPC_MAILBOX_OFF_PAYLOAD + PPC_MAILBOX_PAYLOAD_OUT_OFFSET + (j * 4U);
            got = mailbox_read_u32(mailbox, out_off);
            if (got != expected) {
                fprintf(stderr, "[PPC] mailbox memcpy verify mismatch at word %" PRIu32
                        ": expected=0x%08" PRIx32 " got=0x%08" PRIx32 "\n",
                        j, expected, got);
                return false;
            }
        }
    }
    *seq_in_out = seq;
    return true;
}

static bool run_mailbox_bench(
    uint8_t *mailbox,
    int warmup_iters,
    int bench_iters,
    int cmd_timeout_ms,
    uint32_t *seq_in_out,
    bench_mode_kind mode,
    bool histogram_enabled)
{
    uint32_t seq;
    int i;
    uint64_t start_ns;
    uint64_t end_ns;
    uint64_t elapsed_ns;
    double elapsed_seconds;
    double ops_per_sec;
    double avg_us_per_op;
    uint64_t *samples_ns;

    seq = *seq_in_out;
    samples_ns = NULL;

    if (bench_iters <= 0) {
        fprintf(stderr, "[PPC] benchmark iterations must be > 0\n");
        return false;
    }

    for (i = 0; i < warmup_iters; i++) {
        mailbox_result result;
        uint32_t arg0;

        arg0 = 0x12345678U ^ (uint32_t)i;
        seq += 1U;
        if (mailbox_send_and_wait(
                mailbox, seq, PPC_MAILBOX_CMD_PING, arg0, 0U, cmd_timeout_ms, &result)
            == false) {
            return false;
        }
        if ((result.status != PPC_MAILBOX_STATUS_DONE)
            || (result.result0 != (arg0 ^ 0xffffffffU))) {
            fprintf(stderr, "[PPC] benchmark warmup validation failed at iter=%d\n", i);
            return false;
        }
    }

    if (mode == BENCH_MODE_LATENCY) {
        samples_ns = (uint64_t *)calloc((size_t)bench_iters, sizeof(*samples_ns));
        if (samples_ns == NULL) {
            fprintf(stderr, "[PPC] benchmark sample allocation failed (%d)\n", bench_iters);
            return false;
        }
    }

    if (monotonic_ns(&start_ns) == false) {
        fprintf(stderr, "[PPC] benchmark start clock_gettime failed: %s\n", strerror(errno));
        free(samples_ns);
        return false;
    }

    for (i = 0; i < bench_iters; i++) {
        mailbox_result result;
        uint32_t arg0;
        uint64_t op_start_ns;
        uint64_t op_end_ns;

        arg0 = 0xa5a50000U ^ (uint32_t)i;
        op_start_ns = 0U;
        if (mode == BENCH_MODE_LATENCY) {
            if (monotonic_ns(&op_start_ns) == false) {
                fprintf(stderr, "[PPC] benchmark op-start clock_gettime failed: %s\n",
                        strerror(errno));
                free(samples_ns);
                return false;
            }
        }
        seq += 1U;
        if (mailbox_send_and_wait(
                mailbox, seq, PPC_MAILBOX_CMD_PING, arg0, 0U, cmd_timeout_ms, &result)
            == false) {
            free(samples_ns);
            return false;
        }
        if ((result.status != PPC_MAILBOX_STATUS_DONE)
            || (result.result0 != (arg0 ^ 0xffffffffU))) {
            fprintf(stderr, "[PPC] benchmark validation failed at iter=%d\n", i);
            free(samples_ns);
            return false;
        }
        if (mode == BENCH_MODE_LATENCY) {
            if (monotonic_ns(&op_end_ns) == false) {
                fprintf(stderr, "[PPC] benchmark op-end clock_gettime failed: %s\n",
                        strerror(errno));
                free(samples_ns);
                return false;
            }
            if (op_end_ns < op_start_ns) {
                fprintf(stderr, "[PPC] benchmark op duration underflow at iter=%d\n", i);
                free(samples_ns);
                return false;
            }
            samples_ns[i] = op_end_ns - op_start_ns;
        }
    }

    if (monotonic_ns(&end_ns) == false) {
        fprintf(stderr, "[PPC] benchmark end clock_gettime failed: %s\n", strerror(errno));
        free(samples_ns);
        return false;
    }
    if (end_ns < start_ns) {
        fprintf(stderr, "[PPC] benchmark elapsed time underflow\n");
        free(samples_ns);
        return false;
    }

    elapsed_ns = end_ns - start_ns;
    elapsed_seconds = (double)elapsed_ns / 1000000000.0;
    if (elapsed_seconds <= 0.0) {
        fprintf(stderr, "[PPC] benchmark elapsed time is non-positive\n");
        free(samples_ns);
        return false;
    }

    ops_per_sec = (double)bench_iters / elapsed_seconds;
    avg_us_per_op = (elapsed_seconds * 1000000.0) / (double)bench_iters;
    if (mode == BENCH_MODE_THROUGHPUT) {
        fprintf(stderr,
                "[PPC] bench: mode=%s iters=%d warmup=%d elapsed=%.3fs ops/s=%.0f avg_us=%.2f\n",
                bench_mode_name(mode), bench_iters, warmup_iters, elapsed_seconds, ops_per_sec,
                avg_us_per_op);
    } else {
        uint64_t p50_ns;
        uint64_t p95_ns;
        uint64_t p99_ns;
        uint64_t max_ns;

        qsort(samples_ns, (size_t)bench_iters, sizeof(*samples_ns), compare_u64_ascending);
        p50_ns = samples_ns[percentile_index((size_t)bench_iters, 50U)];
        p95_ns = samples_ns[percentile_index((size_t)bench_iters, 95U)];
        p99_ns = samples_ns[percentile_index((size_t)bench_iters, 99U)];
        max_ns = samples_ns[(size_t)bench_iters - 1U];
        fprintf(stderr,
                "[PPC] bench-lat: mode=%s samples=%d warmup=%d elapsed=%.3fs ops/s=%.0f "
                "avg_us=%.2f p50_us=%.2f p95_us=%.2f p99_us=%.2f max_us=%.2f\n",
                bench_mode_name(mode), bench_iters, warmup_iters, elapsed_seconds, ops_per_sec,
                avg_us_per_op,
                (double)p50_ns / 1000.0, (double)p95_ns / 1000.0, (double)p99_ns / 1000.0,
                (double)max_ns / 1000.0);
        if (histogram_enabled == true) {
            print_latency_histogram(samples_ns, (size_t)bench_iters);
        }
    }

    *seq_in_out = seq;
    free(samples_ns);
    return true;
}

int main(void)
{
    qemu_uae_loader loader;
    const char *so_path;
    const char *model;
    uint32_t hid1;
    uint32_t ram_size;
    uint8_t *ram;
    uint8_t *mailbox;
    uint32_t seq;
    int loops = 0;
    int bench_iters;
    int bench_warmup;
    int start_timeout_ms;
    int cmd_timeout_ms;
    bool bench_mode;
    bool bench_histo;
    bench_mode_kind bench_kind;
    bool host_service_enabled;
    bool host_service_test;
    host_service_context host_ctx;
    pthread_t host_thread;
    bool host_thread_started;
    int exit_code;
    PPCMemoryRegion regions[2];
    bool ppc_ok;

    qemu_uae_loader_warn_if_bad_ld_library_path(stderr);
    g_verbose = env_bool_or_default("PPC_VERBOSE", false);
    bench_mode = env_bool_or_default("PPC_BENCH", false);
    bench_kind = BENCH_MODE_THROUGHPUT;
    bench_histo = false;
    if (bench_mode == true) {
        bench_kind = env_bench_mode_or_default(BENCH_MODE_THROUGHPUT);
        bench_histo = env_bool_or_default("PPC_BENCH_HISTO", false);
    }
    if (bench_mode == true) {
        /*
         * Keep benchmark measurements representative of mailbox throughput by
         * defaulting to quiet logging in benchmark mode.
         */
        g_qemu_log_output = false;
    } else {
        g_qemu_log_output = g_verbose;
    }
    g_poll_sleep_ms = env_nonneg_int_or_default(
        "PPC_MAILBOX_POLL_SLEEP_MS",
        bench_mode ? 0 : 1);
    host_service_enabled = env_bool_or_default("PPC_HOST_SERVICE", bench_mode ? false : true);
    host_service_test = env_bool_or_default(
        "PPC_HOST_SERVICE_TEST",
        (bench_mode == false) && (host_service_enabled == true));
    host_thread_started = false;
    exit_code = 0;
    qemu_uae_loader_init(&loader);

    so_path = getenv("QEMU_UAE_SO");
    if ((so_path == NULL) || (so_path[0] == '\0')) {
        so_path = "/usr/local/lib/qemu-uae.so";
    }
    if (qemu_uae_loader_open(&loader, so_path) == false) {
        fprintf(stderr, "[PPC] loader open failed: %s\n", qemu_uae_loader_error(&loader));
        return 1;
    }

    if (qemu_uae_loader_install_io_callbacks(&loader, io_read32, io_write32, io_read64, io_write64)
        == false) {
        fprintf(stderr, "[PPC] install IO callbacks failed: %s\n", qemu_uae_loader_error(&loader));
        return 2;
    }
    if (qemu_uae_loader_install_log_callback(&loader, harness_log) == false) {
        fprintf(stderr, "[PPC] install log callback failed: %s\n", qemu_uae_loader_error(&loader));
        return 3;
    }

    fprintf(stderr, "[PPC] qemu-uae.so loaded from %s\n", loader.loaded_path);
    fprintf(stderr, "[PPC] callbacks installed\n");

    loader.qemu_uae_init();
    fprintf(stderr, "[PPC] runtime initialized\n");

    model = getenv("PPC_MODEL");
    if ((model == NULL) || (model[0] == '\0')) {
        model = "603e";
    }
    hid1 = env_u32_or_default("PPC_HID1", 0U);

    ppc_ok = false;
    if (loader.qemu_uae_ppc_init != NULL) {
        ppc_ok = loader.qemu_uae_ppc_init(model, hid1);
    }
    if ((ppc_ok == false) && (loader.ppc_cpu_init != NULL)) {
        ppc_ok = loader.ppc_cpu_init(model, hid1);
    }
    if (ppc_ok == false) {
        fprintf(stderr, "[PPC] PPC init failed (model=%s hid1=0x%08" PRIx32 ")\n",
                model, hid1);
        return 4;
    }
    fprintf(stderr, "[PPC] PPC CPU initialized (model=%s hid1=0x%08" PRIx32 ")\n", model, hid1);

    ram_size = env_u32_or_default("PPC_RAM_BYTES", PPC_RAM_BYTES_DEFAULT);
    if (ram_size < (PPC_MAILBOX_PPC_ADDR + PPC_MAILBOX_SIZE + 0x1000U)) {
        ram_size = PPC_MAILBOX_PPC_ADDR + PPC_MAILBOX_SIZE + 0x1000U;
    }

    ram = (uint8_t *)calloc(1U, (size_t)ram_size);
    if (ram == NULL) {
        fprintf(stderr, "[PPC] calloc(%u) failed: %s\n", ram_size, strerror(errno));
        return 5;
    }
    mailbox = ram + PPC_MAILBOX_PPC_ADDR;
    mailbox_init_page(mailbox);
    if ((mailbox_read_u32(mailbox, PPC_MAILBOX_OFF_MAGIC) != PPC_MAILBOX_MAGIC)
        || (mailbox_read_u32(mailbox, PPC_MAILBOX_OFF_ABI_VERSION) != PPC_MAILBOX_VERSION)) {
        fprintf(stderr, "[PPC] mailbox ABI initialization failed\n");
        return 5;
    }
    memset(&host_ctx, 0, sizeof(host_ctx));
    host_ctx.mailbox = mailbox;
    host_ctx.ram = ram;
    host_ctx.ram_size = ram_size;
    host_ctx.idle_sleep_ms = env_nonneg_int_or_default("PPC_HOST_SERVICE_IDLE_MS", 1);
    host_ctx.cpu_affinity = env_nonneg_int_or_default("PPC_HOST_SERVICE_CPU", -1);
    host_ctx.use_sched_fifo = env_bool_or_default("PPC_HOST_SERVICE_SCHED_FIFO", false);
    host_ctx.sched_priority = env_int_or_default("PPC_HOST_SERVICE_SCHED_PRIO", 10);
    host_ctx.doorbell_enabled = env_bool_or_default("PPC_HOSTSVC_DOORBELL", false);
    host_ctx.verbose = g_verbose;
    host_ctx.external_interrupt = loader.qemu_uae_ppc_external_interrupt;
    host_ctx.stop = false;
    if ((host_ctx.doorbell_enabled == true) && (host_ctx.external_interrupt == NULL)) {
        fprintf(stderr,
                "[PPC] hostsvc: PPC_HOSTSVC_DOORBELL=1 requested but "
                "qemu_uae_ppc_external_interrupt is unavailable; disabling doorbell\n");
        host_ctx.doorbell_enabled = false;
    }

    if (install_mailbox_firmware(ram, ram_size, 0x00000000U) == false) {
        fprintf(stderr, "[PPC] failed to place mailbox firmware at 0x00000000\n");
        return 6;
    }
    if (install_mailbox_firmware(ram, ram_size, 0x00000100U) == false) {
        fprintf(stderr, "[PPC] failed to place mailbox firmware at 0x00000100\n");
        return 6;
    }

    memset(regions, 0, sizeof(regions));
    regions[0].start = PPC_RAM_BASE;
    regions[0].size = ram_size;
    regions[0].memory = ram;
    regions[0].name = (char *)"ppc-test-ram";
    regions[0].alias = 0U;
    regions[1].start = PPC_RESET_WINDOW_BASE;
    regions[1].size = ram_size;
    regions[1].memory = ram;
    regions[1].name = (char *)"ppc-reset-window";
    regions[1].alias = 0U;

    loader.ppc_cpu_map_memory(regions, 2);
    loader.ppc_cpu_reset();

    if (loader.ppc_cpu_set_pc != NULL) {
        loader.ppc_cpu_set_pc(0, 0x00000000U);
        fprintf(stderr, "[PPC] PC set to 0x00000000\n");
    } else {
        fprintf(stderr, "[PPC] ppc_cpu_set_pc not exported; using reset/start defaults\n");
    }

    start_timeout_ms = env_int_or_default("PPC_START_TIMEOUT_MS", 2000);
    loader.qemu_uae_start();
    if (wait_until_started_with_timeout(&loader, start_timeout_ms) == false) {
        return 7;
    }
    fprintf(stderr, "[PPC] runtime started (start -> wait complete)\n");

    if (host_service_enabled == true) {
        int thread_rc;

        thread_rc = pthread_create(&host_thread, NULL, host_service_thread_main, &host_ctx);
        if (thread_rc != 0) {
            fprintf(stderr, "[PPC] failed to start host service thread: %s\n", strerror(thread_rc));
            return 8;
        }
        host_thread_started = true;
        if (g_verbose == true) {
            fprintf(stderr,
                    "[PPC] hostsvc: idle_ms=%d cpu=%d doorbell=%s sched_fifo=%s prio=%d\n",
                    host_ctx.idle_sleep_ms,
                    host_ctx.cpu_affinity,
                    host_ctx.doorbell_enabled ? "on" : "off",
                    host_ctx.use_sched_fifo ? "on" : "off",
                    host_ctx.sched_priority);
        }
    }

    loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_RUNNING);

    cmd_timeout_ms = env_int_or_default("PPC_MAILBOX_CMD_TIMEOUT_MS", 500);
    seq = 0U;

    if (bench_mode == true) {
        if (bench_kind == BENCH_MODE_LATENCY) {
            bench_iters = env_int_or_default(
                "PPC_BENCH_SAMPLES",
                env_int_or_default("PPC_BENCH_ITERS", 100000));
        } else {
            bench_iters = env_int_or_default("PPC_BENCH_ITERS", 100000);
        }
        bench_warmup = env_int_or_default("PPC_BENCH_WARMUP", 1000);
        if (run_mailbox_bench(
                mailbox,
                bench_warmup,
                bench_iters,
                cmd_timeout_ms,
                &seq,
                bench_kind,
                bench_histo)
            == false) {
            exit_code = 9;
            goto shutdown;
        }
    } else {
        loops = env_int_or_default("PPC_MAILBOX_LOOPS", 100);
        if (run_mailbox_functional(mailbox, loops, cmd_timeout_ms, &seq) == false) {
            exit_code = 10;
            goto shutdown;
        }
        if (host_service_test == true) {
            if (run_host_service_proofs(mailbox, ram, ram_size, cmd_timeout_ms, &seq) == false) {
                exit_code = 11;
                goto shutdown;
            }
        }
    }

shutdown:
    loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_PAUSED);
    if (host_thread_started == true) {
        host_ctx.stop = true;
        (void)pthread_join(host_thread, NULL);
    }
    if (exit_code != 0) {
        return exit_code;
    }

    {
        bool have_nip;
        uint32_t last_nip;
        uint32_t instruction;

        (void)pthread_mutex_lock(&g_debug_state.lock);
        have_nip = g_debug_state.have_last_nip;
        last_nip = g_debug_state.last_nip;
        (void)pthread_mutex_unlock(&g_debug_state.lock);

        if (have_nip == false) {
            fprintf(stderr, "[PPC] ERROR: did not capture NIP from paused CPU dump\n");
            return 12;
        }
        if (fetch_instruction_from_backing(ram, ram_size, last_nip, &instruction) == false) {
            fprintf(stderr,
                    "[PPC] ERROR: NIP=0x%08" PRIx32 " not mapped in reset-window backing store\n",
                    last_nip);
            return 13;
        }
        fprintf(stderr, "[PPC] fetch sanity: NIP=0x%08" PRIx32 " instr=0x%08" PRIx32 "\n",
                last_nip, instruction);
    }

    if (loader.qemu_uae_main_loop_should_exit != NULL) {
        fprintf(stderr, "[PPC] qemu main loop exit flag: %s\n",
                loader.qemu_uae_main_loop_should_exit() ? "true" : "false");
    }

    if (bench_mode == true) {
        fprintf(stderr, "[PPC] mailbox benchmark complete\n");
    } else {
        fprintf(stderr, "[PPC] mailbox test complete: loops=%d\n", loops);
    }
    return 0;
}
