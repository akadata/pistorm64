DiagROM MOD Player Notes (for regtool/A314 integration)

DiagROM ships a ProTracker replay and a test module:
- `external/DiagROM/Music.MOD`
- Replay routine in `external/DiagROM/DiagROM.s`:
  - `MT_Init` (init)
  - `MT_Music` (per‑frame tick)
  - `MT_End` (stop)
  - Master volume symbol: `mt_MasterVol`

How DiagROM plays the module
- Copies `Music.MOD` from ROM to Chip RAM (`GetChip`).
- Sets A5 = `$DFF000` before calling the replay routines.
- Calls `MT_Init` once, then repeatedly calls `MT_Music` each frame.
- Calls `MT_End` on exit.

Why regtool can’t directly “play MOD”
- `regtool` only pokes registers over the PiStorm bus.
- MOD playback requires a 68k replay routine running on the Amiga side.
- The replay drives Paula DMA, updates periods/volumes, and advances patterns.

Practical path to MOD playback from Pi side
1) Add a tiny Amiga‑side helper that runs the DiagROM replay:
   - Copy `Music.MOD` (or any MOD) into Chip RAM.
   - Copy the replay routine (`MT_Init..mt_END`) into Chip RAM.
   - Call `MT_Init`, then call `MT_Music` once per frame (or VBlank).
2) Expose a control channel:
   - A314 device (recommended): send commands like `LOAD`, `PLAY`, `STOP`.
   - Or pistorm‑dev/Z2: write a small command block into Z2 memory.
3) Keep `regtool` for raw audio tests only (simple waveforms).

Useful DiagROM entry points (reference)
- In `DiagROM.s`:
  - `MT_Init` at ~15351
  - `MT_Music` at ~15399
  - `MT_End` at ~15392
  - `mt_END` marks replay end

Next steps (if we proceed)
- Extract the replay routine and wrap it in a small 68k program.
- Add an A314 “modplay” service + client to load/play/stop.
- Optional: extend `regtool` with `--mod` that delegates to A314.
