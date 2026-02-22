AMIGA_TOOLCHAIN ?= /opt/amiga
M68K_GCC ?= $(AMIGA_TOOLCHAIN)/bin/m68k-amigaos-gcc
VC ?= vc
GUI_COMPILER ?= auto

COMMON_CFLAGS ?= -m68020 -O2 -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -noixemul -DNDEBUG
COMMON_LDFLAGS ?=

PISIMPLE_SRCS := simple_interact.c pistorm_dev.c
COPYMEMS_SRCS := copymems.c pistorm_dev.c
PISTORM_SRCS  := gui_interact.c pistorm_dev.c

REQTOOLS_INC ?= -Ireqtools
REQTOOLS_LIB ?= reqtoolsnb.lib
BUILD_PISTORM_GUI ?= 0
VC_GUI_FLAGS ?= +kick13 -c99 -DNDEBUG -lamiga -lauto $(REQTOOLS_INC)

.PHONY: all clean

all: PiSimple CopyMems
ifeq ($(BUILD_PISTORM_GUI),1)
all: PiStorm
else
	@echo "Skipping PiStorm GUI (set BUILD_PISTORM_GUI=1 to try building it)."
endif

PiSimple: $(PISIMPLE_SRCS)
	$(M68K_GCC) $(COMMON_CFLAGS) $(PISIMPLE_SRCS) $(COMMON_LDFLAGS) -o $@

CopyMems: $(COPYMEMS_SRCS)
	$(M68K_GCC) $(COMMON_CFLAGS) $(COPYMEMS_SRCS) $(COMMON_LDFLAGS) -o $@

PiStorm: $(PISTORM_SRCS) $(REQTOOLS_LIB)
ifeq ($(GUI_COMPILER),vc)
	@command -v $(VC) >/dev/null 2>&1 || { \
		echo "ERROR: GUI_COMPILER=vc selected but '$(VC)' is not installed."; \
		echo "Install VBCC (vc) or set GUI_COMPILER=gcc."; \
		exit 1; \
	}
	$(VC) $(VC_GUI_FLAGS) $(PISTORM_SRCS) $(REQTOOLS_LIB) -o $@
else ifeq ($(GUI_COMPILER),gcc)
	$(M68K_GCC) $(COMMON_CFLAGS) $(REQTOOLS_INC) $(PISTORM_SRCS) $(COMMON_LDFLAGS) -o $@
else
	@command -v $(VC) >/dev/null 2>&1 || { \
		echo "ERROR: PiStorm GUI uses VBCC ReqTools library ($(REQTOOLS_LIB))."; \
		echo "       No '$(VC)' compiler found on this system."; \
		echo "       Install VBCC (vc) and re-run: BUILD_PISTORM_GUI=1 make GUI_COMPILER=vc"; \
		echo "       Or force GCC attempt: BUILD_PISTORM_GUI=1 make GUI_COMPILER=gcc"; \
		exit 1; \
	}
	$(VC) $(VC_GUI_FLAGS) $(PISTORM_SRCS) $(REQTOOLS_LIB) -o $@
endif

clean:
	rm -f PiSimple PiStorm CopyMems *.o
