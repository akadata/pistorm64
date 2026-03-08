# AGENTS.md

## Purpose

This repository implements a Zorro-visible ARM64 execution path for PiStorm-based Amiga systems.

The goal is to let the Amiga launch ARM64 payloads that execute natively on the Pi side through a stable mailbox/shared-memory contract, while keeping Amiga-side integration clean, minimal, and generic.

This is **not** a fake CPU replacement, not an emulator inside an emulator, and not a place for application-specific behavior in launcher code.

## Canonical Model (Do Not Drift)

This repository follows one execution model:

* the application artifact is an AArch64 ELF payload
* launch is initiated from Amiga side (Workbench/CLI)
* instruction execution happens on the ARM coprocessor side
* AmigaOS remains the service owner (Exec/DOS/Intuition/devices/libraries/Workbench)
* all AmigaOS access from payloads is proxied via ARMAccel Service ABI and fulfilled by m68k NDK-backed runtime code

Do not reframe this as a remote-app runtime.
Do not reframe this as a m68k app with optional ARM helpers unless a project explicitly chooses that hybrid model.

## Core Architecture

There are four distinct layers:

### 1. Zorro ARM64 accelerator hardware

The board exposes:

* AutoConfig identity
* MMIO registers
* mailbox/job control
* shared memory window
* completion/status reporting
* optional interrupt signaling

This layer is hardware-facing and protocol-facing only.

### 2. `armaccel.device` (transport)

Responsibilities:

* board discovery/probe
* register access
* mailbox read/write
* shared-memory staging/chunk upload
* job start/wait/reset/recovery
* IRQ and polling completion plumbing

`armaccel.device` must not perform app policy decisions.

### 3. `armaccel.library` (runtime/policy)

Responsibilities:

* ELF inspection
* personality and ABI checks
* service requirement checks
* execution orchestration through `armaccel.device`
* result/status mapping to app-facing API

`armaccel.library` is the single app-facing execution interface.

### 4. ARM64 payload ELF

The payload is the application and owns:

* app behavior and state
* rendering/compute logic
* app-specific command handling
* app-specific UI semantics

A payload must be replaceable without changing launcher/runtime policy code.
Payloads do not call Amiga NDK APIs directly; they request services through the ABI.

## Non-Negotiable Rules

### Rule 1: `armshake` is diagnostics-only

`armshake` is only for:

* board identity/probe
* ping/irq checks
* mailbox/state/contract diagnostics
* optional raw descriptor smoke tests for development

`armshake` is never the user-facing payload launcher.

### Rule 2: No wrapper binary proliferation

Do not create new app-facing launcher binaries per payload type/class.

No `armfractal`-style pattern and no “generic launcher in disguise” replacements.

Execution must flow through `armaccel.library`.

A single optional generic launcher tool (for example `SYS:Tools/armrun`) is acceptable when it is a thin shim that only forwards to `armaccel.library` and does not add app semantics.

`armrun` may:

* locate payload path(s)
* call `armaccel.library` query/execute
* report status/result

`armrun` may not:

* create app windows
* implement menus/requesters
* implement file-manager/editor/game/fractal logic
* contain runtime policy
* contain payload-specific behavior

### Rule 3: Keep transport and policy separate

* `armaccel.device` = transport mechanics only
* `armaccel.library` = detect/decide/execute policy only

Do not mix these concerns.

### Rule 4: Payload logic belongs in payloads

Do not move payload-specific menus, commands, defaults, toggles, or semantics into `armshake`, `armaccel.device`, or `armaccel.library`.

### Rule 5: Personality checks must be explicit

`armaccel.library` personality/compatibility decisions should be explicit and predictable, including checks such as:

* ELF class = 64-bit
* machine = AArch64
* endianness = little-endian
* embedded ELF NOTE `.note.armaccel` (ABI/personality/services/class)
* required services vs available services

Compatibility outcomes should clearly distinguish:

* not an ARMAccel ELF
* valid and runnable
* valid but needs unavailable services
* valid but wrong ABI version

### Rule 6: File association is metadata-driven

Detection must come from metadata embedded in the ELF itself (NOTE section), not sidecar files and not app-specific CLI switches.

### Rule 7: Stable ABI first, features second

Evolve carefully:

* Launch ABI: submit/start/wait/result contract
* Service ABI: generic AmigaOS service brokerage (window/menu/requester/surface/input/files/timers/audio/video/io/disk/network as standardized classes)

Everything else belongs outside runtime transport/policy layers.

No demo-specific feature work should bypass or pre-empt ABI freeze work.

### Rule 8: Never bind system limits to visible aperture size

The visible board window is not the final system limit.

Large payloads/assets/render surfaces must remain supported via chunking/staging/streaming designs.

## Direction for Public API

The library should remain the canonical execution path for Workbench, CLI tools, and applications.

Current shape to preserve:

* `ARMACCEL_IsSupportedELF(path)`
* `ARMACCEL_QueryELF(path, struct ArmAccelELFInfo *)`
* `ARMACCEL_ExecuteELF(path, struct ArmAccelRunOpts *, struct ArmAccelResult *)`

## Build and Install Conventions

When adding build targets in this repo:

* provide `make install`
* support `INSTALL_DIR=...` override
* default install path for Amiga-shared transfer: `/opt/pistorm64/data/a314-shared/`
* runtime binaries follow Amiga naming conventions:
  * libraries end with `.library`
  * devices end with `.device`

## Review Gate

Before merging any change, ask:

1. Does this add generic execution/service infrastructure?
2. Or does it sneak application behavior into diagnostics/runtime layers?

If it does the second, reject or redesign.

## Practical Test

A correct design means:

* a new ARM64 `.elf` can be dropped in
* `armaccel.library` can decide compatibility and run it via `armaccel.device`
* no app-specific switch or enum is added to diagnostics/runtime code
* no per-app launcher rewrite is needed

When this remains true, the architecture is on track.
