// SPDX-License-Identifier: MIT

#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>

static enum log_level log_level = LOG_LEVEL_INFO;
static FILE *log_fp = NULL;
static int log_syslog_enabled_flag = 0;

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static inline const char *log_level_name(enum log_level level) {
    static const char *const names[] = {
        [LOG_LEVEL_ERROR]   = "ERROR",
        [LOG_LEVEL_WARN]    = "WARN",
        [LOG_LEVEL_INFO]    = "INFO",
        [LOG_LEVEL_DEBUG]   = "DEBUG",
        [LOG_LEVEL_VERBOSE] = "VERBOSE",
    };

    unsigned u = (unsigned)level;
    return (u < (unsigned)ARRAY_LEN(names) && names[u]) ? names[u] : "UNKNOWN";
}

static inline int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void log_set_level(enum log_level level) {
    log_level = (enum log_level)clamp_int((int)level, (int)LOG_LEVEL_ERROR, (int)LOG_LEVEL_VERBOSE);
}

enum log_level log_get_level(void) {
    return log_level;
}

struct level_map {
    const char *name;
    enum log_level level;
};

static int log_level_from_string_int(const char *s) {
    static const struct level_map map[] = {
        {"error",   LOG_LEVEL_ERROR},
        {"warn",    LOG_LEVEL_WARN},
        {"warning", LOG_LEVEL_WARN},
        {"info",    LOG_LEVEL_INFO},
        {"debug",   LOG_LEVEL_DEBUG},
        {"verbose", LOG_LEVEL_VERBOSE},
    };

    if (!s || !*s)
        return -1;

    for (unsigned i = 0; i < (unsigned)ARRAY_LEN(map); i++) {
        if (strcasecmp(s, map[i].name) == 0)
            return (int)map[i].level;
    }

    return -1;
}

enum log_level log_parse_level(const char *level) {
    /* Returns (enum log_level)-1 for unknown levels. */
    return (enum log_level)log_level_from_string_int(level);
}

int log_set_file(const char *path) {
    if (!path || !path[0]) {
        if (log_fp) {
            fclose(log_fp);
            log_fp = NULL;
        }
        return 0;
    }

    FILE *fp = fopen(path, "a");
    if (!fp)
        return -1;

    if (log_fp)
        fclose(log_fp);

    log_fp = fp;
    setvbuf(log_fp, NULL, _IOLBF, 0);
    return 0;
}

static inline int log_syslog_priority(enum log_level level) {
    static const int pri[] = {
        [LOG_LEVEL_ERROR]   = LOG_ERR,
        [LOG_LEVEL_WARN]    = LOG_WARNING,
        [LOG_LEVEL_INFO]    = LOG_INFO,
        [LOG_LEVEL_DEBUG]   = LOG_DEBUG,
        [LOG_LEVEL_VERBOSE] = LOG_DEBUG,
    };

    unsigned u = (unsigned)level;
    return (u < (unsigned)ARRAY_LEN(pri) && pri[u]) ? pri[u] : LOG_NOTICE;
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

void log_message(enum log_level level, const char *fmt, ...) {
    if ((int)level > (int)log_level)
        return;

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
