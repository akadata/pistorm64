/*
 * src/ppc/test_ppc_qemuuae.c
 *
 * Standalone smoke test for qemu-uae.so PPC runtime.
 *
 * Build (from pistorm64 repo root):
 *   mkdir -p build/ppc
 *   cc -O2 -g -Wall -Wextra -Wpedantic -std=c11 -pthread \
 *      -Isrc/ppc -o build/ppc/test_ppc_qemuuae \
 *      src/ppc/test_ppc_qemuuae.c src/ppc/qemu_uae_loader.c -ldl
 *
 * Run:
 *   unset LD_LIBRARY_PATH
 *   export QEMU_UAE_SO=/usr/local/lib/qemu-uae.so
 *   export PPC_MODEL=603e
 *   export PPC_RUN_MS=50
 *   export PPC_START_TIMEOUT_MS=2000
 *   ./build/ppc/test_ppc_qemuuae
 */

#define _GNU_SOURCE

#include "qemu_uae_loader.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const uint32_t PPC_COUNTER_ADDR = 0x00001000U;
static const uint32_t PPC_RESET_WINDOW_BASE = 0xfff00000U;

typedef struct harness_debug_state {
    pthread_mutex_t lock;
    bool have_last_nip;
    uint32_t last_nip;
} harness_debug_state;

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

static bool io_read32(uint32_t addr, uint32_t *data, int size)
{
    (void)size;
    if (data != NULL) {
        *data = 0xDEADBEEFU;
    }
    fprintf(stderr, "[PPC-IO] read addr=0x%08" PRIx32 " size=%d -> 0x%08" PRIx32 "\n",
            addr, size, (data != NULL) ? *data : 0U);
    return true;
}

static bool io_write32(uint32_t addr, uint32_t data, int size)
{
    fprintf(stderr, "[PPC-IO] write addr=0x%08" PRIx32 " size=%d data=0x%08" PRIx32 "\n",
            addr, size, data);
    return true;
}

static bool io_read64(uint32_t addr, uint64_t *data)
{
    uint64_t value;

    if (data != NULL) {
        *data = 0xDEADBEEFDEADBEEFULL;
    }
    value = (data != NULL) ? *data : UINT64_C(0);
    fprintf(stderr, "[PPC-IO] read64 addr=0x%08" PRIx32 " -> 0x%016" PRIx64 "\n",
            addr, value);
    return true;
}

