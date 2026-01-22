AMIGA_PREFIX ?= /opt/amiga
CC := $(AMIGA_PREFIX)/bin/m68k-amigaos-gcc

CFLAGS ?= -m68030 -O2 -noixemul -DHAS_STDLIB -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -lamiga

BINS := janusd janus-client janus-fractal janus-ball janus-juggler
AMIGA_OUT ?= ../../../data/a314-shared/pijanus

all: $(BINS)

janusd: janusd.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

janus-client: janus-client.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

janus-ball: janus-ball.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

janus-juggler: janus-juggler.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

janus-fractal: janus-fractal.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: all
	mkdir -p $(AMIGA_OUT)
	cp -f $(BINS) $(AMIGA_OUT)/

clean:
	rm -f $(BINS)
