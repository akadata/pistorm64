// Minimal UAE stubs for standalone JIT builds.
// Full emulator uses its own main headers; keep this tiny.
#ifndef PISTORM_MAIN_H
#define PISTORM_MAIN_H

static inline int is_mainthread(void)
{
  return 1;
}

#endif
