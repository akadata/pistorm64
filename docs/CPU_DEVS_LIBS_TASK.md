# CPU-Specific Amiga Drivers/Libraries Task (Deferred)

Yes — that model is sound, historically correct, and achievable. This is exactly how well-behaved
Amiga drivers should work, and it aligns beautifully with PiStorm64’s philosophy of “disciplined
hardware companion”.

Let’s separate what the Amiga already does for you, what you need to implement, and what is a bonus
rather than a requirement.

## The important grounding truth (good news)

You do not need to invent a new loader mechanism.

AmigaOS, Picasso96, and Exec already give you most of what you want for free.

The trick is to lean into conventions instead of fighting them.

## 1. How CPU selection already works on Amiga

### ExecBase tells the truth

On every Amiga:

- `SysBase->AttnFlags` encodes the CPU family
- Libraries, devices, and drivers routinely check this

Example (classic pattern):

- `AFF_68020`
- `AFF_68030`
- `AFF_68040`
- `AFF_68060`

This is not new, and it’s safe.

## 2. Picasso96 card drivers are not “one-size-only”

Picasso96 does not hard-bind to a single card filename internally.

What happens in practice:

- P96 loads a card driver
- That driver:
  - Identifies itself
  - Exposes capabilities
  - Registers mode tables

Nothing prevents multiple `.card` binaries from existing side-by-side.

This is why historically you’ll find things like:

- `PicassoIV.card`
- `CyberVision64.card`
- CPU-specific builds distributed separately

## 3. The clean PiStorm64 model (recommended)

You’ve already converged on the right shape:

```
LIBS:Picasso96/
    pigfx020.card
    pigfx030.card
    pigfx040.card
    pigfx060.card
```

Each one:

- Is built with the correct `-m680xx`
- Shares the same source
- Registers the same logical PiGFX board
- Differs only in instruction guarantees and tuning

This is correct.

## 4. How to ensure “only the correct one loads”

There are two valid approaches, one mandatory, one optional.

### A. Mandatory (and sufficient): CPU gating inside the driver

Each `.card` should self-reject if the CPU is wrong.

At startup (early in init):

```
if (!(SysBase->AttnFlags & AFF_68040)) {
    return FALSE;   // or appropriate P96 failure
}
```

So:

- `pigfx020.card` → requires ≥020
- `pigfx030.card` → requires ≥030
- `pigfx040.card` → requires ≥040
- `pigfx060.card` → requires ≥060

Result:

- Multiple cards can exist
- Only the matching one initialises
- Others silently decline

This is standard Amiga practice. No emulator logic required.

### B. Optional bonus: deterministic selection via installer / prefs

Later (not now), you can improve UX by:

- PiGFX installer detecting CPU
- Only copying the correct `.card`
- Or setting a preference flag

Nice to have. Not required.

## 5. Emulator CPU ≠ Amiga CPU (important distinction)

You correctly noted this: the emulator can be 020 030 040 etc.

Key insight:

- Amiga-side CPU identity matters
- Pi-side emulator implementation does not

As long as:

- The emulator advertises the correct CPU to AmigaOS
- `AttnFlags` reflect reality

Then:

- The correct `.card` will load
- The wrong ones will politely step aside

This keeps:

- Benchmarks honest
- Real3D sane
- RTG predictable

## 6. Libraries, devices, and “the same rule everywhere”

Once you adopt this pattern, it scales cleanly:

- `.card` files
- `.library` files
- `.device` files

All can follow the same rule:

Multiple binaries may exist; each binary self-selects based on CPU.

This is exactly how Commodore, Phase5, and CyberGraphX-era vendors did it.

You’re not inventing anything — you’re restoring discipline.

## 7. Why this is especially right for PiStorm64

This fits your stated goals perfectly:

- Emulator = JANUS-style bus engine
- Pi = co-processor, not a cheat
- AmigaOS = remains in charge
- CPU class remains meaningful

And crucially:

- You avoid “magic glue logic” in the emulator.

That’s a huge win.

## Final summary (short version)

- Build `pigfx020 / 030 / 040 / 060`
- Install all of them if you want
- Each driver self-checks `AttnFlags`
- Only the correct one activates
- Emulator stays clean
- Behaviour matches real Amiga hardware practice
- Future-proof and extensible

This is one of those design decisions that quietly separates a toy accelerator from a proper system
component.

You’re firmly on the right side of that line.
