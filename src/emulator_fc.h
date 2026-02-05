#ifndef EMULATOR_FC_H
#define EMULATOR_FC_H

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Current Function Code (FC)
 * 0–7 as per 68k spec:
 *  - user/supervisor
 *  - program/data
 *  - CPU space
 */

extern uint32_t current_fc;

enum fc_mode {
  FC_MODE_OFF = 0,
  FC_MODE_STUB,
  FC_MODE_CPLD,
};

void cpu_set_fc(uint32_t fc);
void fc_set_mode(enum fc_mode mode);
enum fc_mode fc_get_mode(void);

// Additional functions for SFC/DFC register handling (following Atari approach)
uint32_t get_current_sfc(void);
uint32_t get_current_dfc(void);

#ifdef __cplusplus
}
#endif
#endif
