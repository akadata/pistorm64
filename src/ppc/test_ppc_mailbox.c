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
 *   ./build/ppc/test_ppc_mailbox
 */

#define _GNU_SOURCE

#include "ppc_mailbox.h"
#include "qemu_uae_loader.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
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

static uint64_t monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0U;
    }
    return ((uint64_t)ts.tv_sec * 1000U) + ((uint64_t)ts.tv_nsec / 1000000U);
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
     */
    static const uint32_t program_words[] = {
        0x3c600000U, 0x60632000U, 0x80830008U, 0x80a3000cU, 0x7c042800U, 0x4182fff4U,
        0x38c00001U, 0x90c30014U, 0x80e30010U, 0x2c070001U, 0x4182001cU, 0x2c070002U,
        0x41820030U, 0x38c00003U, 0x90c30014U, 0x9083000cU, 0x4bffffc8U, 0x81030018U,
        0x7d0940f8U, 0x91230020U, 0x39400002U, 0x91430014U, 0x9083000cU, 0x4bffffacU,
        0x81030018U, 0x39200040U, 0x7c084800U, 0x40810008U, 0x7d284b78U, 0x39430040U,
        0x39630140U, 0x2c080000U, 0x4182001cU, 0x7d0903a6U, 0x818a0000U, 0x918b0000U,
        0x394a0004U, 0x396b0004U, 0x4200fff0U, 0x91030020U, 0x38c00002U, 0x90c30014U,
        0x9083000cU, 0x4bffff5cU
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
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_VERSION, PPC_MAILBOX_VERSION);
    mailbox_write_u32(mailbox, PPC_MAILBOX_OFF_STATUS, PPC_MAILBOX_STATUS_IDLE);
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
    atomic_thread_fence(memory_order_seq_cst);
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
    uint32_t *seq_in_out)
{
    uint32_t seq;
    int i;
    struct timespec ts_start;
    struct timespec ts_end;
    double elapsed_seconds;
    double ops_per_sec;
    double avg_us_per_op;

    seq = *seq_in_out;

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

    if (clock_gettime(CLOCK_MONOTONIC, &ts_start) != 0) {
        fprintf(stderr, "[PPC] benchmark start clock_gettime failed: %s\n", strerror(errno));
        return false;
    }

    for (i = 0; i < bench_iters; i++) {
        mailbox_result result;
        uint32_t arg0;

        arg0 = 0xa5a50000U ^ (uint32_t)i;
        seq += 1U;
        if (mailbox_send_and_wait(
                mailbox, seq, PPC_MAILBOX_CMD_PING, arg0, 0U, cmd_timeout_ms, &result)
            == false) {
            return false;
        }
        if ((result.status != PPC_MAILBOX_STATUS_DONE)
            || (result.result0 != (arg0 ^ 0xffffffffU))) {
            fprintf(stderr, "[PPC] benchmark validation failed at iter=%d\n", i);
            return false;
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &ts_end) != 0) {
        fprintf(stderr, "[PPC] benchmark end clock_gettime failed: %s\n", strerror(errno));
        return false;
    }

    elapsed_seconds = (double)(ts_end.tv_sec - ts_start.tv_sec)
                      + ((double)(ts_end.tv_nsec - ts_start.tv_nsec) / 1000000000.0);
    if (elapsed_seconds <= 0.0) {
        fprintf(stderr, "[PPC] benchmark elapsed time is non-positive\n");
        return false;
    }

    ops_per_sec = (double)bench_iters / elapsed_seconds;
    avg_us_per_op = (elapsed_seconds * 1000000.0) / (double)bench_iters;
    fprintf(stderr,
            "[PPC] bench: iters=%d warmup=%d elapsed=%.3fs ops/s=%.0f avg_us=%.2f\n",
            bench_iters, warmup_iters, elapsed_seconds, ops_per_sec, avg_us_per_op);

    *seq_in_out = seq;
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
    PPCMemoryRegion regions[2];
    bool ppc_ok;

    qemu_uae_loader_warn_if_bad_ld_library_path(stderr);
    g_verbose = env_bool_or_default("PPC_VERBOSE", false);
    bench_mode = env_bool_or_default("PPC_BENCH", false);
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

    loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_RUNNING);

    cmd_timeout_ms = env_int_or_default("PPC_MAILBOX_CMD_TIMEOUT_MS", 500);
    seq = 0U;

    if (bench_mode == true) {
        bench_iters = env_int_or_default("PPC_BENCH_ITERS", 100000);
        bench_warmup = env_int_or_default("PPC_BENCH_WARMUP", 1000);
        if (run_mailbox_bench(mailbox, bench_warmup, bench_iters, cmd_timeout_ms, &seq) == false) {
            return 8;
        }
    } else {
        loops = env_int_or_default("PPC_MAILBOX_LOOPS", 100);
        if (run_mailbox_functional(mailbox, loops, cmd_timeout_ms, &seq) == false) {
            return 9;
        }
    }

    loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_PAUSED);

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
            return 10;
        }
        if (fetch_instruction_from_backing(ram, ram_size, last_nip, &instruction) == false) {
            fprintf(stderr,
                    "[PPC] ERROR: NIP=0x%08" PRIx32 " not mapped in reset-window backing store\n",
                    last_nip);
            return 11;
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
