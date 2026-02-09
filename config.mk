# Centralized configuration for PiStorm64 project
# Values here can be overridden on the make command line, for example:
#   make USE_UAE_JIT=1 PLATFORM=PI4_64BIT

# Target binary name and base platform profile
EXENAME ?= emulator
PLATFORM ?= PI4_64BIT


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
