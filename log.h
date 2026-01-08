// SPDX-License-Identifier: MIT

#ifndef PISTORM_LOG_H
#define PISTORM_LOG_H

#include <stdarg.h>

enum log_level {
  LOG_LEVEL_ERROR = 0,
  LOG_LEVEL_WARN = 1,
  LOG_LEVEL_INFO = 2,
  LOG_LEVEL_DEBUG = 3,
};

void log_set_level(int level);
int log_parse_level(const char* level);
int log_set_file(const char* path);
void log_message(int level, const char* fmt, ...);

#define LOG_ERROR(...) log_message(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_WARN(...) log_message(LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_INFO(...) log_message(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DEBUG(...) log_message(LOG_LEVEL_DEBUG, __VA_ARGS__)

#endif
