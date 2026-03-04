/*
 * src/ppc/test_ppc_qemuuae.c
 *
 * Standalone smoke test for qemu-uae.so PPC runtime.
 *
 * Build (from pistorm64 repo root):
 *   mkdir -p build/ppc
 *   cc -O2 -g -Wall -Wextra -Wpedantic -std=c11 \
 *      -Isrc/ppc -o build/ppc/test_ppc_qemuuae \
 *      src/ppc/test_ppc_qemuuae.c src/ppc/qemu_uae_loader.c -ldl
 *
 * Run:
 *   unset LD_LIBRARY_PATH
 *   export QEMU_UAE_SO=/usr/local/lib/qemu-uae.so
 *   export PPC_MODEL=603e
 *   export PPC_STEPS=100000
 *   ./build/ppc/test_ppc_qemuuae
 */

#include "qemu_uae_loader.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static void harness_log(const char *format, ...)
{
    va_list args;

    fprintf(stderr, "[QEMU-UAE] ");
    va_start(args, format);
    (void)vfprintf(stderr, format, args);
    va_end(args);
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
    clock_t start;
    double elapsed_seconds;
    const double target_seconds = ((double)milliseconds) / 1000.0;

    if (milliseconds <= 0) {
        return;
    }
    start = clock();
    if (start == (clock_t)-1) {
        return;
    }
    while (true) {
        elapsed_seconds = ((double)(clock() - start)) / (double)CLOCKS_PER_SEC;
        if (elapsed_seconds >= target_seconds) {
            break;
        }
    }
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
    int steps;
    int run_ms;

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

    if (qemu_uae_loader_runtime_init(&loader) == false) {
        fprintf(stderr, "[PPC] runtime init failed: %s\n", qemu_uae_loader_error(&loader));
        return 3;
    }
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

    if (qemu_uae_loader_ppc_init(&loader, model, hid1) == false) {
        fprintf(stderr, "[PPC] PPC init failed: %s\n", qemu_uae_loader_error(&loader));
        return 4;
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
    if (ram_size < 0x1000U) {
        ram_size = 0x1000U;
    }

    ram = (uint8_t *)calloc(1U, (size_t)ram_size);
    if (ram == NULL) {
        fprintf(stderr, "[PPC] calloc(%u) failed: %s\n", ram_size, strerror(errno));
        return 5;
    }

    /* 0x48000000 in big-endian is "b ." (branch to self). */
    ram[0] = 0x48;
    ram[1] = 0x00;
    ram[2] = 0x00;
    ram[3] = 0x00;
    ram[0x100] = 0x48;
    ram[0x101] = 0x00;
    ram[0x102] = 0x00;
    ram[0x103] = 0x00;

    memset(regions, 0, sizeof(regions));
    regions[0].start = 0x00000000U;
    regions[0].size = ram_size;
    regions[0].memory = ram;
    regions[0].name = (char *)"ppc-test-ram";
    regions[0].alias = 0U;
    regions[1].start = 0xfff00000U;
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

    if (qemu_uae_loader_runtime_start_and_wait(&loader) == false) {
        fprintf(stderr, "[PPC] runtime start/wait failed: %s\n", qemu_uae_loader_error(&loader));
        return 6;
    }
    fprintf(stderr, "[PPC] runtime started (start -> wait complete)\n");

    if (loader.ppc_cpu_run_single != NULL) {
        steps = env_int_or_default("PPC_STEPS", 100000);
        fprintf(stderr, "[PPC] running single-step batch: %d instructions\n", steps);
        loader.ppc_cpu_run_single(steps);
        loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_PAUSED);
        fprintf(stderr, "[PPC] run_single completed and CPU paused\n");
    } else {
        run_ms = env_int_or_default("PPC_RUN_MS", 10);
        fprintf(stderr, "[PPC] ppc_cpu_run_single unavailable, toggling RUNNING for %d ms\n",
                run_ms);
        loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_RUNNING);
        sleep_ms(run_ms);
        loader.ppc_cpu_set_state(QEMU_UAE_PPC_CPU_STATE_PAUSED);
        fprintf(stderr, "[PPC] CPU paused after controlled run window\n");
    }

    if (loader.qemu_uae_ppc_external_interrupt != NULL) {
        loader.qemu_uae_ppc_external_interrupt(true);
        loader.qemu_uae_ppc_external_interrupt(false);
        fprintf(stderr, "[PPC] external interrupt toggle smoke test complete\n");
    }

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
