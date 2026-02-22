AMIGA_TOOLCHAIN ?= /opt/amiga
AMIGA_PATH := $(AMIGA_TOOLCHAIN)/bin:$(PATH)

M68K_GCC ?= m68k-amigaos-gcc
VASM ?= vasmm68k_mot
VBCC ?=
AHI_INC ?=
AHI_SDK ?=
AHI_ASM_INC ?=

# Auto-detect AHI SDK headers when not explicitly provided.
AHI_INC_CANDIDATES := \
	$(CURDIR)/m68k-amigaos-ahi/Developer/Include/C \
	$(AMIGA_TOOLCHAIN)/src/m68k-amigaos-gcc/build-Linux-m68k-amigaos/vbcc_target_m68k-amigaos/targets/m68k-amigaos/include \
	$(AMIGA_TOOLCHAIN)/m68k-amigaos/include

ifeq ($(strip $(AHI_INC)),)
AHI_INC := $(firstword $(foreach d,$(AHI_INC_CANDIDATES),$(if $(wildcard $(d)/proto/ahi_sub.h),$(d),)))
endif

ifeq ($(strip $(AHI_ASM_INC)),)
ifneq ($(wildcard $(CURDIR)/m68k-amigaos-ahi/Developer/Include/Asm/devices/ahi.i),)
AHI_ASM_INC := $(CURDIR)/m68k-amigaos-ahi/Developer/Include/Asm
endif
endif

CPU ?= 020

ifeq ($(CPU),000)
DEVICE_TARGET = PiAHI000.audio
CPUFLAG = -m68000
endif
ifeq ($(CPU),010)
DEVICE_TARGET = PiAHI010.audio
CPUFLAG = -m68010
endif
ifeq ($(CPU),020)
DEVICE_TARGET = PiAHI020.audio
CPUFLAG = -m68020
endif
ifeq ($(CPU),030)
DEVICE_TARGET = PiAHI030.audio
CPUFLAG = -m68030
endif
ifeq ($(CPU),040)
DEVICE_TARGET = PiAHI040.audio
CPUFLAG = -m68040
endif
ifeq ($(CPU),060)
DEVICE_TARGET = PiAHI060.audio
CPUFLAG = -m68060
endif

VASM_INCLUDES :=
ifneq ($(VBCC),)
VASM_INCLUDES := -I$(VBCC)/NDK39/include/include_i
endif
ifneq ($(wildcard $(AMIGA_TOOLCHAIN)/m68k-amigaos/ndk-include/exec/types.i),)
VASM_INCLUDES += -I$(AMIGA_TOOLCHAIN)/m68k-amigaos/ndk-include
endif
ifneq ($(AMIGA_HEADERS),)
VASM_INCLUDES += -I$(AMIGA_HEADERS)
endif
ifneq ($(AHI_ASM_INC),)
VASM_INCLUDES += -I$(AHI_ASM_INC)
endif

VASM_CPUFLAG ?= -m68020
ifeq ($(CPU),000)
VASM_CPUFLAG = -m68000
endif
ifeq ($(CPU),010)
VASM_CPUFLAG = -m68010
endif
ifeq ($(CPU),020)
VASM_CPUFLAG = -m68020
endif
ifeq ($(CPU),030)
VASM_CPUFLAG = -m68030
endif
ifeq ($(CPU),040)
VASM_CPUFLAG = -m68040
endif
ifeq ($(CPU),060)
VASM_CPUFLAG = -m68060
endif

PREFS_TARGET ?= PI-AHI
LOCAL_PREFS_SRC := $(CURDIR)/m68k-amigaos-ahi/Developer/Drivers/Paula/prefsfile.a
PREFS_SRC ?= $(if $(wildcard $(LOCAL_PREFS_SRC)),$(LOCAL_PREFS_SRC),prefsfile.a)

DEVICE_SRC ?= pi-ahi.c

DEVICE_CFLAGS ?= $(CPUFLAG) -O2 -Wall -Wextra -Wno-unused-parameter -nostartfiles
ifneq ($(AHI_SDK),)
DEVICE_CFLAGS += -I$(AHI_SDK)/Include
endif
ifneq ($(AHI_INC),)
DEVICE_CFLAGS += -I$(AHI_INC)
endif

AHI_SUB_HEADER := $(if $(AHI_INC),$(AHI_INC)/proto/ahi_sub.h,)
AHI_INLINE_HEADERS := $(if $(AHI_INC),$(AHI_INC)/inline/ahi_sub.h $(AHI_INC)/inline/ahi_sub_protos.h,)
AHI_GCC_INLINE_REG_STYLE := $(shell for h in $(AHI_INLINE_HEADERS); do if [ -f "$$h" ] && grep -q "__reg" "$$h" 2>/dev/null; then echo 1; exit 0; fi; done; echo 0)
AHI_FORCE_GCC ?= 0

AHI_DEVICE_BUILDABLE := 1
ifeq ($(AHI_FORCE_GCC),0)
ifneq ($(AHI_GCC_INLINE_REG_STYLE),0)
AHI_DEVICE_BUILDABLE := 0
endif
endif

ALL_TARGETS :=
ifeq ($(AHI_DEVICE_BUILDABLE),1)
ALL_TARGETS += $(DEVICE_TARGET)
else
$(info [ahi_driver] Detected VBCC-style AHI inline headers (__reg) at $(AHI_SUB_HEADER).)
$(info [ahi_driver] Skipping $(DEVICE_TARGET) for GCC; use AHI_FORCE_GCC=1 only if you provide GCC-compatible AHI headers.)
endif
ifneq ($(wildcard $(PREFS_SRC)),)
ALL_TARGETS += $(PREFS_TARGET)
else
$(info [ahi_driver] prefsfile.a not found; skipping $(PREFS_TARGET). Set PREFS_SRC or provide the file to build it.)
endif

all: check-ahi-headers $(ALL_TARGETS)

check-ahi-headers:
	@if [ "$(AHI_DEVICE_BUILDABLE)" != "1" ]; then \
		exit 0; \
	fi
	@if [ ! -f "$(AHI_SUB_HEADER)" ]; then \
		echo "[ahi_driver] Missing proto/ahi_sub.h."; \
		echo "[ahi_driver] Set AHI_INC to the AHI SDK include root."; \
		echo "[ahi_driver] Example:"; \
		echo "  make AHI_INC=$(CURDIR)/m68k-amigaos-ahi/Developer/Include/C"; \
		exit 1; \
	fi

$(PREFS_TARGET): $(PREFS_SRC)
	PATH="$(AMIGA_PATH)" $(VASM) -quiet -phxass -Fhunk $(VASM_CPUFLAG) -o $@ $< $(VASM_INCLUDES)

$(DEVICE_TARGET): $(DEVICE_SRC)
	PATH="$(AMIGA_PATH)" $(M68K_GCC) $(DEVICE_CFLAGS) -o $@ $<

clean:
	rm -f $(PREFS_TARGET) PiAHI*.audio pi-ahi.audio pi-ahi000.audio pi-ahi010.audio pi-ahi020.audio pi-ahi030.audio pi-ahi040.audio pi-ahi060.audio

.PHONY: all clean check-ahi-headers
