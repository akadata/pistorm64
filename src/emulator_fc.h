#ifndef EMULATOR_FC_H
#define EMULATOR_FC_H

#pragma once
#include <stdint.h>

/*
 * Current Function Code (FC)
 * 0–7 as per 68k spec:
 *  - user/supervisor
 *  - program/data
 *  - CPU space
 */

extern uint32_t current_fc;

void cpu_set_fc(uint32_t fc);


#endif
