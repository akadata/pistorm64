AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc

CPU ?= 020

ifeq ($(CPU),020)
TARGET = rtg64.card
CPUFLAG = -m68020
endif
ifeq ($(CPU),030)
TARGET = rtg64.card
CPUFLAG = -m68030
endif
ifeq ($(CPU),040)
TARGET = rtg64.card
CPUFLAG = -m68040
endif
ifeq ($(CPU),060)
TARGET = rtg64.card
CPUFLAG = -m68060
endif

INSTALL_DIR ?= /opt/pistorm64/data/a314-shared/drivers/rtg64
SRC ?= rtg64-card.c
CFLAGS ?= $(CPUFLAG) -O2 -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostdlib -nostartfiles
AMIGA_NDK_C_INCLUDE ?=
P96DEV_INCLUDE ?=
P96_MONITOR_TEMPLATE ?= /opt/pistorm64/data/a314-shared/Picasso96Install/Devs/Monitors/Picasso96
PIGFX_INFO_TEMPLATE ?= $(CURDIR)/../../rtg/rtg_driver_amiga/PiGFX.info

ifeq ($(strip $(AMIGA_NDK_C_INCLUDE)),)
ifneq ($(wildcard $(AMIGA_TOOLCHAIN)/m68k-amigaos/ndk-include),)
AMIGA_NDK_C_INCLUDE := $(AMIGA_TOOLCHAIN)/m68k-amigaos/ndk-include
else ifneq ($(wildcard $(AMIGA_TOOLCHAIN)/ndk/Include_H),)
AMIGA_NDK_C_INCLUDE := $(AMIGA_TOOLCHAIN)/ndk/Include_H
else ifneq ($(wildcard $(AMIGA_TOOLCHAIN)/ndk/include_h),)
AMIGA_NDK_C_INCLUDE := $(AMIGA_TOOLCHAIN)/ndk/include_h
endif
endif

ifneq ($(strip $(AMIGA_NDK_C_INCLUDE)),)
CFLAGS += -I$(AMIGA_NDK_C_INCLUDE)
endif

ifeq ($(strip $(P96DEV_INCLUDE)),)
ifneq ($(wildcard $(CURDIR)/../../../../../amiga.dev/Picasso96Develop/Include/libraries/Picasso96.h),)
P96DEV_INCLUDE := $(CURDIR)/../../../../../amiga.dev/Picasso96Develop/Include
endif
endif

ifneq ($(strip $(P96DEV_INCLUDE)),)
CFLAGS += -I$(P96DEV_INCLUDE)
endif

# Keep local fallback headers last (used only if external NDK/P96 headers are missing).
CFLAGS += -I$(CURDIR)

all: $(TARGET)

$(TARGET): $(SRC) boardinfo64.h
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(CFLAGS) -o $@ $<

install: $(TARGET) rtg64.info rtg64.monitor
	mkdir -p "$(INSTALL_DIR)"
	cp -a $(TARGET) "$(INSTALL_DIR)/"
	if [ -f "$(PIGFX_INFO_TEMPLATE)" ]; then \
		cp -a "$(PIGFX_INFO_TEMPLATE)" "$(INSTALL_DIR)/rtg64.info"; \
		perl -0777 -i -pe 's/BOARDTYPE=pigfx020/BOARDTYPE=rtg64\x00\x00\x00/g' "$(INSTALL_DIR)/rtg64.info"; \
	else \
		cp -a rtg64.info "$(INSTALL_DIR)/"; \
	fi
	if [ -f "$(P96_MONITOR_TEMPLATE)" ]; then \
		cp -a "$(P96_MONITOR_TEMPLATE)" "$(INSTALL_DIR)/rtg64.monitor"; \
	else \
		cp -a rtg64.monitor "$(INSTALL_DIR)/"; \
	fi
	cp -a README.md "$(INSTALL_DIR)/"

clean:
	rm -f rtg64.card
