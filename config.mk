# Centralized configuration for PiStorm64 project

# Build defaults (override on make command line or in local config)
EXENAME ?= emulator
PLATFORM ?= PI4_64BIT_NATIVE

# Build JIT
USE_UAE_JIT = 1

# Kernel backend is default; legacy userspace GPIO is optional
PISTORM_KMOD = 1
PISTORM_ENABLE_QUEUE ?= 1 
# Batching and IPL rate limiting defaults
PISTORM_ENABLE_BATCH ?= 1
PISTORM_IPL_RATELIMIT_US ?= 133
PISTORM_USE_DIRECT_OPS ?= 1

# RTG defaults
RTG_GFX_MEM ?= 128
RTG_WIDTH ?= 1920 # or 2650 or 3840 or 1280 
RTG_HEIGHT ?= 1080 # or  1440 or 2160 or 720
# there is little point using bellow 1080p this is still 60fps + 
# Additional configuration options can be added here as needed
