# PiStorm Batching and Rate Limiting Features

## Overview
This update introduces optional performance improvements to reduce the "ioctl storm" problem in PiStorm:
1. **Batching**: Groups multiple bus operations into single ioctl calls
2. **IPL Rate Limiting**: Reduces frequency of GPIO/status polling in the IPL thread

Both features are **disabled by default** to ensure system stability and proper timing.

## Safety First Approach
The features are implemented with compile-time toggles to ensure:
- Default behavior remains unchanged and stable
- Timing-sensitive operations continue to work correctly
- Green screen/boot issues are avoided

## Enabling Features

### Enable Batching Only
```bash
make PISTORM_ENABLE_BATCH=1 PISTORM_IPL_RATELIMIT_US=0
```

### Enable IPL Rate Limiting Only
```bash
make PISTORM_ENABLE_BATCH=0 PISTORM_IPL_RATELIMIT_US=100  # 100 microseconds interval
```

### Enable Both (Use with caution!)
```bash
make PISTORM_ENABLE_BATCH=1 PISTORM_IPL_RATELIMIT_US=100
```

### Using with sudo install
```bash
sudo make PISTORM_ENABLE_BATCH=1 PISTORM_IPL_RATELIMIT_US=100 PISTORM_KMOD=1 install
```

## Recommended Testing Sequence

1. **Start with defaults** (both features disabled) to ensure stable boot
2. **Enable batching only** if system boots reliably
3. **Add rate limiting** only if batching alone doesn't cause issues
4. **Adjust rate limit** from conservative values (100us) to more aggressive if needed

## Important Notes

- **Timing Matters**: PiStorm's timing requirements are strict; changes can affect boot reliability
- **Green Screen**: Indicates timing issues; disable features if this occurs
- **Flush Before Reads**: When batching is enabled, writes are flushed before reads to maintain ordering
- **Thread Safety**: Batching queue is designed to be thread-safe for single-threaded access

## Performance Measurement
See PERF.md for correct profiling methodology that doesn't interfere with measurements.