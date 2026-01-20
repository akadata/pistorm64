# PiStorm Performance Measurement Guide

## Safe Implementation with Compile-Time Toggles

The performance improvements have been implemented with safety in mind, using compile-time toggles to ensure stability:

### Compile-time flags:
- `PISTORM_ENABLE_BATCH=0` (default) - Disables batching to ensure stability
- `PISTORM_IPL_RATELIMIT_US=0` (default) - Disables IPL rate limiting to ensure timing accuracy

### To enable performance features:
```bash
# Enable batching (set to 1) and/or IPL rate limiting (set to interval in microseconds)
make PISTORM_ENABLE_BATCH=1 PISTORM_IPL_RATELIMIT_US=100

# Or with install
sudo make PISTORM_ENABLE_BATCH=1 PISTORM_IPL_RATELIMIT_US=100 PISTORM_KMOD=1 install
```

## Correct Profiling Method (NO syscall tracing)

When measuring performance improvements to reduce the "ioctl storm", it's critical to measure correctly:

### ❌ DO NOT enable syscall tracing during performance tests
- Do NOT use `syscalls:sys_enter_ioctl` tracepoint
- Tracing syscalls changes behavior and adds overhead
- This invalidates performance measurements

### ✅ Correct measurement procedure:
```bash
# Record performance data without syscall tracing
perf record -F 499 -g --call-graph fp -p $(pgrep -n emulator) -- sleep 10

# Analyze results
perf report --stdio
```

## Key Performance Improvements Implemented

### 1. Batched BUSOP Calls (Optional)
- Implemented `PISTORM_IOC_BATCH` to group multiple operations when enabled
- Reduces ioctl overhead significantly when activated
- Queue-based batching in userspace protocol layer
- Operations flushed periodically or before reads that need immediate results

### 2. Reduced IPL/Status Polling Frequency (Optional)
- Rate limiting added to IPL thread when enabled
- Polls GPIO/status at configurable intervals instead of "as fast as possible"
- Uses cached values between polls
- Maintains responsiveness while reducing overhead when activated

### 3. Optimized Operation Flushing
- Automatic flushing at end of CPU timeslices
- Explicit flush points before status reads
- Maintains correct timing and synchronization

## Measuring Success
After enabling these features, you should see:
- Significantly reduced time spent in ioctl path
- Lower overhead in kernel locking (ps_ioctl, mutex, fdget/fput)
- Improved overall emulator performance
- Reduced CPU thrashing