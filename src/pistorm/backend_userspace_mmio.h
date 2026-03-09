// SPDX-License-Identifier: MIT

#ifndef PISTORM_BACKEND_USERSPACE_MMIO_H
#define PISTORM_BACKEND_USERSPACE_MMIO_H

#include <stdint.h>

/*
 * userspace-mmio backend notes:
 * - Uses /dev/mem and direct GPIO/CPRMAN MMIO.
 * - By default this backend refuses to start if pistorm.ko is loaded.
 * - Selection is via config command: pistorm userspace
 */

int ps_userspace_mmio_set_gpclk_src(uint32_t src);
int ps_userspace_mmio_set_gpclk_div(uint32_t div);
int ps_userspace_mmio_set_wr_stretch(uint32_t count);
int ps_userspace_mmio_set_rd_stretch(uint32_t count);
int ps_userspace_mmio_set_lwpair(uint32_t enabled);
int ps_userspace_mmio_set_r32pair(uint32_t enabled);
int ps_userspace_mmio_set_ramseq(uint32_t enabled);
int ps_userspace_mmio_set_wpipe(uint32_t enabled);

#endif
