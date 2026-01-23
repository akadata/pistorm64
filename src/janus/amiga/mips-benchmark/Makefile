AMIGA_PREFIX ?= /opt/amiga
CC := $(AMIGA_PREFIX)/bin/m68k-amigaos-gcc

CFLAGS ?= -m68030 -O2 -noixemul -DHAS_STDLIB -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -lamiga

# Include janus-ipc module
CFLAGS += -I..

TARGET = janus-mips
SOURCES = janus-mips.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $< ../janus-ipc-amiga.c $(LDFLAGS)

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	@echo "Install target would copy $(TARGET) to appropriate Amiga location"

.PHONY: help
help:
	@echo "Targets:"
	@echo "  all     - Build the executable for Amiga"
	@echo "  clean   - Remove build artifacts"
	@echo "  install - (Placeholder for Amiga installation)"
	@echo ""
	@echo "Usage:"
	@echo "  make                # Build the Amiga executable"
	@echo "  make clean          # Clean build artifacts"
	@echo ""
	@echo "Environment variables:"
	@echo "  AMIGA_PREFIX        # Amiga cross-compiler prefix (default: /opt/amiga)"
	@echo "  CFLAGS              # Compiler flags"
	@echo "  LDFLAGS             # Linker flags"