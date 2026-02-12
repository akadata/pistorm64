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
void uae_pistorm_overlay_changed(int ovl_state);
int uae_pistorm_get_last_init_error(void);
void uae_pistorm_get_last_reset_vectors(uint32_t* sp, uint32_t* pc, int* ovl_state);
uint32_t uae_pistorm_get_pc(void);
uint32_t uae_pistorm_get_regs_pc(void);
uint32_t uae_pistorm_get_regs_pc_p(void);


#ifdef __cplusplus
}
#endif
