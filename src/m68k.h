// SPDX-License-Identifier: MIT
#ifndef PISTORM_M68K_WRAPPER_H
#define PISTORM_M68K_WRAPPER_H

/*
 * Compatibility wrapper:
 * keep project includes as #include "m68k.h" while allowing Makefile
 * include-path gating to remove -Isrc/musashi from generic builds.
 */
#include "musashi/m68k.h"

#endif
