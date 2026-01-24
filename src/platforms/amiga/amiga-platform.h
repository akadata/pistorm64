// SPDX-License-Identifier: MIT

#ifndef AMIGA_PLATFORM_H
#define AMIGA_PLATFORM_H

#include "config_file/config_file.h"

// Function prototypes for amiga-platform.c
int setup_platform_amiga(struct emulator_config* cfg);
void handle_reset_amiga(struct emulator_config* cfg);
void shutdown_platform_amiga(struct emulator_config* cfg);
void create_platform_amiga(struct platform_config* cfg, const char* subsys);
void adjust_ranges_amiga(struct emulator_config* cfg);
void setvar_amiga(struct emulator_config* cfg, const char* var, const char* val);

#endif // AMIGA_PLATFORM_H
