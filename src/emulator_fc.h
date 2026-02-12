// SPDX-License-Identifier: MIT
#ifndef EMULATOR_FC_H
#define EMULATOR_FC_H

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 68k Function Code values (3-bit FC, FC2:FC1:FC0)
 *
 * 0: Reserved / undefined
 * 1: User Data
 * 2: User Program
 * 3: Reserved / undefined
 * 4: Reserved / undefined
 * 5: Supervisor Data
 * 6: Supervisor Program
 * 7: CPU Space (interrupt acknowledge, etc.)
 *
 * Musashi typically ORs FLAG_S into the FC value it passes to the callback.
 * This header treats the low 3 bits as the bus FC and keeps the S bit separate.
 */

/* Public latched FC value (full value as given by Musashi callback) */
extern uint32_t current_fc;

/*
 * Backward-compatible FC mode selector.
 *
 * FC_MODE_OFF  : no FC tracking, callback disabled, CPLD pins idle
 * FC_MODE_STUB : FC tracked in software only, no CPLD signalling
 * FC_MODE_CPLD : FC tracked and forwarded to CPLD pins
 */
enum fc_mode {
  FC_MODE_OFF = 0,
  FC_MODE_STUB,
  FC_MODE_CPLD,
};

/* Current runtime mode (optional for code that wants to inspect) */
extern enum fc_mode current_fc_mode;

/*
 * Initialise / shutdown the FC subsystem.
 *
 * fc_init(mode) will install or remove the Musashi FC callback as needed and
 * set the initial operating mode.
 */
void fc_init(enum fc_mode mode);
void fc_shutdown(void);

/*
 * Musashi FC callback entry point.
 *
 * This is what gets registered via m68k_set_fc_callback(). It should:
 *  - latch the new FC value into current_fc
 *  - update any derived cached state (S bit, bus FC, etc.)
 *  - if mode == FC_MODE_CPLD, drive the CPLD FC pins accordingly
 */
void cpu_set_fc(uint32_t fc);

/*
 * Mode control (backward-compatible interface).
 *
 * These mirror the old API so existing code that toggles FC at runtime keeps
 * working as-is. fc_set_mode() should internally update current_fc_mode and,
 * if needed, enable/disable CPLD signalling without disturbing Musashi.
 */
void fc_set_mode(enum fc_mode mode);
enum fc_mode fc_get_mode(void);

/*
 * FC query helpers.
 *
 * get_current_fc_raw()   : low 3 bits on the bus (FC0..FC2)
 * get_current_fc_space() : same as raw, but explicitly typed as "address space"
 * get_current_fc_s_bit() : non-zero if Supervisor (FLAG_S set), zero if User
 * get_current_fc()       : full latched value (for logging / debug)
 */
uint32_t get_current_fc(void);
uint8_t  get_current_fc_raw(void);
uint8_t  get_current_fc_space(void);
uint8_t  get_current_fc_s_bit(void);

/*
 * SFC/DFC helpers (for PMMU / address-space aware accesses).
 *
 * These mirror the Atari side helpers and give you the currently effective
 * Source and Destination Function Codes, if the MMU backend tracks them.
 * For now they can simply return get_current_fc_space() until MMU wiring is
 * fully implemented.
 */
uint32_t get_current_sfc(void);
uint32_t get_current_dfc(void);

#ifdef __cplusplus
}
#endif

#endif /* EMULATOR_FC_H */
