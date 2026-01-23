# Compiler selection - follows same pattern as main Makefile
ifeq ($(C),clang)
CC := clang
CXX := clang++
endif

ifeq ($(C),gcc)
CC := gcc
CXX := g++
endif

CC ?= gcc

CFLAGS = -Wall -Wextra -pedantic -O2 -std=c99
LDFLAGS = -lm

TARGET = janus-mips-client
SOURCES = janus-mips-client.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

.PHONY: help
help:
	@echo "Targets:"
	@echo "  all     - Build the executable"
	@echo "  clean   - Remove build artifacts"
	@echo "  install - Install to system"
	@echo ""
	@echo "Usage:"
	@echo "  make                # Build the client"
	@echo "  make C=gcc          # Build with gcc"
	@echo "  make C=clang        # Build with clang"
	@echo "  make clean          # Clean build artifacts"
	@echo "  make install        # Install to /usr/local/bin/"