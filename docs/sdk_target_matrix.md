# SDK Target Matrix

This document defines the build/runtime target split for current PiStorm64 work.

## Primary SDK Policy

- Primary development SDK: `/opt/amiga/ndk3.2`
- Compatibility SDK floor: `/opt/amiga/ndk` (3.1)
- Runtime compatibility targets: AmigaOS 3.2, 3.9
- PPC runtime target track: AmigaOS 4.1 (separate profile path)

## Build Matrix

| Component Class | Primary SDK | Secondary SDK | Notes |
|---|---|---|---|
| New Amiga-side tools/utilities | NDK 3.2 | NDK 3.1 | Default to 3.2 headers/libs, keep 3.1 guards where practical. |
| Legacy compatibility utilities | NDK 3.1 | NDK 3.2 | Use when strict old ABI behavior is required. |
| A314 userland binaries | NDK 3.2 | NDK 3.1 | Validate on both OS3.2 and OS3.9 at runtime. |
| PPC board diagnostics (`ppcshake` etc.) | NDK 3.2 | NDK 3.1 | Tooling can be built with 3.2 while preserving old-friendly code paths. |
| PPC backend host code (`qemu-uae` bridge) | Host toolchain | N/A | Not built against Amiga NDK. |

## Runtime Test Matrix

| Runtime OS | Expected Role | Must-Pass Baseline |
|---|---|---|
| AmigaOS 3.2 | Core daily target | Boot, storage, RTG, A314, stable format/copy operations. |
| AmigaOS 3.9 | Compatibility target | Boot, storage, RTG, no regressions in core tools. |
| AmigaOS 4.1 | PPC-native target track | PPC board discovery, handoff path, controlled backend tests. |

## PPC Profiles (Operational)

- `OS3.2 profile`: keep PPC board available but conservative start behavior.
  - Goal: responsive 68k system while PPC tooling is present.
- `OS4.1 profile`: enable PPC-focused startup/handoff behavior.
  - Goal: maximize PPC bring-up success for native PPC workflow.

Keep these as separate config profiles. Do not tune both goals in one single default profile.

## Utility HDF Naming Policy

Current utility disk path in default config references `pistorm.hdf`.  
For neutral naming and Janus direction, migrate to a new neutral utility disk name.

Recommended name:

- `janus-utils.hdf`

Optional alternatives:

- `janus-engine.hdf`
- `janus-shared.hdf`

Recommended migration approach:

1. Create new image and copy required tools/drivers.
2. Switch config references from old utility image path to new name.
3. Keep old image as fallback during one validation cycle.

This keeps compatibility while removing product-name coupling from runtime media naming.

