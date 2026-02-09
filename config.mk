# Tunables: edit here instead of hunting through rule bodies.
# WARNINGS   : compiler warnings; keep strict by default.
# OPT_LEVEL  : optimisation level (-Os/-O2/-O3). Can also set O=2,3,...
# USE_GOLD   : set to 1 to prefer gold linker (if installed).
# USE_RAYLIB : set to 0 to drop raylib/DRM deps and use a null RTG backend.
# USE_ALSA   : set to 0 to drop ALSA/ahi builds and -lasound.
# USE_PMMU   : set to 1 to enable Musashi PMMU support (experimental).
# USE_EC_FPU : set to 1 to force FPU on EC/020/LC/EC040 variants (for 68881/68882 emu).
# ARCH_FEATURES : optional AArch64 feature modifiers (e.g. +crc+simd+fp16+lse).
# CPUFLAGS   : per-platform tuning defaults below; override if needed.
# RAYLIB_*   : raylib include/lib paths; adjust for custom builds.
# USE_VC     : legacy (no longer required for pistorm-dev).
# USE_LTO    : set to 1 to enable link-time optimisation (-flto) on build and link.
# USE_NO_PLT : set to 1 to pass -fno-plt for direct calls (glibc-specific; default off).
# OMIT_FP    : set to 1 to omit frame pointers (-fomit-frame-pointer) for perf.
# USE_PIPE   : set to 1 to add -pipe to compile steps.
# M68K_WARN_SUPPRESS : extra warning suppressions for the generated Musashi core.

# Centralized configuration for PiStorm64 project
# Values here can be overridden on the make command line, for example:
#   make USE_UAE_JIT=1 PLATFORM=PI4_64BIT

# Target binary name and base platform profile
EXENAME ?= emulator
PLATFORM ?= PI4_64BIT

# Rebuild raylib_drm as part of the normal build if RTG is enabled
REBUILD_RAYLIB_DRM ?= 1


# Default build tunables
USE_GOLD   ?= 1
USE_RAYLIB ?= 1
USE_ALSA   ?= 1
USE_PMMU   ?= 1
USE_EC_FPU ?= 0
USE_VC     ?= 0
USE_LTO    ?= 0
USE_NO_PLT ?= 1
OMIT_FP    ?= 1
USE_PIPE   ?= 1
ARCH_FEATURES ?=
CPUFLAGS   ?= -march=armv8-a+crc -mtune=cortex-a53
NO_UNROLL_FLAGS ?= -fno-unroll-loops


# UAE JIT backend (AArch64 UAE CPU core)
# 0 = do not build or link the UAE JIT; always use Musashi
# 1 = build and link UAE JIT backend (runtime can still decide not to use it)
USE_UAE_JIT ?= 0


# Kernel backend for PiStorm (pistorm.ko)
# 0 = build without kernel backend support (legacy userspace GPIO only)
# 1 = build with kernel backend support and expect pistorm.ko
PISTORM_KMOD ?= 1

# Host-side queueing and in-kernel batching for bus transactions
# Queue aggregates operations in userspace before passing them to the kernel
# Batch enables run_batch logic inside pistorm.ko
PISTORM_ENABLE_QUEUE ?= 1
PISTORM_ENABLE_BATCH ?= 1

# Interrupt rate limiting (IPL) in microseconds
# Lower values allow more frequent interrupts; higher values smooth them
PISTORM_IPL_RATELIMIT_US ?= 133

# Direct kmod read/write ops instead of slower ioctl-style paths
# 0 = use legacy ioctl-style operations
# 1 = use direct ops in the kernel backend
PISTORM_USE_DIRECT_OPS ?= 1


# GPCLK configuration feeding the PiStorm CPLD
# Source and divider match the RPi GPCLK settings used by the board
PISTORM_GPCLK_SRC ?= 5
PISTORM_GPCLK_DIV ?= 6

# BERR / reset input handling and batching control for pistorm.ko
# BERR reset:
#   0 = BERR pin not treated as reset input
#   1 = BERR pin wired as reset input into the kernel backend
PISTORM_BERR_RESET ?= 1

# Batch enable:
#   0 = run each bus transaction immediately
#   1 = allow pistorm.ko to batch transactions internally
PISTORM_BATCH_ENABLE ?= 1

# Aggregated kernel module parameters passed to pistorm.ko on load
PISTORM_KMOD_PARAMS ?= run_batch_enable=$(PISTORM_BATCH_ENABLE) berr_reset_input=$(PISTORM_BERR_RESET) gpclk_src=$(PISTORM_GPCLK_SRC) gpclk_div=$(PISTORM_GPCLK_DIV)


# RTG defaults
# RTG_GFX_MEM in MiB; resolution in pixels
RTG_GFX_MEM ?= 128
RTG_WIDTH ?= 1920
RTG_HEIGHT ?= 1080

# Additional configuration options can be added below as needed
