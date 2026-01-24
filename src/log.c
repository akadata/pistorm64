// SPDX-License-Identifier: MIT

#include "log.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static int log_level = LOG_LEVEL_INFO;
static FILE* log_fp = NULL;

static const char* log_level_name(int level) {
  switch (level) {
  case LOG_LEVEL_ERROR:
    return "ERROR";
  case LOG_LEVEL_WARN:
    return "WARN";
  case LOG_LEVEL_INFO:
    return "INFO";
  case LOG_LEVEL_DEBUG:
    return "DEBUG";
  default:
    return "UNKNOWN";
  }
}

void log_set_level(int level) {
  if (level < LOG_LEVEL_ERROR) {
    log_level = LOG_LEVEL_ERROR;
    return;
  }
  if (level > LOG_LEVEL_DEBUG) {
    log_level = LOG_LEVEL_DEBUG;
    return;
  }
  log_level = level;
}

static int log_level_from_string(const char* level) {
  if (!level || !level[0]) {
    return -1;
  }
  if (strcasecmp(level, "error") == 0) {
    return LOG_LEVEL_ERROR;
  }
  if (strcasecmp(level, "warn") == 0) {
    return LOG_LEVEL_WARN;
  }
  if (strcasecmp(level, "warning") == 0) {
    return LOG_LEVEL_WARN;
  }
  if (strcasecmp(level, "info") == 0) {
    return LOG_LEVEL_INFO;
  }
  if (strcasecmp(level, "debug") == 0) {
    return LOG_LEVEL_DEBUG;
  }
  return -1;
}

int log_parse_level(const char* level) {
  return log_level_from_string(level);
}

int log_set_file(const char* path) {
  if (!path || !path[0]) {
    if (log_fp) {
      fclose(log_fp);
      log_fp = NULL;
    }
    return 0;
  }

  FILE* fp = fopen(path, "a");
  if (!fp) {
    return -1;
  }
  if (log_fp) {
    fclose(log_fp);
  }
  log_fp = fp;
  setvbuf(log_fp, NULL, _IOLBF, 0);
  return 0;
}

void log_message(int level, const char* fmt, ...) {
  if (level > log_level) {
    return;
  }

  va_list args;
  va_start(args, fmt);
  fprintf(stdout, "[%s] ", log_level_name(level));
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
  vfprintf(stdout, fmt, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  va_end(args);
  fflush(stdout);

  if (log_fp) {
    va_start(args, fmt);
    fprintf(log_fp, "[%s] ", log_level_name(level));
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
    vfprintf(log_fp, fmt, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    va_end(args);
    fflush(log_fp);
  }
}
