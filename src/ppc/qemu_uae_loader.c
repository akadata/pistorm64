#define _GNU_SOURCE

#include "qemu_uae_loader.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void qemu_uae_loader_set_error(qemu_uae_loader *loader, const char *fmt, ...)
{
    va_list args;

    if (loader == NULL) {
        return;
    }
    va_start(args, fmt);
    (void)vsnprintf(loader->error, sizeof(loader->error), fmt, args);
    va_end(args);
}

static void *qemu_uae_loader_required_symbol(qemu_uae_loader *loader, const char *name)
{
    void *symbol;
    const char *error_text;

    if ((loader == NULL) || (loader->handle == NULL)) {
        return NULL;
    }
    dlerror();
    symbol = dlsym(loader->handle, name);
    error_text = dlerror();
    if (error_text != NULL) {
        qemu_uae_loader_set_error(loader, "dlsym(%s) failed: %s", name, error_text);
        return NULL;
    }
    return symbol;
}

static void *qemu_uae_loader_optional_symbol(qemu_uae_loader *loader, const char *name)
{
    if ((loader == NULL) || (loader->handle == NULL)) {
        return NULL;
    }
    dlerror();
    return dlsym(loader->handle, name);
}

#define QEMU_UAE_BIND_REQUIRED_FN(loader, field, symbol_name, field_type) \
    do { \
        union { \
            void *obj; \
            field_type fn; \
        } conversion; \
        conversion.obj = qemu_uae_loader_required_symbol((loader), (symbol_name)); \
        if (conversion.obj == NULL) { \
            return false; \
        } \
        (loader)->field = conversion.fn; \
    } while (0)

#define QEMU_UAE_BIND_OPTIONAL_FN(loader, field, symbol_name, field_type) \
    do { \
        union { \
            void *obj; \
            field_type fn; \
        } conversion; \
        conversion.obj = qemu_uae_loader_optional_symbol((loader), (symbol_name)); \
        (loader)->field = conversion.fn; \
    } while (0)

#define QEMU_UAE_BIND_REQUIRED_OBJ(loader, field, symbol_name, obj_type) \
    do { \
        void *symbol_obj; \
        symbol_obj = qemu_uae_loader_required_symbol((loader), (symbol_name)); \
        if (symbol_obj == NULL) { \
            return false; \
        } \
        (loader)->field = (obj_type)symbol_obj; \
    } while (0)

void qemu_uae_loader_init(qemu_uae_loader *loader)
{
    if (loader == NULL) {
        return;
    }
    memset(loader, 0, sizeof(*loader));
}

const char *qemu_uae_loader_error(const qemu_uae_loader *loader)
{
    if (loader == NULL) {
        return "qemu_uae_loader: loader is NULL";
    }
    if (loader->error[0] == '\0') {
        return "qemu_uae_loader: no error";
    }
    return loader->error;
}

