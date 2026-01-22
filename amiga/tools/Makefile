AMIGA_PREFIX ?= /opt/amiga
CC := $(AMIGA_PREFIX)/bin/m68k-amigaos-gcc

CFLAGS ?= -m68030 -O2 -noixemul -DHAS_STDLIB -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -lamiga -L$(AMIGA_PREFIX)/m68k-amigaos/libnix/lib -lsocket -lnix -lm

BINS := arp ping route netstat

all: $(BINS)

$(BINS): %: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(BINS)
