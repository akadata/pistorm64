// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int uae_pistorm_init(int cpu_model, int enable_jit, int enable_fpu);
void uae_pistorm_run(void);
void uae_pistorm_set_irq(int level);
void uae_pistorm_pulse_reset(void);
uint32_t uae_pistorm_get_pc(void);

#ifdef __cplusplus
}
#endif
