# JIT Integration Tasklist

Planned sequence to add an optional ARM JIT backend while keeping Musashi as default:

1. CPU backend abstraction (in progress)
   - Add a backend switch with `ENABLE_JIT` (build/runtime) and keep Musashi as the default path.
   - Stub JIT backend initially forwards to Musashi for correctness.
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

Status: Step 1 scaffolded (backend flag + stub). Steps 2–5 not started.***
