// SPDX-License-Identifier: MIT
#pragma once
#include "m68kcpu.h"

// Backend wrappers: Musashi remains the default; JIT is experimental.
void musashi_backend_execute(m68ki_cpu_core* state, int cycles);
void musashi_backend_set_irq(int level);

// JIT backend stubs (currently delegate to Musashi); replace when JIT is added.
void jit_backend_execute(m68ki_cpu_core* state, int cycles);
void jit_backend_set_irq(int level);
