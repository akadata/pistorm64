#ifndef UAE_FRAME_TIME_H
#define UAE_FRAME_TIME_H

#include <stdint.h>
#include <sys/time.h>

typedef uint64_t frame_time_t;

static inline frame_time_t read_processor_time(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    return (frame_time_t)tv.tv_sec * 1000000ull + (frame_time_t)tv.tv_usec;
}

#endif