static bool io_write64(uint32_t addr, uint64_t data)
{
    fprintf(stderr, "[PPC-IO] write64 addr=0x%08" PRIx32 " data=0x%016" PRIx64 "\n",
            addr, data);
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
    fprintf(stderr, "[QEMU-UAE] %s", message);
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

static bool install_counter_program(uint8_t *ram, uint32_t ram_size, uint32_t entry_offset)
{
    /* lis r3, 0x0000 ; ori r3, r3, 0x1000 ; lwz/addi/stw ; b -12 */
    static const uint32_t program_words[] = {
        0x3c600000U,
        0x60631000U,
        0x80830000U,
        0x38840001U,
        0x90830000U,
        0x4bfffff4U
    };
    uint32_t i;
    uint32_t program_size;

    program_size = (uint32_t)(sizeof(program_words));
    if ((entry_offset + program_size) > ram_size) {
        return false;
    }
    if ((PPC_COUNTER_ADDR + 4U) > ram_size) {
        return false;
    }

    for (i = 0U; i < (uint32_t)(sizeof(program_words) / sizeof(program_words[0])); i++) {
        write_be32_at(ram, entry_offset + (i * 4U), program_words[i]);
    }
    write_be32_at(ram, PPC_COUNTER_ADDR, 0U);
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

typedef struct wait_started_context {
    qemu_uae_loader *loader;
} wait_started_context;

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

int main(void)
{
    qemu_uae_loader loader;
    const char *so_path;
    const char *model;
    uint32_t hid1;
    uint32_t ram_size;
    uint8_t *ram;
    PPCMemoryRegion regions[2];
    int run_ms;
    int start_timeout_ms;
    bool ppc_ok;
    uint32_t counter_before;
    uint32_t counter_after;
    bool have_nip;
    uint32_t last_nip;
    uint32_t nip_instruction;

    qemu_uae_loader_warn_if_bad_ld_library_path(stderr);
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
        return 2;
    }

    fprintf(stderr, "[PPC] qemu-uae.so loaded from %s\n", loader.loaded_path);
    fprintf(stderr, "[PPC] callbacks installed\n");

    loader.qemu_uae_init();
    fprintf(stderr, "[PPC] runtime initialized\n");

    if (loader.qemu_uae_version != NULL) {
        int major;
        int minor;
        int revision;
        loader.qemu_uae_version(&major, &minor, &revision);
        fprintf(stderr, "[PPC] qemu-uae API version %d.%d.%d\n", major, minor, revision);
    }

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
        return 3;
    }
    fprintf(stderr, "[PPC] PPC CPU initialized (model=%s hid1=0x%08" PRIx32 ")\n", model, hid1);

    if (loader.ppc_cpu_version != NULL) {
        int major;
        int minor;
        int revision;
        loader.ppc_cpu_version(&major, &minor, &revision);
        fprintf(stderr, "[PPC] PPC backend version %d.%d.%d\n", major, minor, revision);
    }

    ram_size = env_u32_or_default("PPC_RAM_BYTES", 1024U * 1024U);
    if (ram_size < 0x2000U) {
        ram_size = 0x2000U;
    }
    ram = (uint8_t *)calloc(1U, (size_t)ram_size);
    if (ram == NULL) {
        fprintf(stderr, "[PPC] calloc(%u) failed: %s\n", ram_size, strerror(errno));
        return 4;
    }

    if (install_counter_program(ram, ram_size, 0x00000000U) == false) {
        fprintf(stderr, "[PPC] failed to place counter program at 0x00000000\n");
        return 5;
    }
    if (install_counter_program(ram, ram_size, 0x00000100U) == false) {
        fprintf(stderr, "[PPC] failed to place counter program at 0x00000100\n");
        return 5;
    }

    memset(regions, 0, sizeof(regions));
    regions[0].start = 0x00000000U;
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
        return 6;
    }
    fprintf(stderr, "[PPC] runtime started (start -> wait complete)\n");

    counter_before = read_be32_at(ram, PPC_COUNTER_ADDR);
    run_ms = env_int_or_default("PPC_RUN_MS", 50);
    fprintf(stderr, "[PPC] running counter program for %d ms (counter@0x%08x=%" PRIu32 ")\n",
            run_ms, PPC_COUNTER_ADDR, counter_before);

    loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_RUNNING);
    sleep_ms(run_ms);
    loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_PAUSED);

    counter_after = read_be32_at(ram, PPC_COUNTER_ADDR);
    fprintf(stderr, "[PPC] paused: counter@0x%08x=%" PRIu32 "\n",
            PPC_COUNTER_ADDR, counter_after);
    if (counter_after == counter_before) {
        fprintf(stderr, "[PPC] ERROR: counter did not change, PPC code did not execute\n");
        return 7;
    }

    (void)pthread_mutex_lock(&g_debug_state.lock);
    have_nip = g_debug_state.have_last_nip;
    last_nip = g_debug_state.last_nip;
    (void)pthread_mutex_unlock(&g_debug_state.lock);

    if (have_nip == false) {
        fprintf(stderr, "[PPC] ERROR: did not capture NIP from paused CPU dump\n");
        return 8;
    }

    if (fetch_instruction_from_backing(ram, ram_size, last_nip, &nip_instruction) == false) {
        fprintf(stderr,
                "[PPC] ERROR: NIP=0x%08" PRIx32 " not mapped in reset-window backing store\n",
                last_nip);
        return 9;
    }
    fprintf(stderr, "[PPC] fetch sanity: NIP=0x%08" PRIx32 " instr=0x%08" PRIx32 "\n",
            last_nip, nip_instruction);

    if (loader.qemu_uae_main_loop_should_exit != NULL) {
        fprintf(stderr, "[PPC] qemu main loop exit flag: %s\n",
                loader.qemu_uae_main_loop_should_exit() ? "true" : "false");
    }

    fprintf(stderr, "[PPC] harness complete (clean exit path)\n");

    /*
     * Intentionally skip dlclose/free here: qemu_uae_start creates detached
     * threads and the mapped RAM can still be referenced until process exit.
     */
    return 0;
}
