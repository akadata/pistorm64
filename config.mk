# Centralized configuration for PiStorm64 project

# Build defaults (override on make command line or in local config)
EXENAME ?= emulator
PLATFORM ?= PI4_64BIT

# Kernel backend is default; legacy userspace GPIO is optional
PISTORM_KMOD ?= 1

# Batching and IPL rate limiting defaults
PISTORM_ENABLE_BATCH ?= 1
PISTORM_IPL_RATELIMIT_US ?= 100
PISTORM_USE_DIRECT_OPS ?= 0

# RTG defaults
RTG_GFX_MEM ?= 64
RTG_WIDTH ?= 1280
RTG_HEIGHT ?= 720

# Additional configuration options can be added here as needed
