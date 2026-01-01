# JIT Integration Tasklist

Planned sequence to add an optional ARM JIT backend while keeping Musashi as default:

1. CPU backend abstraction (in progress)
   - Backend switch via `--jit`/`--enable-jit` or config `jit on|yes|1`; Musashi stays default.
   - Stub JIT backend currently forwards to Musashi for correctness.
   - FPU-only JIT toggle (`--jit-fpu` or config `jitfpu on|yes|1`) routes F-line opcodes through the JIT hook (currently still Musashi).
2. Threading/affinity hooks (pending)
   - Pin CPU backend to one core; pin Pi I/O (PiSCSI/net/A314/RTG) to other cores.
   - Expose config knobs for affinities.
3. Experimental hosted JIT (pending)
   - Reuse PJIT/Emu68 ideas for a block cache and opcode stubs.
   - Use PiStorm memory callbacks; fallback to Musashi for unhandled ops.
4. Runtime controls and fallback (pending)
   - Runtime switch back to Musashi on JIT errors; add logging/metrics.
5. Kernel module prototype (pending)
   - Mirror the backend interface in `emulator.ko`; keep Pi-side services on other cores.

Status: Step 1 scaffolded (backend flag + stub, config toggles, F-line hook). Steps 2–5 not started.***
