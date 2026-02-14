# RTG

RTG (graphics) uses a Picasso96-style framebuffer backed by host memory.

Paths:
- `platforms/amiga/rtg/`
- `platforms/amiga/rtg/readme.md`

Build flags:
- `make USE_RAYLIB=0` to disable RTG/raylib.

RTG memory is typically mapped into a high address space region (e.g. `0x70010000+`).

Current note:

- RTG is working, but there is a known window-decoration issue still being resolved.
