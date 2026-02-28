# PiSCSI remote local dispatcher Makefile.
# Uses a platform-specific file by default.
#
# Override with:
#   make PLATFORM_FILE=Makefile.linux

ifeq ($(OS),Windows_NT)
PLATFORM_FILE ?= Makefile.windows
else
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(UNAME_S),Darwin)
PLATFORM_FILE ?= Makefile.mac
else
PLATFORM_FILE ?= Makefile.linux
endif
endif

all:
	$(MAKE) -f $(PLATFORM_FILE) all

clean:
	$(MAKE) -f $(PLATFORM_FILE) clean

show-platform:
	@echo "Using $(PLATFORM_FILE)"

.PHONY: all clean show-platform