bool qemu_uae_loader_open(qemu_uae_loader *loader, const char *so_path)
{
    if (loader == NULL) {
        return false;
    }
    if ((so_path == NULL) || (so_path[0] == '\0')) {
        so_path = "/usr/local/lib/qemu-uae.so";
    }

    qemu_uae_loader_set_error(loader, "%s", "");
    loader->loaded_path = so_path;
    loader->runtime_started = false;

    loader->handle = dlopen(so_path, RTLD_NOW | RTLD_GLOBAL);
    if (loader->handle == NULL) {
        qemu_uae_loader_set_error(loader, "dlopen(%s) failed: %s", so_path, dlerror());
        return false;
    }

    QEMU_UAE_BIND_REQUIRED_FN(loader, qemu_uae_init, "qemu_uae_init", qemu_uae_void_function);
    QEMU_UAE_BIND_REQUIRED_FN(loader, qemu_uae_start, "qemu_uae_start", qemu_uae_void_function);
    QEMU_UAE_BIND_REQUIRED_FN(loader, qemu_uae_wait_until_started,
                              "qemu_uae_wait_until_started", qemu_uae_void_function);
    QEMU_UAE_BIND_REQUIRED_FN(loader, qemu_uae_ppc_init, "qemu_uae_ppc_init",
                              qemu_uae_ppc_init_function);
    QEMU_UAE_BIND_REQUIRED_FN(loader, ppc_cpu_init, "ppc_cpu_init",
                              qemu_uae_ppc_cpu_init_function);
    QEMU_UAE_BIND_REQUIRED_FN(loader, ppc_cpu_map_memory, "ppc_cpu_map_memory",
                              qemu_uae_ppc_map_memory_function);
    QEMU_UAE_BIND_REQUIRED_FN(loader, ppc_cpu_reset, "ppc_cpu_reset", qemu_uae_void_function);
    QEMU_UAE_BIND_REQUIRED_FN(loader, ppc_cpu_set_state, "ppc_cpu_set_state",
                              qemu_uae_ppc_set_state_function);

    QEMU_UAE_BIND_OPTIONAL_FN(loader, ppc_cpu_set_pc, "ppc_cpu_set_pc",
                              qemu_uae_ppc_set_pc_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, ppc_cpu_run_single, "ppc_cpu_run_single",
                              qemu_uae_ppc_run_single_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, ppc_cpu_run_continuous,
                              "ppc_cpu_run_continuous", qemu_uae_void_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, ppc_cpu_stop, "ppc_cpu_stop", qemu_uae_void_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, ppc_cpu_pause, "ppc_cpu_pause",
                              qemu_uae_ppc_pause_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, ppc_cpu_check_state, "ppc_cpu_check_state",
                              qemu_uae_ppc_check_state_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, ppc_cpu_version, "ppc_cpu_version",
                              qemu_uae_version_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, qemu_uae_version, "qemu_uae_version",
                              qemu_uae_version_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, qemu_uae_ppc_in_cpu_thread, "qemu_uae_ppc_in_cpu_thread",
                              qemu_uae_bool_void_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, qemu_uae_ppc_external_interrupt,
                              "qemu_uae_ppc_external_interrupt",
                              qemu_uae_external_interrupt_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, qemu_uae_main_loop_should_exit,
                              "qemu_uae_main_loop_should_exit",
                              qemu_uae_bool_void_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, qemu_uae_lock, "qemu_uae_lock",
                              qemu_uae_lock_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, qemu_uae_mutex_lock, "qemu_uae_mutex_lock",
                              qemu_uae_void_function);
    QEMU_UAE_BIND_OPTIONAL_FN(loader, qemu_uae_mutex_unlock, "qemu_uae_mutex_unlock",
                              qemu_uae_void_function);

    QEMU_UAE_BIND_REQUIRED_OBJ(loader, uae_ppc_io_mem_read, "uae_ppc_io_mem_read",
                               qemu_uae_io_mem_read_function *);
    QEMU_UAE_BIND_REQUIRED_OBJ(loader, uae_ppc_io_mem_write, "uae_ppc_io_mem_write",
                               qemu_uae_io_mem_write_function *);
    QEMU_UAE_BIND_REQUIRED_OBJ(loader, uae_ppc_io_mem_read64, "uae_ppc_io_mem_read64",
                               qemu_uae_io_mem_read64_function *);
    QEMU_UAE_BIND_REQUIRED_OBJ(loader, uae_ppc_io_mem_write64, "uae_ppc_io_mem_write64",
                               qemu_uae_io_mem_write64_function *);
    QEMU_UAE_BIND_REQUIRED_OBJ(loader, uae_log, "uae_log", qemu_uae_log_function *);

    return true;
}

void qemu_uae_loader_close(qemu_uae_loader *loader)
{
    if (loader == NULL) {
        return;
    }
    if ((loader->handle != NULL) && (loader->runtime_started == false)) {
        (void)dlclose(loader->handle);
    }
    qemu_uae_loader_init(loader);
}

bool qemu_uae_loader_runtime_start(qemu_uae_loader *loader)
{
    if (loader == NULL) {
        return false;
    }
    if ((loader->qemu_uae_init == NULL) || (loader->qemu_uae_start == NULL)
        || (loader->qemu_uae_wait_until_started == NULL)) {
        qemu_uae_loader_set_error(loader, "runtime functions not resolved");
        return false;
    }

    if (qemu_uae_loader_runtime_init(loader) == false) {
        return false;
    }
    if (qemu_uae_loader_runtime_start_and_wait(loader) == false) {
        return false;
    }

    return true;
}

