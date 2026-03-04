#ifndef QEMU_UAE_LOADER_H
#define QEMU_UAE_LOADER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct PPCMemoryRegion {
    uint32_t start;
    uint32_t size;
    void *memory;
    char *name;
    uint32_t alias;
} PPCMemoryRegion;

typedef bool (*qemu_uae_io_mem_read_function)(uint32_t addr, uint32_t *data, int size);
typedef bool (*qemu_uae_io_mem_write_function)(uint32_t addr, uint32_t data, int size);
typedef bool (*qemu_uae_io_mem_read64_function)(uint32_t addr, uint64_t *data);
typedef bool (*qemu_uae_io_mem_write64_function)(uint32_t addr, uint64_t data);
typedef void (*qemu_uae_log_function)(const char *format, ...);

typedef void (*qemu_uae_void_function)(void);
typedef bool (*qemu_uae_ppc_init_function)(const char *model, uint32_t hid1);
typedef bool (*qemu_uae_ppc_cpu_init_function)(const char *model, uint32_t hid1);
typedef void (*qemu_uae_ppc_map_memory_function)(PPCMemoryRegion *regions, int count);
typedef void (*qemu_uae_ppc_set_state_function)(int state);
typedef void (*qemu_uae_ppc_set_pc_function)(int cpu, uint32_t value);
typedef void (*qemu_uae_ppc_run_single_function)(int count);
typedef void (*qemu_uae_ppc_pause_function)(int pause);
typedef bool (*qemu_uae_ppc_check_state_function)(int state);
typedef void (*qemu_uae_version_function)(int *major, int *minor, int *revision);
typedef bool (*qemu_uae_bool_void_function)(void);
typedef void (*qemu_uae_external_interrupt_function)(bool enable);
typedef int (*qemu_uae_lock_function)(int type);

enum {
    QEMU_UAE_PPC_CPU_STATE_RUNNING = 1,
    QEMU_UAE_PPC_CPU_STATE_PAUSED = 2
};

typedef struct qemu_uae_loader {
    void *handle;
    bool runtime_started;
    char error[256];
    const char *loaded_path;

    qemu_uae_void_function qemu_uae_init;
    qemu_uae_void_function qemu_uae_start;
    qemu_uae_void_function qemu_uae_wait_until_started;
    qemu_uae_ppc_init_function qemu_uae_ppc_init;
    qemu_uae_ppc_cpu_init_function ppc_cpu_init;
    qemu_uae_ppc_map_memory_function ppc_cpu_map_memory;
    qemu_uae_void_function ppc_cpu_reset;
    qemu_uae_ppc_set_state_function ppc_cpu_set_state;

    qemu_uae_ppc_set_pc_function ppc_cpu_set_pc;
    qemu_uae_ppc_run_single_function ppc_cpu_run_single;
    qemu_uae_void_function ppc_cpu_run_continuous;
    qemu_uae_void_function ppc_cpu_stop;
    qemu_uae_ppc_pause_function ppc_cpu_pause;
    qemu_uae_ppc_check_state_function ppc_cpu_check_state;
    qemu_uae_version_function ppc_cpu_version;
    qemu_uae_version_function qemu_uae_version;
    qemu_uae_bool_void_function qemu_uae_ppc_in_cpu_thread;
    qemu_uae_external_interrupt_function qemu_uae_ppc_external_interrupt;
    qemu_uae_bool_void_function qemu_uae_main_loop_should_exit;
    qemu_uae_lock_function qemu_uae_lock;
    qemu_uae_void_function qemu_uae_mutex_lock;
    qemu_uae_void_function qemu_uae_mutex_unlock;

    qemu_uae_io_mem_read_function *uae_ppc_io_mem_read;
    qemu_uae_io_mem_write_function *uae_ppc_io_mem_write;
    qemu_uae_io_mem_read64_function *uae_ppc_io_mem_read64;
    qemu_uae_io_mem_write64_function *uae_ppc_io_mem_write64;
    qemu_uae_log_function *uae_log;
} qemu_uae_loader;

void qemu_uae_loader_init(qemu_uae_loader *loader);
const char *qemu_uae_loader_error(const qemu_uae_loader *loader);
bool qemu_uae_loader_open(qemu_uae_loader *loader, const char *so_path);
void qemu_uae_loader_close(qemu_uae_loader *loader);
bool qemu_uae_loader_runtime_init(qemu_uae_loader *loader);
bool qemu_uae_loader_runtime_start_and_wait(qemu_uae_loader *loader);
bool qemu_uae_loader_runtime_start(qemu_uae_loader *loader);
bool qemu_uae_loader_ppc_init(qemu_uae_loader *loader, const char *model, uint32_t hid1);
bool qemu_uae_loader_install_io_callbacks(
    qemu_uae_loader *loader,
    qemu_uae_io_mem_read_function io_read,
    qemu_uae_io_mem_write_function io_write,
    qemu_uae_io_mem_read64_function io_read64,
    qemu_uae_io_mem_write64_function io_write64);
bool qemu_uae_loader_install_log_callback(qemu_uae_loader *loader, qemu_uae_log_function log_fn);
bool qemu_uae_loader_warn_if_bad_ld_library_path(FILE *stream);

#endif
