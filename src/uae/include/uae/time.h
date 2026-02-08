#ifndef UAE_TIME_H
#define UAE_TIME_H

#include "uae/types.h"
#include <time.h>

/* frame_time_t is often cast to int in the code so we use int for now... */
typedef uae_s64 uae_time_t;

void uae_time_init(void);
void uae_time_calibrate(void);
uae_time_t uae_time(void);

#ifdef _WIN32
void uae_time_use_rdtsc(bool enable);
uae_s64 read_system_time(void);
uae_s64 read_processor_time_rdtsc(void);
#endif

typedef uae_time_t frame_time_t;

extern int64_t g_uae_epoch;

#include <sys/time.h>

/* Returns elapsed time in microseconds since start of emulator. */
static inline frame_time_t read_processor_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((frame_time_t)tv.tv_sec * 1000000LL + (frame_time_t)tv.tv_usec) - g_uae_epoch;
}

extern frame_time_t syncbase, cputimebase;

#endif /* UAE_TIME_H */