bool qemu_uae_loader_runtime_init(qemu_uae_loader *loader)
{
    if (loader == NULL) {
        return false;
    }
    if (loader->qemu_uae_init == NULL) {
        qemu_uae_loader_set_error(loader, "qemu_uae_init not resolved");
        return false;
    }
    loader->qemu_uae_init();
    return true;
}

bool qemu_uae_loader_runtime_start_and_wait(qemu_uae_loader *loader)
{
    if (loader == NULL) {
        return false;
    }
    if ((loader->qemu_uae_start == NULL) || (loader->qemu_uae_wait_until_started == NULL)) {
        qemu_uae_loader_set_error(loader, "qemu_uae_start/wait symbols not resolved");
        return false;
    }
    loader->qemu_uae_start();
    if ((loader->qemu_uae_mutex_lock != NULL) && (loader->qemu_uae_mutex_unlock != NULL)) {
        loader->qemu_uae_mutex_lock();
        loader->qemu_uae_wait_until_started();
        loader->qemu_uae_mutex_unlock();
    } else {
        loader->qemu_uae_wait_until_started();
    }
    loader->runtime_started = true;
    return true;
}

bool qemu_uae_loader_ppc_init(qemu_uae_loader *loader, const char *model, uint32_t hid1)
{
    if (loader == NULL) {
        return false;
    }
    if ((model == NULL) || (model[0] == '\0')) {
        model = "603e";
    }
    if (loader->qemu_uae_ppc_init != NULL) {
        if (loader->qemu_uae_ppc_init(model, hid1)) {
            return true;
        }
    }
    if (loader->ppc_cpu_init != NULL) {
        if (loader->ppc_cpu_init(model, hid1)) {
            return true;
        }
    }

    qemu_uae_loader_set_error(loader, "PPC init failed for model '%s' hid1=0x%08x",
                              model, hid1);
    return false;
}

bool qemu_uae_loader_install_io_callbacks(
    qemu_uae_loader *loader,
    qemu_uae_io_mem_read_function io_read,
    qemu_uae_io_mem_write_function io_write,
    qemu_uae_io_mem_read64_function io_read64,
    qemu_uae_io_mem_write64_function io_write64)
{
    if (loader == NULL) {
        return false;
    }
    if ((loader->uae_ppc_io_mem_read == NULL)
        || (loader->uae_ppc_io_mem_write == NULL)
        || (loader->uae_ppc_io_mem_read64 == NULL)
        || (loader->uae_ppc_io_mem_write64 == NULL)) {
        qemu_uae_loader_set_error(loader, "IO callback storage symbols not resolved");
        return false;
    }

    *(loader->uae_ppc_io_mem_read) = io_read;
    *(loader->uae_ppc_io_mem_write) = io_write;
    *(loader->uae_ppc_io_mem_read64) = io_read64;
    *(loader->uae_ppc_io_mem_write64) = io_write64;
    return true;
}

bool qemu_uae_loader_install_log_callback(qemu_uae_loader *loader, qemu_uae_log_function log_fn)
{
    if (loader == NULL) {
        return false;
    }
    if (loader->uae_log == NULL) {
        qemu_uae_loader_set_error(loader, "uae_log storage symbol not resolved");
        return false;
    }
    *(loader->uae_log) = log_fn;
    return true;
}

bool qemu_uae_loader_warn_if_bad_ld_library_path(FILE *stream)
{
    const char *ld_path;
    bool has_python2_path;

    ld_path = getenv("LD_LIBRARY_PATH");
    if (ld_path == NULL) {
        return false;
    }

    has_python2_path = false;
    if (strstr(ld_path, "python2.7") != NULL) {
        has_python2_path = true;
    }
    if (strstr(ld_path, "Python-2.7.18") != NULL) {
        has_python2_path = true;
    }

    if ((has_python2_path == true) && (stream != NULL)) {
        fprintf(stream,
                "[PPC] WARNING: LD_LIBRARY_PATH contains python2 paths.\n"
                "[PPC] WARNING: unset LD_LIBRARY_PATH before running this harness.\n"
                "[PPC] WARNING: current LD_LIBRARY_PATH='%s'\n",
                ld_path);
    }

    return has_python2_path;
}
