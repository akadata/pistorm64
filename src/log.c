// SPDX-License-Identifier: MIT

#include "log.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>

static int log_level = LOG_LEVEL_INFO;
static FILE* log_fp = NULL;
static int log_syslog_enabled_flag = 0;

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
  case LOG_LEVEL_VERBOSE:
    return "VERBOSE";
  default:
    return "UNKNOWN";
  }
}

void log_set_level(int level) {
  if (level < LOG_LEVEL_ERROR) {
    log_level = LOG_LEVEL_ERROR;
    return;
  }
  if (level > LOG_LEVEL_VERBOSE) {
    log_level = LOG_LEVEL_VERBOSE;
    return;
  }
  log_level = level;
}

int log_get_level(void) {
  return log_level;
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
  if (strcasecmp(level, "verbose") == 0) {
    return LOG_LEVEL_VERBOSE;
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

static int log_syslog_priority(int level) {
  switch (level) {
  case LOG_LEVEL_ERROR:
    return LOG_ERR;
  case LOG_LEVEL_WARN:
    return LOG_WARNING;
  case LOG_LEVEL_INFO:
    return LOG_INFO;
  case LOG_LEVEL_DEBUG:
  case LOG_LEVEL_VERBOSE:
    return LOG_DEBUG;
  default:
    return LOG_NOTICE;
  }
}

int log_set_syslog(int enable) {
  if (enable) {
    if (!log_syslog_enabled_flag) {
      openlog("pistorm64", LOG_PID | LOG_CONS, LOG_USER);
      log_syslog_enabled_flag = 1;
    }
    return 0;
  }
  if (log_syslog_enabled_flag) {
    closelog();
    log_syslog_enabled_flag = 0;
  }
  return 0;
}

int log_syslog_enabled(void) {
  return log_syslog_enabled_flag;
}

void log_message(int level, const char* fmt, ...) {
  if (level > log_level) {
    return;
  }

  char message[2048];
  va_list args;

  va_start(args, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
  vsnprintf(message, sizeof(message), fmt, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  va_end(args);

  if (log_syslog_enabled_flag) {
    syslog(log_syslog_priority(level), "[%s] %s", log_level_name(level), message);
  }

  fprintf(stdout, "[%s] %s", log_level_name(level), message);
  fflush(stdout);

  if (log_fp) {
    fprintf(log_fp, "[%s] %s", log_level_name(level), message);
    fflush(log_fp);
  }
}
