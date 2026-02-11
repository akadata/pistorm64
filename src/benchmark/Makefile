# Makefile for PiStorm benchmark utility
# Uses the new kernel module interface

CC = gcc
CFLAGS = -Wall -Wextra -Werror -O3 -DPISTORM_KMOD -DPISTORM_ENABLE_BATCH=1 -I../.. -I../../src -I../../include -I../../include/uapi
LDFLAGS = -lpthread -lm

# Source files
SOURCES = benchmark.c ../../src/gpio/ps_protocol_kmod.c ../../src/log.c
TARGET = benchmark

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

# Additional targets for convenience
help:
	@echo "PiStorm Benchmark Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build the benchmark utility (default)"
	@echo "  clean    - Remove built files"
	@echo "  install  - Install the benchmark to /usr/local/bin (requires sudo)"
	@echo ""
	@echo "Usage after build:"
	@echo "  sudo ./benchmark --chip-kb 1024 --repeat 3"
	@echo "  sudo ./benchmark --memtest --chip-kb 1024"
	@echo ""
	@echo "Note: Run with sudo privileges and ensure the emulator is not running."