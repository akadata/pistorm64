# AGENTS.md – PiStorm64 PPC Integration Context

## Overview

This repository is integrating QEMU-UAE's PowerPC core into PiStorm64.

Goal:

* Use Musashi for 68k (already in-tree).
* Use QEMU-UAE PPC core (via qemu-uae.so).
* DO NOT use UAE JIT for 68k.
* Final architecture: Musashi (68k) + QEMU PPC.

UAE JIT present in src/ must NOT be used or re-enabled.

---

## System Environment

Host: Raspberry Pi 4 (aarch64)
User: smalley

Important paths:

Python 2.7 (required to build qemu-uae):
/home/smalley/src/Python-2.7.18

QEMU-UAE source tree:
/home/smalley/fs-uae-plugin-qemu-uae/qemu-uae

Built shared object:
/usr/local/lib/qemu-uae.so

Firmware directory (manually populated):
/usr/local/share/qemu

QEMU config directory:
/usr/local/etc/qemu

Minimal target-ppc.conf currently created:
/usr/local/etc/qemu/target-ppc.conf

Runtime hygiene:

* `LD_LIBRARY_PATH` must be unset before running the harness.
* Do not run harnesses with Python 2.7 library paths injected.

---

## Current Status

* qemu-uae.so builds successfully.
* It exports PPC and qemu_uae_* symbols.
* It segfaults or aborts if callbacks/init order are wrong.
* Stable sequence for this build is:

  1. Install `uae_log` + `uae_ppc_io_mem_*` callback pointers.
  2. `qemu_uae_init()`
  3. `qemu_uae_ppc_init(model, hid1)` / `ppc_cpu_init(model, hid1)`
  4. `ppc_cpu_map_memory()`
  5. `ppc_cpu_reset()`
  6. `qemu_uae_start()`
  7. `qemu_uae_mutex_lock()`
  8. `qemu_uae_wait_until_started()`
  9. `qemu_uae_mutex_unlock()`
  10. run control (`ppc_cpu_run_single` if available, else `ppc_cpu_set_state`)

The test harness exists at:

src/ppc/test_ppc_qemuuae.c

Built with:

cc -O2 -g -Wall -Wextra -Wpedantic -std=c11 
-Isrc/ppc -o build/ppc/test_ppc_qemuuae 
src/ppc/test_ppc_qemuuae.c src/ppc/qemu_uae_loader.c -ldl

Run with clean runtime:

unset LD_LIBRARY_PATH
export QEMU_UAE_SO=/usr/local/lib/qemu-uae.so
export PPC_MODEL=603e
export PPC_STEPS=100000
./build/ppc/test_ppc_qemuuae

The harness dynamically loads qemu-uae.so using dlopen.

---

## Immediate Tasks (Codex Priority)

### Task 1: Stabilize Test Harness

Objective:
Create a minimal, stable PPC execution test that:

* dlopen()s qemu-uae.so
* Calls correct qemu_uae runtime init sequence
* Initializes a PPC CPU (e.g., 603e)
* Maps a small RAM buffer
* Sets PC
* Executes a small number of instructions
* Exits cleanly without segfault

Must:

* Avoid static linking against qemu-uae.so
* Use dlopen/dlsym only
* Clean up pedantic function pointer casts

Must NOT:

* Use any UAE 68k JIT
* Modify Musashi core
* Pull in other QEMU subsystems

The test harness is the integration gate.
No pistorm64 core wiring happens until this works.

---

## Architectural Rules

1. Musashi remains the only 68k engine.
2. QEMU-UAE is PPC only.
3. No UAE JIT involvement.
4. No QEMU system emulation beyond what PPC core requires.
5. Keep PPC isolated in src/ppc/ until stable.

---

## Long-Term Goal

When stable:

* Replace any existing PPC emulation in pistorm64 with qemu-uae PPC backend.
* Wire memory mapping to pistorm shared memory region.
* Integrate interrupt bridging.

But NONE of that happens until the standalone test harness is stable.

---

## Notes

* qemu-uae is not intended to be installed via make install.
* Firmware manually copied from pc-bios.
* Runtime init order is critical.
* Crash observed at NULL dereference before proper init.

Codex must treat the harness as the first deliverable milestone.

## entry points for qemu-uae.so

nm -D /usr/local/lib/qemu-uae.so | rg -n " qemu_uae_| ppc_cpu_|uae_ppc_" | sort
3256:000000000062ddf0 D ppc_cpu_aliases
3257:000000000024b5e4 T ppc_cpu_class_by_pvr
3258:000000000024b9a0 T ppc_cpu_class_by_pvr_mask
3259:0000000000255f80 T ppc_cpu_do_interrupt
3260:0000000000256780 T ppc_cpu_do_system_reset
3261:00000000002380d0 T ppc_cpu_dump_state
3262:000000000017d3e0 T ppc_cpu_dump_statistics
3263:0000000000256900 T ppc_cpu_exec_interrupt
3264:0000000000275020 T ppc_cpu_gdb_read_register
3265:0000000000275280 T ppc_cpu_gdb_read_register_apple
3266:00000000002754c0 T ppc_cpu_gdb_write_register
3267:0000000000275710 T ppc_cpu_gdb_write_register_apple
3268:000000000024f570 T ppc_cpu_get_phys_page_debug
3269:000000000039cf28 T ppc_cpu_init
3270:000000000024ba70 T ppc_cpu_list
3271:000000000039d120 T ppc_cpu_map_memory
3272:000000000039d480 T ppc_cpu_reset
3273:000000000039d428 T ppc_cpu_run_continuous
3274:000000000039d508 T ppc_cpu_set_state
3275:000000000039cf24 T ppc_cpu_version
4362:000000000039d6e0 T qemu_uae_init
4363:000000000039d3c0 T qemu_uae_lock
4364:000000000039d860 T qemu_uae_main_loop_should_exit
4365:000000000012dbac T qemu_uae_mutex_lock
4366:000000000012dc50 T qemu_uae_mutex_trylock
4367:000000000012dd20 T qemu_uae_mutex_trylock_cancel
4368:000000000012dc40 T qemu_uae_mutex_unlock
4369:000000000039d3a8 T qemu_uae_ppc_external_interrupt
4370:000000000039d108 T qemu_uae_ppc_in_cpu_thread
4371:000000000039d104 T qemu_uae_ppc_init
4372:000000000039d600 T qemu_uae_set_started
4373:000000000039d880 T qemu_uae_slirp_init
4374:000000000039d900 T qemu_uae_slirp_input
4375:000000000039d7e4 T qemu_uae_start
4376:000000000039d6c4 T qemu_uae_version
4377:000000000039d680 T qemu_uae_wait_until_started
5177:0000000000ae32d8 B uae_ppc_io_mem_read
5178:0000000000ae32c8 B uae_ppc_io_mem_read64
5179:0000000000ae32d0 B uae_ppc_io_mem_write
5180:0000000000ae32c0 B uae_ppc_io_mem_write64


# Test harness location 

src/ppc/test_ppc_qemuuae.c
 
End of AGENTS.md
