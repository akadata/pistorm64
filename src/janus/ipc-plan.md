Janus IPC plan (draft)
======================

Goal
----
Provide a Janus-style IPC layer that lets Amiga tasks communicate with a
Pi-side "remote CPU" service (QEMU, DOSBox, or a custom host service),
using shared memory and lightweight doorbell signaling.

Constraints and assumptions
---------------------------
- No FC lines available on the CPLD; assume simple register doorbells.
- Amiga side should use Exec message ports for local IPC and task wakeups.
- Pi side should be able to poll or receive interrupts for doorbells.
- Keep shared regions non-cacheable or explicitly flushed on 68020+.

Reference
---------
- ADCD 2.1, Libraries Manual, node028C:
  "Introduction to Exec / Interprocess Communications"
  (Exec message ports, WaitPort/GetMsg/ReplyMsg model).
  File: Hardware/ADCD_2.1/_txt/Libraries_Manual_guide/node028C.html.txt

Existing building blocks in this tree
-------------------------------------
- PiStorm interaction device registers:
  src/platforms/amiga/pistorm-dev/pistorm-dev-enums.h
- Janus ring registration command:
  PI_CMD_JANUS_INIT (PTR1 base, WORD1 size, WORD2 flags)
- Amiga-side interaction stubs/tools:
  src/platforms/amiga/pistorm-dev/pistorm_dev_amiga/
- Autoconf mapping and custom ranges:
  src/platforms/amiga/amiga-autoconf.c

IPC sketch
----------
1) Shared memory region (Z2 or Z3 autoconf RAM):
   - Ring buffers for host->amiga and amiga->host.
   - Simple header: write_index, read_index, size, seq.
2) Doorbell registers (pistorm-dev space):
   - AMIGA->PI: write "kick" + seq.
   - PI->AMIGA: set status register + optional interrupt.
3) Amiga-side daemon:
   - Opens message port for local clients.
   - Pumps messages into shared ring and waits on doorbell/status.
4) Pi-side service:
   - Handles command queue, dispatches to QEMU/DOSBox or native helpers.
   - Sends responses back over the ring with status codes.

Demo command
------------
- JANUS_CMD_FRACTAL: Pi renders a Mandelbrot frame into an Amiga RAM
  buffer and sets a status word when complete.
- JANUS_CMD_TEXT: small text payload for sanity checks in the Pi log.

Remote CPU ideas
----------------
- QEMU i386 with a minimal virtual "Janus device" for data + framebuffer.
- DOSBox as a lightweight proof-of-concept (keyboard/mouse + file IO).
- RTG surface as host framebuffer target (Amiga window or full-screen).

Open questions
--------------
- Best signaling path for PI->AMIGA: polling vs. level interrupt.
- Where to place shared memory in Z2 vs Z3 without breaking Fast RAM use.
- Minimum command set: file IO, clipboard, keyboard/mouse, video blit.
