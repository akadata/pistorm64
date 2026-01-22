Amiga-side Janus daemon (skeleton)
==================================

janusd.c is a minimal Exec message-port server that writes incoming
payloads into a shared ring buffer and kicks the Pi-side doorbell.

Build (vbcc example):
  vc +kick13 -c99 janusd.c -o janusd
  vc +kick13 -c99 janus-client.c -o janus-client
  vc +kick13 -c99 janus-fractal.c -o janus-fractal

Build (amiga-gcc):
  make
  make install

The Makefile uses /opt/amiga/bin/m68k-amigaos-gcc and copies binaries to:
  data/a314-shared/pijanus

Amiga install script:
  data/a314-shared/pijanus/Install

You can link it with your preferred Amiga toolchain. It is intended
as a starting point for a full Janus IPC daemon.

janus-client.c is a tiny test sender that posts a message to the
PiStormJanus port and waits for a reply.

janus-fractal.c opens a Workbench window, asks the Pi to render a
fractal into an Amiga RAM buffer, and blits the result with
WritePixelArray8. Left click zooms in, right click zooms out.
